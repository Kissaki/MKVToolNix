#include "common/common_pch.h"

#include <QAction>
#include <QMenu>

#include <matroska/KaxSemantic.h>

#include "common/qt.h"
#include "mkvtoolnix-gui/main_window/main_window.h"
#include "mkvtoolnix-gui/main_window/preferences_dialog.h"
#include "mkvtoolnix-gui/util/modify_tracks_submenu.h"
#include "mkvtoolnix-gui/util/settings.h"

namespace mtx::gui::Util {

void
ModifyTracksSubmenu::setupTrack(QMenu &subMenu) {
  m_toggleTrackEnabledFlag     = new QAction{&subMenu};
  m_toggleDefaultTrackFlag     = new QAction{&subMenu};
  m_toggleForcedDisplayFlag    = new QAction{&subMenu};
  m_toggleOriginalFlag         = new QAction{&subMenu};
  m_toggleCommentaryFlag       = new QAction{&subMenu};
  m_toggleHearingImpairedFlag  = new QAction{&subMenu};
  m_toggleVisualImpairedFlag   = new QAction{&subMenu};
  m_toggleTextDescriptionsFlag = new QAction{&subMenu};

  m_toggleTrackEnabledFlag    ->setShortcut(Q("Ctrl+Alt+F, E"));
  m_toggleDefaultTrackFlag    ->setShortcut(Q("Ctrl+Alt+F, D"));
  m_toggleForcedDisplayFlag   ->setShortcut(Q("Ctrl+Alt+F, F"));
  m_toggleOriginalFlag        ->setShortcut(Q("Ctrl+Alt+F, O"));
  m_toggleCommentaryFlag      ->setShortcut(Q("Ctrl+Alt+F, C"));
  m_toggleHearingImpairedFlag ->setShortcut(Q("Ctrl+Alt+F, H"));
  m_toggleVisualImpairedFlag  ->setShortcut(Q("Ctrl+Alt+F, V"));
  m_toggleTextDescriptionsFlag->setShortcut(Q("Ctrl+Alt+F, T"));

  m_toggleTrackEnabledFlag    ->setData(libmatroska::KaxTrackFlagEnabled::ClassInfos.GlobalId.GetValue());
  m_toggleDefaultTrackFlag    ->setData(libmatroska::KaxTrackFlagDefault::ClassInfos.GlobalId.GetValue());
  m_toggleForcedDisplayFlag   ->setData(libmatroska::KaxTrackFlagForced::ClassInfos.GlobalId.GetValue());
  m_toggleCommentaryFlag      ->setData(libmatroska::KaxFlagCommentary::ClassInfos.GlobalId.GetValue());
  m_toggleOriginalFlag        ->setData(libmatroska::KaxFlagOriginal::ClassInfos.GlobalId.GetValue());
  m_toggleHearingImpairedFlag ->setData(libmatroska::KaxFlagHearingImpaired::ClassInfos.GlobalId.GetValue());
  m_toggleVisualImpairedFlag  ->setData(libmatroska::KaxFlagVisualImpaired::ClassInfos.GlobalId.GetValue());
  m_toggleTextDescriptionsFlag->setData(libmatroska::KaxFlagTextDescriptions::ClassInfos.GlobalId.GetValue());

  subMenu.addAction(m_toggleTrackEnabledFlag);
  subMenu.addAction(m_toggleDefaultTrackFlag);
  subMenu.addAction(m_toggleForcedDisplayFlag);
  subMenu.addAction(m_toggleOriginalFlag);
  subMenu.addAction(m_toggleCommentaryFlag);
  subMenu.addAction(m_toggleHearingImpairedFlag);
  subMenu.addAction(m_toggleVisualImpairedFlag);
  subMenu.addAction(m_toggleTextDescriptionsFlag);
}

void
ModifyTracksSubmenu::setupLanguage(QMenu &subMenu) {
  subMenu.clear();

  auto idx = 0;

  for (auto const &formattedLanguage : Util::Settings::get().m_languageShortcuts) {
    auto language = mtx::bcp47::language_c::parse(to_utf8(formattedLanguage));
    if (!language.is_valid())
      continue;

    ++idx;

    auto action = new QAction{&subMenu};
    action->setData(formattedLanguage);
    action->setText(Q(language.format_long()));

    if (idx <= 10)
      action->setShortcut(Q("Ctrl+Alt+A, %1").arg(idx % 10));

    subMenu.addAction(action);

    connect(action, &QAction::triggered, this, &ModifyTracksSubmenu::languageChangeTriggered);
  }

  if (idx)
    subMenu.addSeparator();

  m_configureLanguages = new QAction{&subMenu};
  subMenu.addAction(m_configureLanguages);

  connect(m_configureLanguages, &QAction::triggered, this, []() { MainWindow::get()->editPreferencesAndShowPage(PreferencesDialog::Page::LanguagesShortcuts); });

  retranslateUi();
}

void
ModifyTracksSubmenu::retranslateUi() {
  if (m_configureLanguages)
    m_configureLanguages->setText(QY("&Configure available languages"));

  if (!m_toggleTrackEnabledFlag)
    return;

  m_toggleTrackEnabledFlag    ->setText(QY("Toggle \"track &enabled\" flag"));
  m_toggleDefaultTrackFlag    ->setText(QY("Toggle \"&default track\" flag"));
  m_toggleForcedDisplayFlag   ->setText(QY("Toggle \"&forced display\" flag"));
  m_toggleOriginalFlag        ->setText(QY("Toggle \"&original\" flag"));
  m_toggleCommentaryFlag      ->setText(QY("Toggle \"&commentary\" flag"));
  m_toggleHearingImpairedFlag ->setText(QY("Toggle \"&hearing impaired\" flag"));
  m_toggleVisualImpairedFlag  ->setText(QY("Toggle \"&visual impaired\" flag"));
  m_toggleTextDescriptionsFlag->setText(QY("Toggle \"&text descriptions\" flag"));
}

void
ModifyTracksSubmenu::languageChangeTriggered() {
  auto action = dynamic_cast<QAction *>(sender());
  if (!action)
    return;

  auto language = action->data().toString();
  if (!language.isEmpty())
    Q_EMIT languageChangeRequested(language);
}

}
