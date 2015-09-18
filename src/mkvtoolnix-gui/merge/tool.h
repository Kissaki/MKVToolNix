#ifndef MTX_MKVTOOLNIX_GUI_MERGE_TOOL_H
#define MTX_MKVTOOLNIX_GUI_MERGE_TOOL_H

#include "common/common_pch.h"

#include "mkvtoolnix-gui/main_window/tool_base.h"
#include "mkvtoolnix-gui/util/files_drag_drop_handler.h"

class QDragEnterEvent;
class QDropEvent;
class QMenu;

namespace mtx { namespace gui { namespace Merge {

namespace Ui {
class Tool;
}

class MuxConfig;
class Tab;

class Tool : public ToolBase {
  Q_OBJECT;

protected:
  // UI stuff:
  std::unique_ptr<Ui::Tool> ui;
  QMenu *m_mergeMenu;
  mtx::gui::Util::FilesDragDropHandler m_filesDDHandler;

public:
  explicit Tool(QWidget *parent, QMenu *mergeMenu);
  ~Tool();

  virtual void setupUi() override;
  virtual void setupActions() override;
  virtual void openConfigFile(QString const &fileName);
  virtual void openFromConfig(MuxConfig const &config);

public slots:
  virtual void retranslateUi();

  virtual void newConfig();
  virtual void openConfig();

  virtual bool closeTab(int index);
  virtual void closeCurrentTab();
  virtual void closeSendingTab();
  virtual bool closeAllTabs();

  virtual void saveConfig();
  virtual void saveConfigAs();
  virtual void saveOptionFile();
  virtual void startMuxing();
  virtual void addToJobQueue();
  virtual void showCommandLine();

  virtual void toolShown() override;
  virtual void tabTitleChanged();

  virtual void filesDropped(QStringList const &fileNames);

  virtual void addMultipleFiles(QStringList const &fileNames);
  virtual void addMultipleFilesFromCommandLine(QStringList const &fileNames);
  virtual void openMultipleConfigFilesFromCommandLine(QStringList const &fileNames);
  virtual void addMultipleFilesToNewSettings(QStringList const &fileNames, bool newSettingsForEachFile);

  virtual void setupTabPositions();

protected:
  Tab *appendTab(Tab *tab);
  virtual Tab *currentTab();

  virtual void enableMenuActions();
  virtual void showMergeWidget();

  virtual void dragEnterEvent(QDragEnterEvent *event) override;
  virtual void dropEvent(QDropEvent *event) override;
};

}}}

#endif // MTX_MKVTOOLNIX_GUI_MERGE_TOOL_H
