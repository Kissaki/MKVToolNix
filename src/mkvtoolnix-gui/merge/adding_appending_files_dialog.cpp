#include "common/common_pch.h"

#include <QFileInfo>
#include <QPushButton>

#include "common/qt.h"
#include "mkvtoolnix-gui/forms/merge/adding_appending_files_dialog.h"
#include "mkvtoolnix-gui/merge/adding_appending_files_dialog.h"
#include "mkvtoolnix-gui/merge/tab.h"

namespace mtx::gui::Merge {

using namespace mtx::gui;

AddingAppendingFilesDialog::AddingAppendingFilesDialog(QWidget *parent,
                                                       Tab &tab,
                                                       Mode mode)
  : QDialog{parent}
  , ui{new Ui::AddingAppendingFilesDialog}
{
  // Setup UI controls.
  ui->setupUi(this);

  ui->rbAdd->setChecked(true);
  ui->cbFileName->setEnabled(false);

  if (mode == AddSourceFiles) {
    setWindowTitle(QY("Adding multiple files via 'Add source files'"));
    ui->lTitle->setText(QY("Adding multiple files via 'Add source files'"));
    ui->lQuestion->setText(QY("What do you want to do with the files?"));
  }

  for (auto const &file : tab.sourceFiles()) {
    auto info = QFileInfo{file->m_fileName};
    ui->cbFileName->addItem(Q("%1 (%2)").arg(info.fileName()).arg(info.path()));
  }

  if (tab.sourceFiles().isEmpty()) {
    ui->lAppendTo->setEnabled(false);
    ui->rbAppend->setEnabled(false);
    ui->rbAddAdditionalParts->setEnabled(false);
  }

  adjustSize();

  connect(ui->buttonBox,            &QDialogButtonBox::accepted, this, &AddingAppendingFilesDialog::accept);
  connect(ui->buttonBox,            &QDialogButtonBox::rejected, this, &AddingAppendingFilesDialog::reject);
  connect(ui->rbAdd,                &QPushButton::clicked,       this, &AddingAppendingFilesDialog::selectionChanged);
  connect(ui->rbAddAdditionalParts, &QPushButton::clicked,       this, &AddingAppendingFilesDialog::selectionChanged);
  connect(ui->rbAddEachToNew,       &QPushButton::clicked,       this, &AddingAppendingFilesDialog::selectionChanged);
  connect(ui->rbAddToNew,           &QPushButton::clicked,       this, &AddingAppendingFilesDialog::selectionChanged);
  connect(ui->rbAppend,             &QPushButton::clicked,       this, &AddingAppendingFilesDialog::selectionChanged);
}

AddingAppendingFilesDialog::~AddingAppendingFilesDialog() {
}

void
AddingAppendingFilesDialog::setDefaults(Util::Settings::MergeAddingAppendingFilesPolicy decision,
                                        int fileNum,
                                        bool alwaysCreateNewSettingsForVideoFiles) {
  auto ctrl = decision == Util::Settings::MergeAddingAppendingFilesPolicy::Add          ? ui->rbAdd
            : decision == Util::Settings::MergeAddingAppendingFilesPolicy::Append       ? ui->rbAppend
            : decision == Util::Settings::MergeAddingAppendingFilesPolicy::AddToNew     ? ui->rbAddToNew
            : decision == Util::Settings::MergeAddingAppendingFilesPolicy::AddEachToNew ? ui->rbAddEachToNew
            :                                                                             ui->rbAddAdditionalParts;

  ctrl->setChecked(true);
  ctrl->setFocus();

  if (fileNum <= ui->cbFileName->count())
    ui->cbFileName->setCurrentIndex(fileNum);

  ui->cbAlwaysCreateNewSettingsForVideoFiles->setChecked(alwaysCreateNewSettingsForVideoFiles);

  selectionChanged();
}

Util::Settings::MergeAddingAppendingFilesPolicy
AddingAppendingFilesDialog::decision()
  const {
  return ui->rbAdd->isChecked()          ? Util::Settings::MergeAddingAppendingFilesPolicy::Add
       : ui->rbAppend->isChecked()       ? Util::Settings::MergeAddingAppendingFilesPolicy::Append
       : ui->rbAddToNew->isChecked()     ? Util::Settings::MergeAddingAppendingFilesPolicy::AddToNew
       : ui->rbAddEachToNew->isChecked() ? Util::Settings::MergeAddingAppendingFilesPolicy::AddEachToNew
       :                                   Util::Settings::MergeAddingAppendingFilesPolicy::AddAdditionalParts;
}

int
AddingAppendingFilesDialog::fileNum()
  const {
  return ui->cbFileName->currentIndex();
}

void
AddingAppendingFilesDialog::selectionChanged() {
  ui->cbFileName->setEnabled(ui->rbAppend->isChecked() || ui->rbAddAdditionalParts->isChecked());
  ui->cbAlwaysUseThisDecision->setEnabled(ui->rbAdd->isChecked() || ui->rbAddToNew->isChecked() || ui->rbAddEachToNew->isChecked());
}

bool
AddingAppendingFilesDialog::alwaysCreateNewSettingsForVideoFiles()
  const {
  return ui->cbAlwaysCreateNewSettingsForVideoFiles->isEnabled() && ui->cbAlwaysCreateNewSettingsForVideoFiles->isChecked();
}

bool
AddingAppendingFilesDialog::alwaysUseThisDecision()
  const {
  return ui->cbAlwaysUseThisDecision->isEnabled() && ui->cbAlwaysUseThisDecision->isChecked();
}

}
