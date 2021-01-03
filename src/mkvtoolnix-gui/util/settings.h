#pragma once

#include "common/common_pch.h"

#include <QDateTime>
#include <QDir>
#include <QString>
#include <QTabWidget>
#include <QVariant>

#include "common/bcp47.h"
#include "common/translation.h"
#include "mkvtoolnix-gui/info/job_settings.h"
#include "mkvtoolnix-gui/merge/enums.h"
#include "mkvtoolnix-gui/util/recently_used_strings.h"

class QSettings;
class QSplitter;

namespace mtx::gui::Util {

class Settings: public QObject {
  Q_OBJECT
  Q_ENUMS(RunProgramForEvent)

public:
  enum RunProgramType {
    Min,
    ExecuteProgram,
    PlayAudioFile,
    ShutDownComputer,
    HibernateComputer,
    SleepComputer,
    DeleteSourceFiles,
    Max,
    Default = ExecuteProgram,
  };

  enum RunProgramForEvent {
    RunNever                         = 0x00,
    RunAfterJobQueueFinishes         = 0x01,
    RunAfterJobCompletesSuccessfully = 0x02,
    RunAfterJobCompletesWithErrors   = 0x04,
    RunAfterJobCompletesWithWarnings = 0x08,
  };

  Q_DECLARE_FLAGS(RunProgramForEvents, RunProgramForEvent)

  enum ProcessPriority {
    LowestPriority = 0,
    LowPriority,
    NormalPriority,
    HighPriority,
    HighestPriority,
  };

  enum ScanForPlaylistsPolicy {
    AskBeforeScanning = 0,
    AlwaysScan,
    NeverScan,
  };

  enum OutputFileNamePolicy {
    DontSetOutputFileName = 0,
    ToPreviousDirectory,
    ToFixedDirectory,
    ToParentOfFirstInputFile,
    ToSameAsFirstInputFile,
    ToRelativeOfFirstInputFile,
  };

  enum class SetDefaultLanguagePolicy {
    OnlyIfAbsent = 0,
    IfAbsentOrUndetermined,
  };

  enum class DeriveLanguageFromFileNamePolicy {
    Never = 0,
    OnlyIfAbsent,
    IfAbsentOrUndetermined,
  };

  enum class JobRemovalPolicy {
    Never,
    IfSuccessful,
    IfWarningsFound,
    Always,
  };

  enum class ClearMergeSettingsAction {
    None,
    NewSettings,
    RemoveInputFiles,
    CloseSettings,
  };

  enum class MergeAddingAppendingFilesPolicy {
    Ask,
    Add,
    AddToNew,
    AddEachToNew,
    Append,
    AddAdditionalParts,
  };

  enum class HeaderEditorDroppedFilesPolicy {
    Ask,
    Open,
    AddAttachments,
  };

  enum class TrackPropertiesLayout {
    HorizontalScrollArea,
    HorizontalTwoColumns,
    VerticalTabWidget,
  };

  enum class MergeMissingAudioTrackPolicy {
    Never,
    IfAudioTrackPresent,
    Always,
  };

  enum class BCP47LanguageEditingMode {
    FreeForm,
    Components,
  };

  class RunProgramConfig {
  public:
    RunProgramType m_type{RunProgramType::ExecuteProgram};
    bool m_active{true};
    QString m_name;
    RunProgramForEvents m_forEvents{};
    QStringList m_commandLine;
    QString m_audioFile;
    unsigned int m_volume{75};

    bool isValid() const;
    QString validate() const;
    QString name() const;

  private:
    QString nameForExternalProgram() const;
    QString nameForPlayAudioFile() const;
  };

  using RunProgramConfigPtr  = std::shared_ptr<RunProgramConfig>;
  using RunProgramConfigList = QList<RunProgramConfigPtr>;

  mtx::bcp47::language_c m_defaultAudioTrackLanguage, m_defaultVideoTrackLanguage, m_defaultSubtitleTrackLanguage, m_defaultChapterLanguage;
  SetDefaultLanguagePolicy m_whenToSetDefaultLanguage;
  DeriveLanguageFromFileNamePolicy m_deriveAudioTrackLanguageFromFileNamePolicy, m_deriveVideoTrackLanguageFromFileNamePolicy, m_deriveSubtitleTrackLanguageFromFileNamePolicy;
  QString m_regexForDerivingTrackLanguagesFromFileNames;
  QStringList m_recognizedTrackLanguagesInFileNames, m_mergePredefinedSplitSizes, m_mergePredefinedSplitDurations;
  QStringList m_mergePredefinedVideoTrackNames, m_mergePredefinedAudioTrackNames, m_mergePredefinedSubtitleTrackNames;
  QString m_chapterNameTemplate, m_defaultChapterCountry, m_ceTextFileCharacterSet, m_defaultSubtitleCharset, m_defaultAdditionalMergeOptions;
  QStringList m_oftenUsedLanguages, m_oftenUsedRegions, m_oftenUsedCharacterSets;
  bool m_oftenUsedLanguagesOnly, m_oftenUsedRegionsOnly, m_oftenUsedCharacterSetsOnly;
  ProcessPriority m_priority;
  double m_probeRangePercentage;
  QTabWidget::TabPosition m_tabPosition;
  bool m_elideTabHeaderLabels;
  QDir m_lastOpenDir, m_lastOutputDir, m_lastConfigDir;
  bool m_setAudioDelayFromFileName, m_autoSetFileTitle, m_autoClearFileTitle, m_disableCompressionForAllTrackTypes, m_disableDefaultTrackForSubtitles, m_mergeAlwaysShowOutputFileControls, m_dropLastChapterFromBlurayPlaylist;
  bool m_mergeEnableDialogNormGainRemoval, m_mergeAddBlurayCovers;
  ClearMergeSettingsAction m_clearMergeSettings;
  MergeAddingAppendingFilesPolicy m_mergeAddingAppendingFilesPolicy, m_mergeLastAddingAppendingDecision;
  HeaderEditorDroppedFilesPolicy m_headerEditorDroppedFilesPolicy;
  bool m_headerEditorDateTimeInUTC;
  TrackPropertiesLayout m_mergeTrackPropertiesLayout;
  MergeMissingAudioTrackPolicy m_mergeWarnMissingAudioTrack;
  RecentlyUsedStrings m_mergeLastRelativeOutputDirs{10}, m_mergeLastFixedOutputDirs{10}, m_mergeLastOutputDirs{10};

