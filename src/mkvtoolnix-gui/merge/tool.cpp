#include "common/common_pch.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

#include "common/qt.h"
#include "mkvtoolnix-gui/app.h"
#include "mkvtoolnix-gui/forms/merge/tool.h"
#include "mkvtoolnix-gui/forms/main_window/main_window.h"
#include "mkvtoolnix-gui/jobs/tool.h"
#include "mkvtoolnix-gui/merge/source_file.h"
#include "mkvtoolnix-gui/merge/tab.h"
#include "mkvtoolnix-gui/merge/tool.h"
#include "mkvtoolnix-gui/main_window/main_window.h"
#include "mkvtoolnix-gui/util/file_dialog.h"
#include "mkvtoolnix-gui/util/files_drag_drop_handler.h"
#include "mkvtoolnix-gui/util/message_box.h"
#include "mkvtoolnix-gui/util/settings.h"
#include "mkvtoolnix-gui/util/string.h"
#include "mkvtoolnix-gui/util/widget.h"

namespace mtx::gui::Merge {

using namespace mtx::gui;

class ToolPrivate {
  friend class Tool;

  // UI stuff:
  std::unique_ptr<Ui::Tool> ui;
  QMenu *mergeMenu{};
  mtx::gui::Util::FilesDragDropHandler filesDDHandler{Util::FilesDragDropHandler::Mode::Remember};

