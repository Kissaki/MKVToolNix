#pragma once

#include "common/common_pch.h"

#include <QDialog>

#include "mkvtoolnix-gui/merge/source_file.h"
#include "mkvtoolnix-gui/util/settings.h"

namespace mtx::gui::Merge {

class Tab;

namespace Ui {
class AddingAppendingFilesDialog;
}

class AddingAppendingFilesDialog : public QDialog {
  Q_OBJECT
protected:
  std::unique_ptr<Ui::AddingAppendingFilesDialog> ui;

public:
  enum Mode {
    DragAndDrop,
    AddSourceFiles,
  };

public:
  explicit AddingAppendingFilesDialog(QWidget *parent, Tab &tab, Mode mode);
  ~AddingAppendingFilesDialog();

  void setDefaults(Util::Settings::MergeAddingAppendingFilesPolicy decision, int fileNum, bool alwaysCreateNewSettingsForVideoFiles);

  Util::Settings::MergeAddingAppendingFilesPolicy decision() const;
  int fileNum() const;
  bool alwaysCreateNewSettingsForVideoFiles() const;
  bool alwaysUseThisDecision() const;

public Q_SLOTS:
  void selectionChanged();
};

}
