/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   definition of functions for converting between Vorbis comments and Matroska tags

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/base64.h"
#include "common/debugging.h"
#include "common/flac.h"
#include "common/iso639.h"
#include "common/mime.h"
#include "common/mm_io_x.h"
#include "common/mm_mem_io.h"
#include "common/strings/editing.h"
#include "common/strings/formatting.h"
#include "common/strings/parsing.h"
#include "common/tags/target_type.h"
#include "common/tags/vorbis.h"

#include <matroska/KaxTags.h>
#include <matroska/KaxTag.h>

namespace mtx::tags {

namespace {
debugging_option_c s_debug{"vorbis_comments|vorbis_tags"};

std::unordered_map<std::string, std::pair<std::string, int>> s_vorbis_to_matroska;
// std::unordered_map<int, std::unordered_map<std::string, std::string>> s_matroska_to_vorbis;

void
init_vorbis_to_matroska() {
  if (!s_vorbis_to_matroska.empty())
    return;

  // Official "minimal list of standard field names" from the Vorbis
  // comment spec at https://www.xiph.org/vorbis/doc/v-comment.html
  s_vorbis_to_matroska["ALBUM"s]                 = { "ALBUM"s,              Track };
  s_vorbis_to_matroska["COPYRIGHT"s]             = { "COPYRIGHT"s,          Track };
  s_vorbis_to_matroska["COPYRIGHT"s]             = { "COPYRIGHT"s,          Track };
  s_vorbis_to_matroska["DATE"s]                  = { "DATE_RECORDED"s,      Track };
  s_vorbis_to_matroska["DESCRIPTION"s]           = { "DESCRIPTION"s,        Track };
  s_vorbis_to_matroska["GENRE"s]                 = { "GENRE"s,              Track };
  s_vorbis_to_matroska["ISRC"s]                  = { "ISRC"s,               Track };
  s_vorbis_to_matroska["LICENSE"s]               = { "LICENSE"s,            Track };
  s_vorbis_to_matroska["LOCATION"s]              = { "RECORDING_LOCATION"s, Track };
  s_vorbis_to_matroska["ORGANIZATION"s]          = { "LABEL"s,              Track };
  s_vorbis_to_matroska["PERFORMER"s]             = { "LEAD_PERFORMER"s,     Track };
  s_vorbis_to_matroska["TRACKNUMBER"s]           = { "PART_NUMBER"s,        Track };
  // CONTACT ?
  // TITLE handled elsewhere
  // VERSION ?

  // Other commonly found field names
  s_vorbis_to_matroska["ACTOR"s]                 = { "ACTOR"s,              Track };
  s_vorbis_to_matroska["ALBUMARTIST"s]           = { "ARTIST"s,             Album };
  s_vorbis_to_matroska["ARTIST"s]                = { "ARTIST"s,             Track };
  s_vorbis_to_matroska["COMMENT"s]               = { "COMMENT"s,            Track };
  s_vorbis_to_matroska["COMPOSER"s]              = { "COMPOSER"s,           Track };
  s_vorbis_to_matroska["DIRECTOR"s]              = { "DIRECTOR"s,           Track };
  s_vorbis_to_matroska["ENCODED_BY"s]            = { "ENCODED_BY"s,         Track };
  s_vorbis_to_matroska["ENCODED_USING"s]         = { "ENCODED_USING"s,      Track };
  s_vorbis_to_matroska["ENCODER"s]               = { "ENCODER"s,            Track };
  s_vorbis_to_matroska["ENCODER_OPTIONS"s]       = { "ENCODER_OPTIONS"s,    Track };
  s_vorbis_to_matroska["PRODUCER"s]              = { "PRODUCER"s,           Track };
  s_vorbis_to_matroska["REPLAYGAIN_ALBUM_GAIN"s] = { "REPLAYGAIN_GAIN"s,    Album };
  s_vorbis_to_matroska["REPLAYGAIN_ALBUM_PEAK"s] = { "REPLAYGAIN_PEAK"s,    Album };
  s_vorbis_to_matroska["REPLAYGAIN_TRACK_GAIN"s] = { "REPLAYGAIN_GAIN"s,    Track };
  s_vorbis_to_matroska["REPLAYGAIN_TRACK_PEAK"s] = { "REPLAYGAIN_PEAK"s,    Track };
  // COVERART
  // COVERARTMIME
}

// void
// init_matroska_to_vorbis() {
//   if (!s_matroska_to_vorbis.empty())
//     return;

//   init_vorbis_to_matroska();

//   for (auto const &[vorbis, matroska] : s_vorbis_to_matroska)
//     s_matroska_to_vorbis[matroska.second][matroska.first] = vorbis;
// }

std::string
parse_language(std::string key_language) {
  auto index = map_to_iso639_2_code(key_language, true);
  if (-1 != index)
    return g_iso639_languages[index].iso639_2_code;

  boost::smatch matches;
  if (   boost::regex_search(key_language, matches, boost::regex{".*\\[(.+?)\\]", boost::regex::perl})
      && ((index = map_to_iso639_2_code(boost::to_lower_copy(matches[1].str()), true)) != -1))
    return g_iso639_languages[index].iso639_2_code;

  if (   boost::regex_search(key_language, matches, boost::regex{".*\\((.+?)\\)", boost::regex::perl})
      && ((index = map_to_iso639_2_code(boost::to_lower_copy(matches[1].str()), true)) != -1))
    return g_iso639_languages[index].iso639_2_code;

  return {};
}

std::string
file_base_name_for_picture_type([[maybe_unused]] unsigned int type) {
#if defined(HAVE_FLAC_FORMAT_H)
  return mtx::flac::file_base_name_for_picture_type(type);
#else
  return "cover"s;
#endif
}

std::shared_ptr<attachment_t>
parse_metadata_block_picture(vorbis_comments_t::type_e comments_type,
                             std::string const &value) {
  if (vorbis_comments_t::type_e::Unknown == comments_type)
    return {};

  try {
    auto data = mtx::base64::decode(value);
    mm_mem_io_c in{*data};

    auto picture      = std::make_shared<attachment_t>();
    auto picture_type = in.read_uint32_be();

    in.read(picture->mime_type,   in.read_uint32_be());
    in.read(picture->description, in.read_uint32_be());

    in.skip(4 * 4);             // width, height, color depth, number of colors used

    picture->data  = in.read(in.read_uint32_be());
    auto extension = mtx::mime::primary_file_extension_for_type(picture->mime_type);
    picture->name  = fmt::format("{}.{}", file_base_name_for_picture_type(picture_type), extension);

    mxdebug_if(s_debug, fmt::format("parse_metadata_block_picture: MIME type: {} name: {} data size: {}\n", picture->mime_type, picture->name, picture->data ? picture->data->get_size() : 0));

    if (!picture->mime_type.empty() && !extension.empty() && picture->data && picture->data->get_size())
      return picture;

  } catch (mtx::base64::exception const &ex) {
    mxdebug_if(s_debug, fmt::format("parse_metadata_block_picture: Base64 exception: {}\n", ex.what()));

  } catch (mtx::mm_io::exception const &ex) {
    mxdebug_if(s_debug, fmt::format("parse_metadata_block_picture: I/O exception: {}\n", ex.what()));
  }

  return {};
}

} // anonymous namespace

converted_vorbis_comments_t
from_vorbis_comments(vorbis_comments_t const &vorbis_comments) {
  init_vorbis_to_matroska();

  converted_vorbis_comments_t converted;
  converted.m_tags = std::make_shared<libmatroska::KaxTags>();

  for (auto const &[vorbis_full_key, value] : vorbis_comments.m_comments) {
    mxdebug_if(s_debug, fmt::format("from_vorbis_comments: parsing {}={}\n", vorbis_full_key, elide_string(value, 40)));

    auto key_name_language                  = split(vorbis_full_key, "-", 2);
    auto vorbis_key                         = balg::to_upper_copy(key_name_language[0]);
    auto const &[matroska_key, target_type] = s_vorbis_to_matroska[vorbis_key];

    if (vorbis_key == "TITLE") {
      converted.m_title = value;
      continue;
    }

    if (vorbis_key == "LANGUAGE") {
      auto new_language = parse_language(value);
      if (!new_language.empty())
        converted.m_language = new_language;
      continue;
    }

    if (vorbis_key == "METADATA_BLOCK_PICTURE") {
      auto picture = parse_metadata_block_picture(vorbis_comments.m_type, value);
      if (picture)
        converted.m_pictures.push_back(picture);
      continue;
    }

    if (matroska_key.empty())
      continue;


  }

  return converted;
}

vorbis_comments_t
parse_vorbis_comments_from_packet(memory_cptr const &packet) {
  try {
    mm_mem_io_c in{*packet};
    vorbis_comments_t comments;

    auto header = in.read(8)->to_string();
    unsigned int offset{};

    if (header == "OpusTags") {
      comments.m_type = vorbis_comments_t::type_e::Opus;
      offset          = 8;

    } else if (boost::regex_search(header, boost::regex{"^.vorbis", boost::regex::perl})) {
      comments.m_type = vorbis_comments_t::type_e::Vorbis;
      offset          = 7;

    } else if (boost::regex_search(header, boost::regex{"^OVP\\d{2}", boost::regex::perl})) {
      comments.m_type = vorbis_comments_t::type_e::VP_8_9;
      offset          = 7;

    } else
      return {};

    in.setFilePointer(offset);
    auto vendor_length = in.read_uint32_le();
    comments.m_vendor  = in.read(vendor_length)->to_string();

    auto num_comments  = in.read_uint32_le();

    comments.m_comments.reserve(num_comments);

    while (num_comments > 0) {
      --num_comments;

      auto comment_length = in.read_uint32_le();
      std::string line;

      in.read(line, comment_length);

      auto parts = split(line, "=", 2);
      if (parts.size() == 2)
        comments.m_comments.emplace_back(parts[0], parts[1]);
    }

    return comments;

  } catch(...) {
  }

  return {};
}

}