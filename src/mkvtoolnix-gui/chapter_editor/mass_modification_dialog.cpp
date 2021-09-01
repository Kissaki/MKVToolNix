#include "common/common_pch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>

#include "common/qt.h"
#include "common/strings/parsing.h"
#include "mkvtoolnix-gui/forms/chapter_editor/mass_modification_dialog.h"
#include "mkvtoolnix-gui/chapter_editor/mass_modification_dialog.h"
#include "mkvtoolnix-gui/main_window/main_window.h"
#include "mkvtoolnix-gui/util/widget.h"

namespace mtx::gui::ChapterEditor {

using namespace mtx::gui;

MassModificationDialog::MassModificationDialog(QWidget *parent,
                                               bool editionOrChapterSelected,
                                               QStringList const &additionalLanguages)
  : QDialog{parent}
  , m_ui{new Ui::MassModificationDialog}
  , m_editionOrChapterSelected{editionOrChapterSelected}
{
  setupUi(additionalLanguages);
  retranslateUi();
}

MassModificationDialog::~MassModificationDialog() {
}

void
MassModificationDialog::setupUi(QStringList const &additionalLanguages) {
  m_ui->setupUi(this);

  m_ui->ldwLanguage->setAdditionalLanguages(additionalLanguages);

  connect(m_ui->cbShift,               &QCheckBox::toggled,                                                          this,                        &MassModificationDialog::verifyOptions);
  connect(m_ui->leShiftBy,             &QLineEdit::textChanged,                                                      this,                        &MassModificationDialog::verifyOptions);
  connect(m_ui->cbMultiply,            &QCheckBox::toggled,                                                          this,                        &MassModificationDialog::verifyOptions);
  connect(m_ui->dsbMultiplyBy,         static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,                        &MassModificationDialog::verifyOptions);
  connect(m_ui->cbSetLanguage,         &QCheckBox::toggled,                                                          m_ui->ldwLanguage,           &QComboBox::setEnabled);
  connect(m_ui->cbConstrictExpand,     &QCheckBox::toggled,                                                          m_ui->rbConstrict,           &QRadioButton::setEnabled);
  connect(m_ui->cbConstrictExpand,     &QCheckBox::toggled,                                                          m_ui->rbExpand,              &QRadioButton::setEnabled);
  connect(m_ui->cbSetEndTimestamps,    &QCheckBox::toggled,                                                          m_ui->cbRemoveEndTimestamps, &QRadioButton::setDisabled);
  connect(m_ui->cbRemoveEndTimestamps, &QCheckBox::toggled,                                                          m_ui->cbSetEndTimestamps,    &QRadioButton::setDisabled);
  connect(m_ui->buttonBox,             &QDialogButtonBox::accepted,                                                  this,                        &MassModificationDialog::accept);
  connect(m_ui->buttonBox,             &QDialogButtonBox::rejected,                                                  this,                        &MassModificationDialog::reject);

  m_ui->leShiftBy->setEnabled(false);
  m_ui->dsbMultiplyBy->setEnabled(false);
  m_ui->ldwLanguage->setEnabled(false);
  m_ui->rbConstrict->setEnabled(false);
  m_ui->rbExpand->setEnabled(false);
  m_ui->rbConstrict->setChecked(true);

  m_ui->cbShift->setFocus();
}

void
MassModificationDialog::retranslateUi() {
  Util::setToolTip(m_ui->leShiftBy,
                   Q("%1 %2 %3")
                   .arg(QY("The format is either the form 'HH:MM:SS.nnnnnnnnn' or a number followed by one of the units 's', 'ms' or 'us'."))
                   .arg(QY("Negative values are allowed."))
                   .arg(QY("If both shifting and multiplication are enabled then the shift will be performed before the multiplication.")));

  Util::setToolTip(m_ui->dsbMultiplyBy, QY("If both shifting and multiplication are enabled then the shift will be performed before the multiplication."));

  Util::setToolTip(m_ui->cbSetEndTimestamps,
                   Q("<p>%1 %2</p><p>%3</p>")
                   .arg(QY("For most entries the smallest start timestamp of all chapters on the same level higher than the current chapter's start timestamp will be used as its end timestamp."))
                   .arg(QY("If there is no such chapter, the parent chapter's end timestamp will be used instead."))
                   .arg(QY("If the chapters were loaded from a Matroska file, the end timestamp for very last chapter on the top-most level will be derived from the file's duration.")));

  if (m_editionOrChapterSelected)
    m_ui->lTitle->setText(QY("Please select the actions to apply to the selected edition or chapter and all of its children."));
  else
    m_ui->lTitle->setText(QY("Please select the actions to apply to all editions, chapters and sub-chapters."));
}

MassModificationDialog::Actions
MassModificationDialog::actions()
  const {
  auto result = Actions{};

  if (m_ui->cbShift->isChecked())                                                           result |= Action::Shift;
  if (m_ui->cbSort->isChecked())                                                            result |= Action::Sort;
  if (m_ui->cbConstrictExpand->isChecked() && m_ui->rbConstrict->isChecked())               result |= Action::Constrict;
  if (m_ui->cbConstrictExpand->isChecked() && m_ui->rbExpand->isChecked())                  result |= Action::Expand;
  if (m_ui->cbSetLanguage->isChecked())                                                     result |= Action::SetLanguage;
  if (m_ui->cbMultiply->isChecked())                                                        result |= Action::Multiply;
  if (m_ui->cbSetEndTimestamps->isEnabled() && m_ui->cbSetEndTimestamps->isChecked())       result |= Action::SetEndTimestamps;
  if (m_ui->cbRemoveEndTimestamps->isEnabled() && m_ui->cbRemoveEndTimestamps->isChecked()) result |= Action::RemoveEndTimestamps;
  if (m_ui->cbRemoveNames->isChecked())                                                     result |= Action::RemoveNames;

  return result;
}

double
MassModificationDialog::multiplyBy()
  const {
  return m_ui->dsbMultiplyBy->value();
}

int64_t
MassModificationDialog::shiftBy()
  const {
  auto timestamp = int64_t{};
  mtx::string::parse_timestamp(to_utf8(m_ui->leShiftBy->text()), timestamp, true);
  return timestamp;
}

mtx::bcp47::language_c
MassModificationDialog::language()
  const {
  return m_ui->ldwLanguage->language();
}

bool
MassModificationDialog::isMultiplyByValid()
  const {
  if (!m_ui->cbMultiply->isChecked())
    return true;

  return m_ui->dsbMultiplyBy->value() != 0;
}

bool
MassModificationDialog::isShiftByValid()
  const {
  if (!m_ui->cbShift->isChecked())
    return true;

  auto timestamp = int64_t{};
  return mtx::string::parse_timestamp(to_utf8(m_ui->leShiftBy->text()), timestamp, true);
}

void
MassModificationDialog::verifyOptions() {
  m_ui->leShiftBy->setEnabled(m_ui->cbShift->isChecked());
  m_ui->dsbMultiplyBy->setEnabled(m_ui->cbMultiply->isChecked());

  Util::buttonForRole(m_ui->buttonBox)->setEnabled(isMultiplyByValid() && isShiftByValid());
}

}
