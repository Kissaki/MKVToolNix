/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   helper function for WebVTT data

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/regex.h"
#include "common/strings/formatting.h"
#include "common/strings/parsing.h"
#include "common/webvtt.h"

namespace mtx::webvtt {

constexpr auto RE_TIMESTAMP = "((?:\\d{2}:)?\\d{2}:\\d{2}\\.\\d{3})";

struct parser_c::impl_t {
public:
  std::vector<std::string> current_block, global_blocks, local_blocks;
  bool parsing_global_data{true};
  std::deque<parser_c::cue_cptr> cues;
  unsigned int current_cue_number{}, total_number_of_cues{}, total_number_of_bytes{};
  debugging_option_c debug{"parser"};

  mtx::regex::jp::Regex timestamp_line_re{fmt::format("^{0} --> {0}(?: ([^\\n]+))?$", RE_TIMESTAMP), "S"};
};

parser_c::parser_c()
  : m{new impl_t()}
{
}

parser_c::~parser_c() { // NOLINT(modernize-use-equals-default) due to pimpl idiom requiring explicit dtor declaration somewhere
}

void
parser_c::add_line(std::string const &line) {
  auto tmp = mtx::string::chomp(line);

  if (tmp.empty())
    add_block();

  else
    m->current_block.emplace_back(std::move(tmp));
}

void
parser_c::add_joined_lines(std::string const &joined_lines) {
  auto lines = mtx::string::split(mtx::string::chomp(mtx::string::normalize_line_endings(joined_lines)), "\n");

  for (auto const &line : lines)
    add_line(line);
}

void
parser_c::add_joined_lines(memory_c const &mem) {
  if (!mem.get_size())
    return;

  add_joined_lines(mem.to_string());
}

void
parser_c::flush() {
  add_block();
  m->parsing_global_data = false;
}

void
parser_c::add_block() {
  if (m->current_block.empty())
    return;

  mtx::regex::jp::VecNum matches;
  std::string label, additional;
  auto timestamp_line = -1;

  if (mtx::regex::match(m->current_block[0], matches, m->timestamp_line_re))
    timestamp_line = 0;

  else if ((m->current_block.size() > 1) && mtx::regex::match(m->current_block[1], matches, m->timestamp_line_re)) {
    timestamp_line = 1;
    label          = std::move(m->current_block[0]);

  } else {
    auto content = mtx::string::join(m->current_block, "\n");
    (m->parsing_global_data ? m->global_blocks : m->local_blocks).emplace_back(std::move(content));

    m->current_block.clear();

    return;
  }

  m->parsing_global_data = false;

  timestamp_c start, end;
  mtx::string::parse_timestamp(matches[0][1], start);
  mtx::string::parse_timestamp(matches[0][2], end);

  auto content       = mtx::string::join(m->current_block.begin() + timestamp_line + 1, m->current_block.end(), "\n");
  content            = adjust_embedded_timestamps(content, start.negate());
  auto cue           = std::make_shared<cue_t>();
  cue->m_start       = start;
  cue->m_duration    = end - start;
  cue->m_content     = memory_c::clone(content);
  auto settings_list = matches[0][3];

  if (! (label.empty() && settings_list.empty() && m->local_blocks.empty())) {
    additional = settings_list + "\n" + label + "\n" + mtx::string::join(m->local_blocks, "\n");
    cue->m_addition = memory_c::clone(additional);
  }

  mxdebug_if(m->debug,
             fmt::format("label «{0}» start «{1}» end «{2}» settings list «{3}» additional «{4}» content «{5}»\n",
                         label, matches[0][1], matches[0][2], matches[0][3],
                         mtx::regex::replace(additional, mtx::regex::jp::Regex{"\n+"}, "g", "–"),
                         mtx::regex::replace(content,    mtx::regex::jp::Regex{"\n+"}, "g", "–")));

  m->local_blocks.clear();
  m->current_block.clear();

  m->total_number_of_bytes += cue->m_content->get_size() + (cue->m_addition ? cue->m_addition->get_size() : 0);

  m->cues.emplace_back(cue);
}

bool
parser_c::codec_private_available()
  const {
  return !m->parsing_global_data;
}

memory_cptr
parser_c::get_codec_private()
  const {
  return memory_c::clone(mtx::string::join(m->global_blocks, "\n\n"));
}

bool
parser_c::cue_available()
  const {
  return !m->cues.empty();
}

parser_c::cue_cptr
parser_c::get_cue() {
  auto cue = m->cues.front();
  m->cues.pop_front();
  return cue;
}

unsigned int
parser_c::get_current_cue_number()
  const {
  return m->current_cue_number;
}

unsigned int
parser_c::get_total_number_of_cues()
  const {
  return m->total_number_of_cues;
}

unsigned int
parser_c::get_total_number_of_bytes()
  const {
  return m->total_number_of_bytes;
}

std::string
parser_c::adjust_embedded_timestamps(std::string const &text,
                                            timestamp_c const &offset) {
  static std::optional<mtx::regex::jp::Regex> s_embedded_timestamp_re;

  if (!s_embedded_timestamp_re)
    s_embedded_timestamp_re = mtx::regex::jp::Regex{fmt::format("<{0}>", RE_TIMESTAMP), "S"};

  return mtx::regex::replace(text, *s_embedded_timestamp_re, "g", [&offset](auto const &match) -> std::string {
    timestamp_c timestamp;
    mtx::string::parse_timestamp(match[1], timestamp);
    return fmt::format("<{0}>", mtx::string::format_timestamp(timestamp + offset, 3));
  });
}

} // namespace mtx::webvtt