  OutputFileNamePolicy m_outputFileNamePolicy;
  bool m_autoDestinationOnlyForVideoFiles, m_mergeSetDestinationFromTitle;
  QDir m_relativeOutputDir, m_fixedOutputDir;
  bool m_uniqueOutputFileNames, m_autoClearOutputFileName;

  ScanForPlaylistsPolicy m_scanForPlaylistsPolicy;
  unsigned int m_minimumPlaylistDuration;

  JobRemovalPolicy m_jobRemovalPolicy, m_jobRemovalOnExitPolicy;
  unsigned int m_maximumConcurrentJobs;
  bool m_removeOldJobs;
  int m_removeOldJobsDays;
  bool m_useDefaultJobDescription, m_showOutputOfAllJobs, m_switchToJobOutputAfterStarting, m_resetJobWarningErrorCountersOnExit;
  bool m_removeOutputFileOnJobFailure;

  bool m_uiDisableHighDPIScaling, m_uiDisableDarkStyleSheet, m_uiDisableToolTips;
  bool m_checkForUpdates;
  QDateTime m_lastUpdateCheck;

  bool m_showToolSelector, m_warnBeforeClosingModifiedTabs, m_warnBeforeAbortingJobs, m_warnBeforeOverwriting, m_showMoveUpDownButtons;
  QString m_uiLocale, m_uiFontFamily;
  int m_uiFontPointSize;
  bool m_uiStayOnTop;

  bool m_enableMuxingTracksByLanguage, m_enableMuxingAllVideoTracks, m_enableMuxingAllAudioTracks, m_enableMuxingAllSubtitleTracks;
  QStringList m_enableMuxingTracksByTheseLanguages;
  QList<Merge::TrackType> m_enableMuxingTracksByTheseTypes;

  QHash<QString, QList<int> > m_splitterSizes;

  QString m_mediaInfoExe;

  Info::JobSettings m_defaultInfoJobSettings;
  RunProgramConfigList m_runProgramConfigurations;

  BCP47LanguageEditingMode m_bcp47LanguageEditingMode;

public:
  Settings();
  void load();
  void save() const;

  QString priorityAsString() const;
  QString actualMkvmergeExe() const;

  void setValue(QString const &group, QString const &key, QVariant const &value);
  QVariant value(QString const &group, QString const &key, QVariant const &defaultValue = QVariant{}) const;

  void handleSplitterSizes(QSplitter *splitter);
  void restoreSplitterSizes(QSplitter *splitter);

  QString localeToUse(QString const &requestedLocale = {}) const;

  QString lastOpenDirPath() const;
  QString lastConfigDirPath() const;

public Q_SLOTS:
  void storeSplitterSizes();

protected:
  void loadDefaults(QSettings &reg);
  void loadDerivingTrackLanguagesSettings(QSettings &reg);
  void loadSplitterSizes(QSettings &reg);
  void loadDefaultInfoJobSettings(QSettings &reg);
  void loadRunProgramConfigurations(QSettings &reg);

  void saveDefaults(QSettings &reg) const;
  void saveDerivingTrackLanguagesSettings(QSettings &reg) const;
  void saveSplitterSizes(QSettings &reg) const;
  void saveDefaultInfoJobSettings(QSettings &reg) const;
  void saveRunProgramConfigurations(QSettings &reg) const;

  void addDefaultRunProgramConfigurations(QSettings &reg);
  void addDefaultRunProgramConfigurationForType(QSettings &reg, RunProgramType type, std::function<void(RunProgramConfig &)> const &modifier = nullptr);
  bool fixDefaultAudioFileNameBug();

  void setDefaults(std::optional<QVariant> enableMuxingTracksByTheseTypes);

protected:
  static Settings s_settings;

  static void withGroup(QString const &group, std::function<void(QSettings &)> worker);

public:
  static Settings &get();
  static void change(std::function<void(Settings &)> worker);
  static std::unique_ptr<QSettings> registry();

  static void runOncePerVersion(QString const &topic, std::function<void()> worker);

  static QString exeWithPath(QString const &exe);
  static QString determineMediaInfoExePath();

  static void migrateFromRegistry();
  static void convertOldSettings();

  static QString iniFileLocation();
  static QString iniFileName();

  static QString cacheDirLocation(QString const &subDir);
  static QString prepareCacheDir(QString const &subDir);
};

// extern Settings g_settings;

}

Q_DECLARE_OPERATORS_FOR_FLAGS(mtx::gui::Util::Settings::RunProgramForEvents)