  ToolPrivate(QMenu *p_mergeMenu);
};

ToolPrivate::ToolPrivate(QMenu *p_mergeMenu)
  : ui{new Ui::Tool}
  , mergeMenu{p_mergeMenu}
{
}

// ------------------------------------------------------------

Tool::Tool(QWidget *parent,
           QMenu *mergeMenu)
  : ToolBase{parent}
  , p_ptr{new ToolPrivate{mergeMenu}}
{
  auto &p = *p_func();

  // Setup UI controls.
  p.ui->setupUi(this);

  MainWindow::get()->registerSubWindowWidget(*this, *p.ui->merges);
}

Tool::~Tool() {
}

void
Tool::setupUi() {
  auto &p = *p_func();

  Util::setupTabWidgetHeaders(*p.ui->merges);

  showMergeWidget();

  retranslateUi();
}

void
Tool::setupActions() {
  auto &p   = *p_func();
  auto mw   = MainWindow::get();
  auto mwUi = MainWindow::getUi();

  connect(p.mergeMenu,                                &QMenu::aboutToShow,               this, &Tool::enableMenuActions);
  connect(p.mergeMenu,                                &QMenu::aboutToHide,               this, &Tool::enableCopyMenuActions);

  connect(mwUi->actionMergeNew,                       &QAction::triggered,               this, &Tool::newConfig);
  connect(mwUi->actionMergeOpen,                      &QAction::triggered,               this, &Tool::openConfig);
  connect(mwUi->actionMergeClose,                     &QAction::triggered,               this, &Tool::closeCurrentTab);
  connect(mwUi->actionMergeCloseAll,                  &QAction::triggered,               this, &Tool::closeAllTabs);
  connect(mwUi->actionMergeSave,                      &QAction::triggered,               this, &Tool::saveConfig);
  connect(mwUi->actionMergeSaveAll,                   &QAction::triggered,               this, &Tool::saveAllConfigs);
  connect(mwUi->actionMergeSaveAs,                    &QAction::triggered,               this, &Tool::saveConfigAs);
  connect(mwUi->actionMergeSaveOptionFile,            &QAction::triggered,               this, &Tool::saveOptionFile);
  connect(mwUi->actionMergeStartMuxing,               &QAction::triggered,               this, &Tool::startMuxing);
  connect(mwUi->actionMergeStartMuxingAll,            &QAction::triggered,               this, &Tool::startMuxingAll);
  connect(mwUi->actionMergeAddToJobQueue,             &QAction::triggered,               this, &Tool::addToJobQueue);
  connect(mwUi->actionMergeAddAllToJobQueue,          &QAction::triggered,               this, &Tool::addAllToJobQueue);
  connect(mwUi->actionMergeShowMkvmergeCommandLine,   &QAction::triggered,               this, &Tool::showCommandLine);
  connect(mwUi->actionMergeCopyFirstFileNameToTitle,  &QAction::triggered,               this, &Tool::copyFirstFileNameToTitle);
  connect(mwUi->actionMergeCopyOutputFileNameToTitle, &QAction::triggered,               this, &Tool::copyOutputFileNameToTitle);
  connect(mwUi->actionMergeCopyTitleToOutputFileName, &QAction::triggered,               this, &Tool::copyTitleToOutputFileName);

  connect(p.ui->merges,                               &QTabWidget::tabCloseRequested,    this, &Tool::closeTab);
  connect(p.ui->newFileButton,                        &QPushButton::clicked,             this, &Tool::newConfig);
  connect(p.ui->openFileButton,                       &QPushButton::clicked,             this, &Tool::openConfig);

  connect(mw,                                         &MainWindow::preferencesChanged,   this, [&p]() { Util::setupTabWidgetHeaders(*p.ui->merges); });
  connect(mw,                                         &MainWindow::preferencesChanged,   this, &Tool::retranslateUi);
  connect(mw,                                         &MainWindow::preferencesChanged,   this, []() { SourceFile::setupFromPreferences(); });

  connect(App::instance(),                            &App::addingFilesToMergeRequested, this, &Tool::addMultipleFilesFromCommandLine);
  connect(App::instance(),                            &App::openConfigFilesRequested,    this, &Tool::openMultipleConfigFilesFromCommandLine);
}

void
Tool::enableMenuActions() {
  auto mwUi   = MainWindow::getUi();
  auto tab    = currentTab();
  auto hasTab = tab && tab->isEnabled();

  mwUi->actionMergeSave->setEnabled(hasTab);
  mwUi->actionMergeSaveAs->setEnabled(hasTab);
  mwUi->actionMergeSaveOptionFile->setEnabled(hasTab);
  mwUi->actionMergeClose->setEnabled(hasTab);
  mwUi->actionMergeStartMuxing->setEnabled(hasTab);
  mwUi->actionMergeAddToJobQueue->setEnabled(hasTab);
  mwUi->actionMergeShowMkvmergeCommandLine->setEnabled(hasTab);
  mwUi->actionMergeCopyFirstFileNameToTitle->setEnabled(hasTab && tab->hasSourceFiles());
  mwUi->actionMergeCopyOutputFileNameToTitle->setEnabled(hasTab && tab->hasDestinationFileName());
  mwUi->actionMergeCopyTitleToOutputFileName->setEnabled(hasTab && tab->hasTitle());
  mwUi->menuMergeAll->setEnabled(hasTab);
  mwUi->actionMergeSaveAll->setEnabled(hasTab);
  mwUi->actionMergeCloseAll->setEnabled(hasTab);
  mwUi->actionMergeStartMuxingAll->setEnabled(hasTab);
  mwUi->actionMergeAddAllToJobQueue->setEnabled(hasTab);
}

void
Tool::enableCopyMenuActions() {
  auto mwUi = MainWindow::getUi();

  mwUi->actionMergeCopyFirstFileNameToTitle->setEnabled(true);
  mwUi->actionMergeCopyOutputFileNameToTitle->setEnabled(true);
  mwUi->actionMergeCopyTitleToOutputFileName->setEnabled(true);
}

void
Tool::showMergeWidget() {
  auto &p = *p_func();

  p.ui->stack->setCurrentWidget(p.ui->merges->count() ? p.ui->mergesPage : p.ui->noMergesPage);
  enableMenuActions();
  enableCopyMenuActions();
}

void
Tool::toolShown() {
  MainWindow::get()->showTheseMenusOnly({ p_func()->mergeMenu });
  showMergeWidget();
}

void
Tool::retranslateUi() {
  auto &p            = *p_func();
  auto buttonToolTip = Util::Settings::get().m_uiDisableToolTips ? Q("") : App::translate("CloseButton", "Close Tab");

  p.ui->retranslateUi(this);

  for (auto idx = 0, numTabs = p.ui->merges->count(); idx < numTabs; ++idx) {
    static_cast<Tab *>(p.ui->merges->widget(idx))->retranslateUi();
    auto button = Util::tabWidgetCloseTabButton(*p.ui->merges, idx);
    if (button)
      button->setToolTip(buttonToolTip);
  }
}

Tab *
Tool::currentTab() {
  auto &p = *p_func();

  return static_cast<Tab *>(p.ui->merges->widget(p.ui->merges->currentIndex()));
}

Tab *
Tool::appendTab(Tab *tab) {
  auto &p = *p_func();

  connect(tab, &Tab::removeThisTab, this, &Tool::closeSendingTab);
  connect(tab, &Tab::titleChanged,  this, &Tool::tabTitleChanged);

  p.ui->merges->addTab(tab, Util::escape(tab->title(), Util::EscapeKeyboardShortcuts));
  p.ui->merges->setCurrentIndex(p.ui->merges->count() - 1);

  showMergeWidget();

  return tab;
}

void
Tool::newConfig() {
  appendTab(new Tab{this});
}

void
Tool::openConfig() {
  auto &settings = Util::Settings::get();
  auto fileName  = Util::getOpenFileName(this, QY("Open settings file"), settings.lastConfigDirPath(), QY("MKVToolNix GUI config files") + Q(" (*.mtxcfg);;") + QY("All files") + Q(" (*)"));
  if (fileName.isEmpty())
    return;

  openConfigFile(fileName);
}

void
Tool::openConfigFile(QString const &fileName) {
  Util::Settings::change([&fileName](Util::Settings &cfg) {
    cfg.m_lastConfigDir.setPath(QFileInfo{fileName}.path());
  });

  if (MainWindow::jobTool()->addJobFile(fileName)) {
    MainWindow::get()->setStatusBarMessage(QY("The job has been added to the job queue."));
    return;
  }

  auto tab = currentTab();
  if (tab && tab->isEmpty())
    tab->deleteLater();

  appendTab(new Tab{this})
    ->load(fileName);
}

void
Tool::openFromConfig(MuxConfig const &config) {
  appendTab(new Tab{this})
    ->cloneConfig(config);
}

bool
Tool::closeTab(int index) {
  auto &p = *p_func();

  if ((0  > index) || (p.ui->merges->count() <= index))
    return false;

  auto tab = static_cast<Tab *>(p.ui->merges->widget(index));

  if (Util::Settings::get().m_warnBeforeClosingModifiedTabs && tab->hasBeenModified()) {
    MainWindow::get()->switchToTool(this);
    p.ui->merges->setCurrentIndex(index);

    auto answer = Util::MessageBox::question(this)
      ->title(QY("Close modified settings"))
      .text(QY("The multiplex settings creating \"%1\" have been modified. Do you really want to close? All changes will be lost.").arg(tab->title()))
      .buttonLabel(QMessageBox::Yes, QY("&Close settings"))
      .buttonLabel(QMessageBox::No,  QY("Cancel"))
      .exec();
    if (answer != QMessageBox::Yes)
      return false;
  }

  p.ui->merges->removeTab(index);
  delete tab;

  showMergeWidget();

  return true;
}

void
Tool::closeCurrentTab() {
  auto &p = *p_func();

  closeTab(p.ui->merges->currentIndex());
}

void
Tool::closeSendingTab() {
  auto &p  = *p_func();
  auto idx = p.ui->merges->indexOf(dynamic_cast<Tab *>(sender()));
  if (-1 != idx)
    closeTab(idx);
}

bool
Tool::closeAllTabs() {
  auto &p = *p_func();

  for (auto index = p.ui->merges->count(); index > 0; --index) {
    p.ui->merges->setCurrentIndex(index);
    if (!closeTab(index - 1))
      return false;
  }

  return true;
}

void
Tool::saveConfig() {
  auto tab = currentTab();
  if (tab)
    tab->onSaveConfig();
}

void
Tool::saveAllConfigs() {
  forEachTab([](Tab &tab) { tab.onSaveConfig(); });
}

void
Tool::saveConfigAs() {
  auto tab = currentTab();
  if (tab)
    tab->onSaveConfigAs();
}

void
Tool::saveOptionFile() {
  auto tab = currentTab();
  if (tab)
    tab->onSaveOptionFile();
}

void
Tool::startMuxing() {
  auto tab = currentTab();
  if (tab)
    tab->addToJobQueue(true);
}

void
Tool::startMuxingAll() {
  forEachTab([](Tab &tab) { tab.addToJobQueue(true); });
}

void
Tool::addToJobQueue() {
  auto tab = currentTab();
  if (tab)
    tab->addToJobQueue(false);
}

void
Tool::addAllToJobQueue() {
  forEachTab([](Tab &tab) { tab.addToJobQueue(false); });
}

void
Tool::showCommandLine() {
  auto tab = currentTab();
  if (tab)
    tab->onShowCommandLine();
}

void
Tool::copyFirstFileNameToTitle() {
  auto tab = currentTab();
  if (tab && tab->isEnabled())
    tab->onCopyFirstFileNameToTitle();
}

void
Tool::copyOutputFileNameToTitle() {
  auto tab = currentTab();
  if (tab && tab->isEnabled())
    tab->onCopyOutputFileNameToTitle();
}

void
Tool::copyTitleToOutputFileName() {
  auto tab = currentTab();
  if (tab && tab->isEnabled())
    tab->onCopyTitleToOutputFileName();
}

void
Tool::tabTitleChanged() {
  auto &p  = *p_func();
  auto tab = dynamic_cast<Tab *>(sender());
  auto idx = p.ui->merges->indexOf(tab);
  if (tab && (-1 != idx))
    p.ui->merges->setTabText(idx, Util::escape(tab->title(), Util::EscapeKeyboardShortcuts));
}

void
Tool::dragEnterEvent(QDragEnterEvent *event) {
  auto &p = *p_func();

  p.filesDDHandler.handle(event, false);
}

void
Tool::dragMoveEvent(QDragMoveEvent *event) {
  auto &p = *p_func();

  p.filesDDHandler.handle(event, false);
}

void
Tool::dropEvent(QDropEvent *event) {
  auto &p = *p_func();

  if (p.filesDDHandler.handle(event, true))
    filesDropped(p.filesDDHandler.fileNames(), event->mouseButtons());
}

void
Tool::filesDropped(QStringList const &fileNames,
                   Qt::MouseButtons mouseButtons) {
  auto configExt  = Q(".mtxcfg");
  auto mediaFiles = QStringList{};

  for (auto const &fileName : fileNames)
    if (fileName.endsWith(configExt))
      openConfigFile(fileName);
    else
      mediaFiles << fileName;

  if (!mediaFiles.isEmpty())
    addMultipleFiles(mediaFiles, mouseButtons);
}

void
Tool::addMultipleFiles(QStringList const &fileNames,
                       Qt::MouseButtons mouseButtons) {
  auto &p = *p_func();

  if (!p.ui->merges->count())
    newConfig();

  auto tab = currentTab();
  Q_ASSERT(!!tab);

  tab->addFilesToBeAddedOrAppendedDelayed(fileNames, mouseButtons);
}

void
Tool::addMultipleFilesFromCommandLine(QStringList const &fileNames) {
  MainWindow::get()->switchToTool(this);
  addMultipleFiles(fileNames, Qt::NoButton);
}

void
Tool::openMultipleConfigFilesFromCommandLine(QStringList const &fileNames) {
  MainWindow::get()->switchToTool(this);
  for (auto const &fileName : fileNames)
    openConfigFile(fileName);
}

void
Tool::addMultipleFilesToNewSettings(QStringList const &fileNames,
                                    bool newSettingsForEachFile) {
  auto toProcess = fileNames;

  while (!toProcess.isEmpty()) {
    auto fileNamesToAdd = QStringList{};

    if (newSettingsForEachFile)
      fileNamesToAdd << toProcess.takeFirst();

    else {
      fileNamesToAdd = toProcess;
      toProcess.clear();
    }

    newConfig();

    auto tab = currentTab();
    Q_ASSERT(!!tab);

    tab->addFiles(fileNamesToAdd);
  }
}

void
Tool::addMergeTabIfNoneOpen() {
  auto &p = *p_func();

  if (!p.ui->merges->count())
    appendTab(new Tab{this});
}

void
Tool::forEachTab(std::function<void(Tab &)> const &worker) {
  auto &p           = *p_func();
  auto currentIndex = p.ui->merges->currentIndex();

  for (auto index = 0, numTabs = p.ui->merges->count(); index < numTabs; ++index) {
    p.ui->merges->setCurrentIndex(index);
    worker(dynamic_cast<Tab &>(*p.ui->merges->widget(index)));
  }

  p.ui->merges->setCurrentIndex(currentIndex);
}

std::pair<QString, QString>
Tool::nextPreviousWindowActionTexts()
  const {
  return {
    QY("&Next multiplex settings"),
    QY("&Previous multiplex settings"),
  };
}

}
