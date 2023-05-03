#pragma once

#include "common/common_pch.h"

#include <QDialog>

#include "common/bcp47.h"

class QComboBox;
class QLineEdit;
class QRegularExpression;

namespace mtx::bcp47 {
class language_c;
}

namespace mtx::gui::Util {

namespace Ui {
class LanguageDialog;
}

class LanguageDialogPrivate;
class LanguageDialog : public QDialog {
  Q_OBJECT

protected:
  MTX_DECLARE_PRIVATE(LanguageDialogPrivate)

  std::unique_ptr<LanguageDialogPrivate> const p_ptr;

  explicit LanguageDialog(LanguageDialogPrivate &p);

public:
  explicit LanguageDialog(QWidget *parent);
  ~LanguageDialog();

  void setAdditionalLanguages(QStringList const &additionalLanguages);

  void setLanguage(mtx::bcp47::language_c const &language);
  mtx::bcp47::language_c language() const;

  void reset();

  void retranslateUi();

public Q_SLOTS:
  void updateFromFreeForm(bool force = false);
  void updateFromComponents();

  void addVariantRowAndUpdateLayout();
  void addExtensionRowAndUpdateLayout();
  void addPrivateUseRowAndUpdateLayout();

  void removeRowForClickedButton();

  void enableNormalizeActions(mtx::bcp47::language_c const &currentLanguage);

  void replaceWithNormalizedForm(mtx::bcp47::normalization_mode_e mode, bool always);

  void saveDialogGeometry();

  virtual int exec() override;

Q_SIGNALS:
  void tagValidityChanged(bool isValid);

protected:
  void setupConnections();
  void setupReplaceNormalizedMenu();
  void setupFreeFormAndComponentControls();
  void connectComponentWidgetChange(QWidget *widget);
  void decorateReplaceMenuEntries();
  void changeNormalizationMode(mtx::bcp47::normalization_mode_e mode);

  void setupExtendedSubtagComboBox();
  void setupScriptComboBox();
  void setupVariantComboBox(QComboBox &comboBox);

  void reinitializeLanguageComboBox();

  QVector<QWidget *> allComponentWidgets();
  QVector<QWidget *> allComponentWidgetsMatchingName(QRegularExpression const &matcher);

  mtx::bcp47::language_c languageTagFromComponents();
  void setComponentsFromLanguageTag(mtx::bcp47::language_c const &tag, bool force);

  void setStatusFromLanguageTag(mtx::bcp47::language_c const &tag);
  void setMultipleWidgetsTexts(QString const &objectNamePrefix, std::vector<std::string> const &values);
  void setWidgetText(QWidget &widget, QString const &text);

  QWidget *addRowItem(QString const &type, std::function<QWidget *(int)> const &createWidget);
  QWidget *addVariantRow();
  QWidget *addExtensionRow();
  QWidget *addPrivateUseRow();

  int componentGridRowForWidget(QWidget *widget);
  void removeRowItems(QString const &namePrefix);
  void createInitialComponentWidgetList();
  void createGridLayoutFromComponentWidgetList();

  std::pair<QStringList, QStringList> determineInfoAndWarningsFor(mtx::bcp47::language_c const &tag);
};

}
