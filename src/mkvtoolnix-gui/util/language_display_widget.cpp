#include "common/common_pch.h"

#include <QEvent>
#include <QPushButton>

#include "common/bcp47.h"
#include "common/iana_language_subtag_registry.h"
#include "common/iso639.h"
#include "common/iso3166.h"
#include "common/iso15924.h"
#include "common/qt.h"
#include "mkvtoolnix-gui/forms/util/language_display_widget.h"
#include "mkvtoolnix-gui/util/elide_label.h"
#include "mkvtoolnix-gui/util/language_dialog.h"
#include "mkvtoolnix-gui/util/language_display_widget.h"
#include "mkvtoolnix-gui/util/settings.h"

namespace mtx::gui::Util {

class LanguageDisplayWidgetPrivate {
  friend class LanguageDisplayWidget;

  QString additionalToolTip, clearTitle{QY("no language selected")};
  QStringList additionalLanguages;
  mtx::bcp47::language_c language;
  std::unique_ptr<Ui::LanguageDisplayWidget> ui{new Ui::LanguageDisplayWidget};
};

LanguageDisplayWidget::LanguageDisplayWidget(QWidget *parent)
  : QWidget{parent}
  , p_ptr{new LanguageDisplayWidgetPrivate}
{
  auto &p = *p_ptr;

  p.ui->setupUi(this);
  p.ui->lLanguage->setElideMode(Qt::ElideRight);
  p.ui->pbClear->setVisible(false);

  updateDisplay();

  p.ui->lLanguage->installEventFilter(this);

  connect(p.ui->pbClear, &QPushButton::clicked, this, &LanguageDisplayWidget::clearLanguage);
  connect(p.ui->pbEdit,  &QPushButton::clicked, this, &LanguageDisplayWidget::editLanguage);
}

LanguageDisplayWidget::~LanguageDisplayWidget() {
}

void
LanguageDisplayWidget::retranslateUi() {
  p_func()->ui->retranslateUi(this);
  updateDisplay();
}

void
LanguageDisplayWidget::enableClearingLanguage(bool enable) {
  auto &p = *p_func();

  p.ui->pbClear->setVisible(enable);
}

void
LanguageDisplayWidget::editLanguage() {
  auto &p = *p_func();

  LanguageDialog dlg{this};

  if (p.language.is_valid())
    dlg.setLanguage(p.language);

  dlg.setAdditionalLanguages(p.additionalLanguages);

  if (!dlg.exec())
    return;

  p.language = dlg.language();

  updateDisplay();

  Q_EMIT languageChanged(p.language);
}

void
LanguageDisplayWidget::clearLanguage() {
  auto &p    = *p_func();
  p.language = mtx::bcp47::language_c{};

  updateDisplay();

  Q_EMIT languageChanged(p.language);
}

void
LanguageDisplayWidget::setLanguage(mtx::bcp47::language_c const &language) {
  p_func()->language = language;

  updateDisplay();
}

void
LanguageDisplayWidget::setClearTitle(QString const &clearTitle) {
  p_func()->clearTitle = clearTitle;

  updateDisplay();
}

void
LanguageDisplayWidget::setAdditionalToolTip(QString const &additionalToolTip) {
  p_func()->additionalToolTip = additionalToolTip.isEmpty() || additionalToolTip.startsWith('<') ? additionalToolTip : Q("<span>%1</span>").arg(additionalToolTip.toHtmlEscaped());

  updateDisplay();
}

void
LanguageDisplayWidget::setAdditionalLanguages(QStringList const &additionalLanguages) {
  p_func()->additionalLanguages = additionalLanguages;
}

void
LanguageDisplayWidget::setAdditionalLanguages(QString const &additionalLanguage) {
  p_func()->additionalLanguages = QStringList{additionalLanguage};
}

mtx::bcp47::language_c
LanguageDisplayWidget::language()
  const {
  return p_func()->language;
}

void
LanguageDisplayWidget::updateDisplay() {
  auto &p = *p_func();

  auto text = p.language.is_valid() ? Q(p.language.format_long()) : p.clearTitle;
  p.ui->lLanguage->setText(text);

  updateToolTip();
}

void
LanguageDisplayWidget::updateToolTip() {
  auto &p = *p_func();

  if (Util::Settings::get().m_uiDisableToolTips) {
    p.ui->lLanguage->setToolTip({});
    return;
  }

  QStringList content;

  if (!p.additionalToolTip.isEmpty())
    content << p.additionalToolTip;

  if (!p.language.is_valid()) {
    p.ui->lLanguage->setToolTip(content.join(QString{}));
    return;
  }

  if (!content.isEmpty())
    content << Q("<hr>");


  auto language     = Q("—"),
    extendedSubtags = Q("—"),
    script          = Q("—"),
    region          = Q("—"),
    variants        = Q("—"),
    privateUses     = Q("—");

  if (!p.language.get_language().empty()) {
    auto languageOpt = mtx::iso639::look_up(p.language.get_language());
    language         = languageOpt ? Q("%1 (%2)").arg(Q(languageOpt->english_name)).arg(Q(p.language.get_language())) : Q(p.language.get_language());
  }

  if (!p.language.get_script().empty()) {
    auto scriptOpt = mtx::iso15924::look_up(p.language.get_script());
    script         = scriptOpt ? Q("%1 (%2)").arg(Q(scriptOpt->english_name)).arg(Q(scriptOpt->code)) : Q(p.language.get_script());
  }

  if (!p.language.get_region().empty()) {
    auto regionOpt = mtx::iso3166::look_up(p.language.get_region());
    region         = regionOpt ? Q("%1 (%2)").arg(Q(regionOpt->name)).arg(Q(p.language.get_region())) : Q(p.language.get_region());
  }

  QStringList entries;
  for (auto const &subtag : p.language.get_extended_language_subtags()) {
    auto subtagOpt = mtx::iana::language_subtag_registry::look_up_extlang(subtag);
    entries << (subtagOpt ? Q("%1 (%2)").arg(Q(subtagOpt->description)).arg(Q(subtag)) : Q(subtag));
  }

  if (!entries.empty())
    extendedSubtags = entries.join(Q(", "));

  entries.clear();
  for (auto const &variant : p.language.get_variants()) {
    auto variantOpt = mtx::iana::language_subtag_registry::look_up_variant(variant);
    entries << (variantOpt ? Q("%1 (%2)").arg(Q(variantOpt->description)).arg(Q(variant)) : Q(variant));
  }

  if (!entries.empty())
    variants = entries.join(Q(", "));

  entries.clear();
  for (auto const &privateUse : p.language.get_private_use())
    entries << Q(privateUse);

  if (!entries.empty())
    privateUses = entries.join(Q(", "));

  content << Q("<b>%1:</b>").arg(QY("BCP 47 language tag details").toHtmlEscaped())
          << Q("<table>")
          << Q("<tr><td>%1:</td><td>%2</td></tr>") .arg(QY("Language"))        .arg(language       .toHtmlEscaped())
          << Q("<tr><td>%1:</td><td>%2</td></tr>") .arg(QY("Extended subtags")).arg(extendedSubtags.toHtmlEscaped())
          << Q("<tr><td>%1:</td><td>%2</td></tr>") .arg(QY("Script"))          .arg(script         .toHtmlEscaped())
          << Q("<tr><td>%1:</td><td>%2</td></tr>") .arg(QY("Region"))          .arg(region         .toHtmlEscaped())
          << Q("<tr><td>%1:</td><td>%2</td></tr>") .arg(QY("Variants"))        .arg(variants       .toHtmlEscaped())
          << Q("<tr><td>%1:</td><td>%2</td></tr>") .arg(QY("Private use"))     .arg(privateUses    .toHtmlEscaped())
          << Q("</table>");

  p.ui->lLanguage->setToolTip(content.join(QString{}));
}

bool
LanguageDisplayWidget::eventFilter(QObject *obj,
                                   QEvent *ev) {
  auto &p = *p_func();

  if ((obj == p.ui->lLanguage) && (ev->type() == QEvent::MouseButtonRelease)) {
    if (p.ui->lLanguage->isEnabled())
      editLanguage();
    return true;
  }

  return QWidget::eventFilter(obj, ev);
}

}
