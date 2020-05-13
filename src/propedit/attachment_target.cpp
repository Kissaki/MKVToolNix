/*
   mkvpropedit -- utility for editing properties of existing Matroska files

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include <matroska/KaxAttachments.h>

#include "common/construct.h"
#include "common/list_utils.h"
#include "common/mime.h"
#include "common/mm_io_x.h"
#include "common/mm_file_io.h"
#include "common/strings/editing.h"
#include "common/strings/parsing.h"
#include "common/strings/utf8.h"
#include "common/unique_numbers.h"
#include "propedit/attachment_target.h"

using namespace libmatroska;

attachment_target_c::attachment_target_c()
  : target_c()
  , m_command{ac_add}
  , m_selector_type{st_id}
  , m_selector_num_arg{}
  , m_attachments_modified{}
{
}

attachment_target_c::~attachment_target_c() {
}

void
attachment_target_c::set_id_manager(attachment_id_manager_cptr const &id_manager) {
  m_id_manager = id_manager;
}

bool
attachment_target_c::operator ==(target_c const &cmp)
  const {
  auto a_cmp = dynamic_cast<attachment_target_c const *>(&cmp);
  if (!a_cmp)
    return false;
  return (m_command             == a_cmp->m_command)
      && (m_options             == a_cmp->m_options)
      && (m_selector_type       == a_cmp->m_selector_type)
      && (m_selector_num_arg    == a_cmp->m_selector_num_arg)
      && (m_selector_string_arg == a_cmp->m_selector_string_arg);
}

void
attachment_target_c::validate() {
  if (!mtx::included_in(m_command, ac_add, ac_replace))
    return;

  try {
    m_file_content = mm_file_io_c::slurp(m_file_name);
  } catch (mtx::mm_io::exception &ex) {
    mxerror(fmt::format(Y("The file '{0}' could not be opened for reading: {1}.\n"), m_file_name, ex.what()));
  }
}

void
attachment_target_c::dump_info()
  const {
  mxinfo(fmt::format("  attachment target:\n"
                     "    file_name: {0}\n"
                     "    command: {1} ({2})\n"
                     "    options: {3}\n"
                     "    selector_type: {4} ({5})\n"
                     "    selector_num_arg: {6}\n"
                     "    selector_string_arg: {7}\n",
                     m_file_name,
                     m_command,
                       ac_add     == m_command ? "add"
                     : ac_delete  == m_command ? "delete"
                     : ac_replace == m_command ? "replace"
                     : ac_update  == m_command ? "update"
                     :                           "unknown",
                     m_options,
                     m_selector_type,
                       st_id        == m_selector_type ? "ID"
                     : st_uid       == m_selector_type ? "UID"
                     : st_name      == m_selector_type ? "name"
                     : st_mime_type == m_selector_type ? "MIME type"
                     :                                   "unknown",
                     m_selector_num_arg, m_selector_string_arg));
}

bool
attachment_target_c::has_changes()
  const {
  return true;
}

bool
attachment_target_c::has_content_been_modified()
  const {
  return m_attachments_modified;
}

void
attachment_target_c::parse_spec(command_e command,
                                const std::string &spec,
                                options_t const &options) {
  m_spec    = spec;
  m_command = command;
  m_options = options;

  if (ac_add == m_command) {
    if (spec.empty())
      throw std::invalid_argument{"empty file name"};
    m_file_name = spec;
    return;
  }

  std::regex s_spec_re;
  std::smatch matches;
  auto offset = 0u;

  if (ac_replace == m_command) {
    // captures:                1   2      3        4                5       6
    s_spec_re = std::regex{"^(?:(=)?(\\d+):(.+))|(?:(name|mime-type):([^:]+):(.+))$", std::regex_constants::icase};
    offset    = 1;

  } else if (mtx::included_in(m_command, ac_delete, ac_update))
    // captures:                1   2          3                4
    s_spec_re = std::regex{"^(?:(=)?(\\d+))|(?:(name|mime-type):(.+))$", std::regex_constants::icase};

  else
    assert(false);

  if (!std::regex_match(spec, matches, s_spec_re))
    throw std::invalid_argument{"generic format error"};

  if (matches[3 + offset].str().empty()) {
    // First case: by ID/UID
    if (ac_replace == m_command)
      m_file_name   = matches[3].str();
    m_selector_type = matches[1].str() == "=" ? st_uid : st_id;
    if (!mtx::string::parse_number(matches[2].str(), m_selector_num_arg))
      throw std::invalid_argument{"ID/UID not a number"};

  } else {
    // Second case: by name/MIME type
    if (ac_replace == m_command)
      m_file_name         = matches[6].str();
    m_selector_type       = balg::to_lower_copy(matches[3 + offset].str()) == "name" ? st_name : st_mime_type;
    m_selector_string_arg = matches[4 + offset].str();

    if (ac_replace == m_command)
      m_selector_string_arg = unescape_colon(m_selector_string_arg);
  }

  if ((ac_replace == m_command) && m_file_name.empty())
    throw std::invalid_argument{"empty file name"};
}

void
attachment_target_c::execute() {
  if (ac_add == m_command)
    execute_add();

  else if (ac_delete == m_command)
    execute_delete();

  else if (mtx::included_in(m_command, ac_replace, ac_update))
    execute_replace();

  else
    assert(false);
}

void
attachment_target_c::execute_add() {
  auto mime_type   = m_options.m_mime_type                          ? *m_options.m_mime_type   : mtx::mime::guess_type(m_file_name, true);
  auto file_name   = m_options.m_name && !m_options.m_name->empty() ? *m_options.m_name        : to_utf8(bfs::path{m_file_name}.filename().string());
  auto description = m_options.m_description                        ? *m_options.m_description : ""s;
  auto uid         = m_options.m_uid                                ? *m_options.m_uid         : create_unique_number(UNIQUE_ATTACHMENT_IDS);

  auto att          = mtx::construct::cons<KaxAttached>(new KaxFileName,                                         to_wide(file_name),
                                                        !description.empty() ? new KaxFileDescription : nullptr, to_wide(description),
                                                        new KaxMimeType,                                         mime_type,
                                                        new KaxFileUID,                                          uid,
                                                        new KaxFileData,                                         m_file_content);

  m_level1_element->PushElement(*att);

  m_attachments_modified = true;
}

void
attachment_target_c::execute_delete() {
  bool deleted_something = st_id == m_selector_type ? delete_by_id() : delete_by_uid_name_mime_type();

  if (!deleted_something)
    mxwarn(fmt::format(Y("No attachment matched the spec '{0}'.\n"), m_spec));

  else
    m_attachments_modified = true;
}

void
attachment_target_c::execute_replace() {
  bool replaced_something = st_id == m_selector_type ? replace_by_id() : replace_by_uid_name_mime_type();

  if (!replaced_something)
    mxwarn(fmt::format(Y("No attachment matched the spec '{0}'.\n"), m_spec));

  else
    m_attachments_modified = true;
}

bool
attachment_target_c::delete_by_id() {
  auto attached = m_id_manager->get(m_selector_num_arg);
  if (!attached)
    return false;

  m_id_manager->remove(m_selector_num_arg);

  auto itr = std::find(m_level1_element->begin(), m_level1_element->end(), attached);
  if (m_level1_element->end() == itr)
    return false;

  delete *itr;
  m_level1_element->Remove(itr);

  return true;
}

bool
attachment_target_c::matches_by_uid_name_or_mime_type(KaxAttached &att) {
  if (st_uid == m_selector_type) {
    auto file_uid = FindChild<KaxFileUID>(att);
    auto uid      = file_uid ? uint64(*file_uid) : static_cast<uint64_t>(0);
    return uid == m_selector_num_arg;
  }

  if (st_name == m_selector_type) {
    auto file_name = FindChild<KaxFileName>(att);
    return file_name && (UTFstring(*file_name).GetUTF8() == m_selector_string_arg);
  }

  if (st_mime_type == m_selector_type) {
    auto mime_type = FindChild<KaxMimeType>(att);
    return mime_type && (std::string(*mime_type) == m_selector_string_arg);
  }

  assert(false);
  return false;                 // avoid compiler warning
}

bool
attachment_target_c::delete_by_uid_name_mime_type() {
  bool deleted_something = false;

  for (auto counter = m_level1_element->ListSize(); 0 < counter; --counter) {
    auto idx = counter - 1;
    auto ele = (*m_level1_element)[idx];
    auto att = dynamic_cast<KaxAttached *>(ele);

    if (!att || !matches_by_uid_name_or_mime_type(*att))
      continue;

    m_level1_element->Remove(idx);
    m_id_manager->remove(att);
    delete att;

    deleted_something = true;
  }

  return deleted_something;
}

bool
attachment_target_c::replace_by_id() {
  auto attached = m_id_manager->get(m_selector_num_arg);
  if (!attached)
    return false;

  if (m_level1_element->end() == std::find(m_level1_element->begin(), m_level1_element->end(), attached))
    return false;

  replace_attachment_values(*attached);

  return true;
}

bool
attachment_target_c::replace_by_uid_name_mime_type() {
  bool replaced_something = false;

  for (auto idx = m_level1_element->ListSize(); 0 < idx; --idx) {
    auto attached = dynamic_cast<KaxAttached *>((*m_level1_element)[idx - 1]);

    if (!attached || !matches_by_uid_name_or_mime_type(*attached))
      continue;

    replace_attachment_values(*attached);
    replaced_something = true;
  }

  return replaced_something;
}

void
attachment_target_c::replace_attachment_values(KaxAttached &att) {
  if (m_options.m_name) {
    auto file_name = !m_options.m_name->empty() ? *m_options.m_name : to_utf8(bfs::path{m_file_name}.filename().string());
    GetChild<KaxFileName>(att).SetValueUTF8(file_name);
  }

  if (m_options.m_mime_type) {
    auto mime_type = m_options.m_mime_type->empty() ? mtx::mime::guess_type(m_file_name, true) : *m_options.m_mime_type;
    GetChild<KaxMimeType>(att).SetValue(mime_type);
  }

  if (m_options.m_description) {
    if (m_options.m_description->empty())
      DeleteChildren<KaxFileDescription>(att);
    else
      GetChild<KaxFileDescription>(att).SetValueUTF8(*m_options.m_description);
  }

  if (m_options.m_uid)
    GetChild<KaxFileUID>(att).SetValue(*m_options.m_uid);

  if (m_file_content)
    GetChild<KaxFileData>(att).CopyBuffer(m_file_content->get_buffer(), m_file_content->get_size());
}

std::string
attachment_target_c::unescape_colon(std::string arg) {
  boost::replace_all(arg, "\\c", ":");
  return arg;
}
