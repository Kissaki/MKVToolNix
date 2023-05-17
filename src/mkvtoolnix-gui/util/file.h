#pragma once

#include "common/common_pch.h"

class QString;
class QUrl;

namespace mtx::gui::Util {

struct BomAsciiCheckResult {
  byte_order_mark_e byteOrderMark{byte_order_mark_e::none};
  unsigned int bomLength{};
  bool containsNonAscii{};
};

BomAsciiCheckResult checkForBomAndNonAscii(QString const &fileName);

QUrl pathToFileUrl(QString const &path);

QString removeInvalidPathCharacters(QString fileName);
QString replaceInvalidFileNameCharacters(QString fileName);

QStringList replaceDirectoriesByContainedFiles(QStringList const &namesToCheck);

QString detectMIMEType(QString const &fileName);

void saveTextToFile(QString const &fileName, QString const &content, bool saveLastOpenDir = false);
void saveTextToFile(QString const &fileName, QStringList const &content, bool saveLastOpenDir = false);

}
