/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit http://www.gnu.org/copyleft/gpl.html

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include <ebml/EbmlHead.h>
#include <ebml/EbmlMaster.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlSubHead.h>
#include <ebml/EbmlVoid.h>
#include <matroska/KaxSemantic.h>

#include "common/at_scope_exit.h"
#include "common/ebml.h"
#include "common/doc_type_version_handler.h"
#include "common/doc_type_version_handler_p.h"
#include "common/list_utils.h"
#include "common/mm_io.h"
#include "common/mm_io_x.h"
#include "common/stereo_mode.h"

namespace mtx {

doc_type_version_handler_c::doc_type_version_handler_c()
  : p_ptr{new doc_type_version_handler_private_c}
{
  doc_type_version_handler_private_c::init_tables();
}

doc_type_version_handler_c::~doc_type_version_handler_c() {
}

EbmlElement &
doc_type_version_handler_c::render(EbmlElement &element,
                                   mm_io_c &file,
                                   bool with_default) {
  element.Render(file, with_default);
  return account(element, with_default);
}

EbmlElement &
doc_type_version_handler_c::account(EbmlElement &element,
                                    bool with_default) {
  if (!with_default && element.IsDefaultValue())
    return element;

  auto p  = p_func();
  auto id = EbmlId(element).GetValue();

  if (p->s_version_by_element[id] > p->version) {
    mxdebug_if(p->debug, boost::format("account: bumping version from %1% to %2% due to ID 0x%|3$x|\n") % p->version % p->s_version_by_element[id] % id);
    p->version = p->s_version_by_element[id];
  }

  if (p->s_read_version_by_element[id] > p->read_version) {
    mxdebug_if(p->debug, boost::format("account: bumping read_version from %1% to %2% due to ID 0x%|3$x|\n") % p->read_version % p->s_read_version_by_element[id] % id);
    p->read_version = p->s_read_version_by_element[id];
  }

  if (dynamic_cast<EbmlMaster *>(&element)) {
    auto &master = static_cast<EbmlMaster &>(element);
    for (auto child : master)
      account(*child);

  } else if (dynamic_cast<KaxVideoStereoMode *>(&element)) {
    auto value = static_cast<KaxVideoStereoMode &>(element).GetValue();
    if (!mtx::included_in(static_cast<stereo_mode_c::mode>(value), stereo_mode_c::mono, stereo_mode_c::unspecified) && (p->version < 3)) {
      mxdebug_if(p->debug, boost::format("account: bumping version from %1% to 3 due to KaxVideoStereoMode value %2%\n") % p->version % value);
      p->version = 3;
    }
  }

  return element;
}

doc_type_version_handler_c::update_result_e
doc_type_version_handler_c::update_ebml_head(mm_io_c &file) {
  auto p      = p_func();
  auto result = do_update_ebml_head(file);

  mxdebug_if(p->debug, boost::format("update_ebml_head: result %1%\n") % static_cast<unsigned int>(result));

  return result;
}

doc_type_version_handler_c::update_result_e
doc_type_version_handler_c::do_update_ebml_head(mm_io_c &file) {
  auto p = p_func();

  auto previous_pos = file.getFilePointer();
  at_scope_exit_c restore_pos([previous_pos, &file]() { file.setFilePointer(previous_pos); });

  try {
    file.setFilePointer(0);
    auto stream = std::make_shared<EbmlStream>(file);
    auto head   = std::shared_ptr<EbmlHead>(static_cast<EbmlHead *>(stream->FindNextID(EBML_INFO(EbmlHead), 0xFFFFFFFFL)));

    if (!head)
      return update_result_e::err_no_head_found;

    EbmlElement *l0{};
    int upper_lvl_el{};
    auto &context = EBML_CONTEXT(head.get());

    head->Read(*stream, context, upper_lvl_el, l0, true, SCOPE_ALL_DATA);
    head->SkipData(*stream, context);

    auto old_size          = file.getFilePointer() - head->GetElementPosition();
    auto &dt_version       = GetChild<EDocTypeVersion>(*head);
    auto file_version      = dt_version.GetValue();
    auto &dt_read_version  = GetChild<EDocTypeReadVersion>(*head);
    auto file_read_version = dt_read_version.GetValue();
    auto changed           = false;

    if (file_version < p->version) {
      dt_version.SetValue(p->version);
      changed = true;
    }

    if (file_read_version < p->read_version) {
      dt_read_version.SetValue(p->read_version);
      changed = true;
    }

    mxdebug_if(p->debug,
               boost::format("do_update_ebml_head: account version %1% read_version %2%, file version %3% read_version %4%, changed %5%\n")
               % p->version % p->read_version % file_version % file_read_version % changed);

    if (!changed)
      return update_result_e::ok_no_update_needed;

    head->UpdateSize(true);
    auto new_size = head->ElementSize(true);

    if (new_size > old_size)
      return update_result_e::err_not_enough_space;

    auto diff = old_size - new_size;
    if (diff == 1)
      head->SetSizeLength(head->GetSizeLength() + 1);

    file.setFilePointer(head->GetElementPosition());
    head->Render(*stream, true);

    if (diff > 1) {
      EbmlVoid v;
      v.SetSize(diff - 2);
      v.Render(*stream);
    }

  } catch (mtx::mm_io::exception &) {
    return update_result_e::err_read_or_write_failure;
  }

  return update_result_e::ok_updated;
}

} // namespace mtx
