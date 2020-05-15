/*
  mkvmerge -- utility for splicing together matroska files
  from component media subtypes

  Distributed under the GPL v2
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html

  helper functions for chapters on DVDs

  Written by Moritz Bunkus <moritz@bunkus.org>.
*/
#include "common/common_pch.h"

#if defined(HAVE_DVDREAD)

#include "common/timestamp.h"

namespace mtx::chapters {

std::vector<std::vector<timestamp_c>> parse_dvd(std::string const &file_name);
std::shared_ptr<libmatroska::KaxChapters> maybe_parse_dvd(std::string const &file_name, std::string const &language);

}

#endif  // HAVE_DVDREAD