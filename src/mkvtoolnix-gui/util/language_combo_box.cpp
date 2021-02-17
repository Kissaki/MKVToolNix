#include "common/common_pch.h"

#include <QAbstractItemView>
#include <QStringList>

#include "common/qt.h"
#include "mkvtoolnix-gui/app.h"
#include "mkvtoolnix-gui/util/language_combo_box.h"
#include "mkvtoolnix-gui/util/settings.h"
#include "mkvtoolnix-gui/util/widget.h"

namespace mtx::gui::Util {

LanguageComboBox::LanguageComboBox(QWidget *parent)
  : ComboBoxBase{parent}
{
}

LanguageComboBox::~LanguageComboBox() {
}

LanguageComboBox::LanguageComboBox(ComboBoxBasePrivate &d,
                                   QWidget *parent)
  : ComboBoxBase{d, parent}
{
}

bool
LanguageComboBox::onlyShowOftenUsed()
  const {
  auto &cfg = Util::Settings::get();
  return cfg.m_oftenUsedLanguagesOnly && !cfg.m_oftenUsedLanguages.isEmpty();
}

ComboBoxBase &
LanguageComboBox::setup(bool withEmpty,
                        QString const &emptyTitle) {
  auto onlyOftenUsed = onlyShowOftenUsed();

  ComboBoxBase::setup(withEmpty, emptyTitle);

  auto separatorOffset = 0;

  if (withEmpty) {
    addItem(emptyTitle, Q(""));
    ++separatorOffset;
  }

  auto commonLanguages = onlyOftenUsed ? mergeCommonAndAdditionalItems(App::commonIso639Languages(), App::iso639Languages(), additionalItems()) : App::commonIso639Languages();
  auto undIsCommon     = std::find_if(commonLanguages.begin(), commonLanguages.end(), [](auto const &language) { return language.second == Q("und"); }) != commonLanguages.end();

  if (!onlyOftenUsed && !undIsCommon) {
    for (auto const &language : App::iso639Languages()) {
      if (language.second != Q("und"))
        continue;

      addItem(language.first, language.second);
      insertSeparator(1 + separatorOffset);
      separatorOffset += 2;
      break;
    }

  }

  if (!commonLanguages.empty()) {
    for (auto const &language : commonLanguages)
      addItem(language.first, language.second);

    if (!onlyOftenUsed)
      insertSeparator(commonLanguages.size() + separatorOffset);
  }

  if (!onlyOftenUsed) {
    auto &languages = Util::Settings::get().m_useISO639_3Languages ? App::iso639Languages() : App::iso639_2Languages();
    for (auto const &language : languages)
      addItem(language.first, language.second);
  }

  view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  Util::fixComboBoxViewWidth(*this);

  return *this;
}

}
