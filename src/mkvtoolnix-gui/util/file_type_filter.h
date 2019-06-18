#pragma once

#include "common/common_pch.h"

#include <QStringList>

namespace mtx { namespace gui { namespace Util {

class FileTypeFilter {
public:
  static QStringList const &get();
  static void reset();

public:
  static QStringList s_filter;
};

}}}
