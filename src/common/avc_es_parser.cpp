/** MPEG-4 p10 elementary stream parser

   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   \file

   \author Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include "common/avcc.h"
#include "common/avc_es_parser.h"
#include "common/bit_reader.h"
#include "common/byte_buffer.h"
#include "common/checksums/base.h"
#include "common/endian.h"
#include "common/frame_timing.h"
#include "common/hacks.h"
#include "common/memory_slice_cursor.h"
#include "common/mm_io.h"
#include "common/mpeg.h"
#include "common/strings/formatting.h"

namespace mtx::avc {

es_parser_c::es_parser_c()
  : mtx::avc_hevc::es_parser_c{"avc"s, 11, 13}
{
  if (m_debug_nalu_types || debugging_c::requested("avc_statistics"))
    init_nalu_names();
}

es_parser_c::~es_parser_c() {
  mxdebug_if(m_debug_timestamps, fmt::format("stream_position {0} parsed_position {1}\n", m_stream_position, m_parsed_position));

  if (!debugging_c::requested("avc_statistics"))
    return;

  mxdebug(fmt::format("AVC statistics: #frames: out {0} discarded {1} #timestamps: in {2} generated {3} discarded {4} num_fields: {5} num_frames: {6} num_sei_nalus: {7} num_idr_slices: {8}\n",
                      m_stats.num_frames_out,   m_stats.num_frames_discarded, m_stats.num_timestamps_in, m_stats.num_timestamps_generated, m_stats.num_timestamps_discarded,
                      m_stats.num_field_slices, m_stats.num_frame_slices,     m_stats.num_sei_nalus,     m_stats.num_idr_slices));

  static const char *s_slice_type_names[] = {
    "P",  "B",  "I",  "SP",  "SI",
    "P2", "B2", "I2", "SP2", "SI2",
    "unknown"
  };

  mxdebug("mpeg4::p10: Number of NALUs by type:\n");
  for (int i = 0, size = m_stats.num_nalus_by_type.size(); i < size; ++i)
    if (0 != m_stats.num_nalus_by_type[i])
      mxdebug(fmt::format("  {0}: {1}\n", get_nalu_type_name(i + 1), m_stats.num_nalus_by_type[i]));

  mxdebug("mpeg4::p10: Number of slices by type:\n");
  for (int i = 0, size = m_stats.num_slices_by_type.size(); i < size; ++i)
    if (0 != m_stats.num_slices_by_type[i])
      mxdebug(fmt::format("  {0}: {1}\n", s_slice_type_names[i], m_stats.num_slices_by_type[i]));
}

bool
es_parser_c::headers_parsed()
  const {
  return m_configuration_record_ready
      && !m_sps_info_list.empty()
      && (m_sps_info_list.front().width  > 0)
      && (m_sps_info_list.front().height > 0);
}

void
es_parser_c::add_bytes(unsigned char *buffer,
                       size_t size) {
  mtx::mem::slice_cursor_c cursor;
  int marker_size              = 0;
  int previous_marker_size     = 0;
  int previous_pos             = -1;
  uint64_t previous_parsed_pos = m_parsed_position;

  if (m_unparsed_buffer && (0 != m_unparsed_buffer->get_size()))
    cursor.add_slice(m_unparsed_buffer);
  cursor.add_slice(buffer, size);

  if (3 <= cursor.get_remaining_size()) {
    uint32_t marker =                               1 << 24
                    | (unsigned int)cursor.get_char() << 16
                    | (unsigned int)cursor.get_char() <<  8
                    | (unsigned int)cursor.get_char();

    while (true) {
      if (NALU_START_CODE == marker)
        marker_size = 4;
      else if (NALU_START_CODE == (marker & 0x00ffffff))
        marker_size = 3;

      if (0 != marker_size) {
        if (-1 != previous_pos) {
          int new_size = cursor.get_position() - marker_size - previous_pos - previous_marker_size;
          auto nalu = memory_c::alloc(new_size);
          cursor.copy(nalu->get_buffer(), previous_pos + previous_marker_size, new_size);
          m_parsed_position = previous_parsed_pos + previous_pos;

          mtx::mpeg::remove_trailing_zero_bytes(*nalu);
          if (nalu->get_size())
            handle_nalu(nalu, m_parsed_position);
        }
        previous_pos         = cursor.get_position() - marker_size;
        previous_marker_size = marker_size;
        marker_size          = 0;
      }

      if (!cursor.char_available())
        break;

      marker <<= 8;
      marker  |= (unsigned int)cursor.get_char();
    }
  }

  if (-1 == previous_pos)
    previous_pos = 0;

  m_stream_position += size;
  m_parsed_position  = previous_parsed_pos + previous_pos;

  int new_size = cursor.get_size() - previous_pos;
  if (0 != new_size) {
    m_unparsed_buffer = memory_c::alloc(new_size);
    cursor.copy(m_unparsed_buffer->get_buffer(), previous_pos, new_size);

  } else
    m_unparsed_buffer.reset();
}

void
es_parser_c::flush() {
  if (m_unparsed_buffer && (5 <= m_unparsed_buffer->get_size())) {
    m_parsed_position += m_unparsed_buffer->get_size();
    int marker_size = get_uint32_be(m_unparsed_buffer->get_buffer()) == NALU_START_CODE ? 4 : 3;
    auto nalu_size  = m_unparsed_buffer->get_size() - marker_size;
    handle_nalu(memory_c::clone(m_unparsed_buffer->get_buffer() + marker_size, nalu_size), m_parsed_position - nalu_size);
  }

  m_unparsed_buffer.reset();
  if (m_have_incomplete_frame) {
    m_frames.push_back(m_incomplete_frame);
    m_have_incomplete_frame = false;
  }

  cleanup();
}

void
es_parser_c::clear() {
  m_unparsed_buffer.reset();
  m_have_incomplete_frame = false;
  m_parsed_position       = 0;
}

bool
es_parser_c::flush_decision(mtx::avc_hevc::slice_info_t &si,
                            mtx::avc_hevc::slice_info_t &ref) {

  if (NALU_TYPE_IDR_SLICE == si.nalu_type) {
    if (0 != si.first_mb_in_slice)
      return false;

    if ((NALU_TYPE_IDR_SLICE == ref.nalu_type) && (si.idr_pic_id != ref.idr_pic_id))
      return true;

    if (NALU_TYPE_IDR_SLICE != ref.nalu_type)
      return true;
  }

  if (si.frame_num != ref.frame_num)
    return true;
  if (si.field_pic_flag != ref.field_pic_flag)
    return true;
  if ((si.nal_ref_idc != ref.nal_ref_idc) && (!si.nal_ref_idc || !ref.nal_ref_idc))
    return true;

  if (m_sps_info_list[si.sps].pic_order_cnt_type == m_sps_info_list[ref.sps].pic_order_cnt_type) {
    if (!m_sps_info_list[ref.sps].pic_order_cnt_type) {
      if (si.pic_order_cnt_lsb != ref.pic_order_cnt_lsb)
        return true;
      if (si.delta_pic_order_cnt_bottom != ref.delta_pic_order_cnt_bottom)
        return true;

    } else if ((1 == m_sps_info_list[ref.sps].pic_order_cnt_type)
               && (   (si.delta_pic_order_cnt[0] != ref.delta_pic_order_cnt[0])
                   || (si.delta_pic_order_cnt[1] != ref.delta_pic_order_cnt[1])))
      return true;
  }

  return false;
}

void
es_parser_c::flush_incomplete_frame() {
  if (!m_have_incomplete_frame || !m_configuration_record_ready)
    return;

  m_frames.push_back(m_incomplete_frame);
  m_incomplete_frame.clear();
  m_have_incomplete_frame = false;
}

void
es_parser_c::add_sps_and_pps_to_extra_data() {
  mxdebug_if(m_debug_sps_pps_changes, fmt::format("mpeg4::p10: adding all SPS & PPS before key frame due to changes from AVCC\n"));

  m_extra_data.erase(std::remove_if(m_extra_data.begin(), m_extra_data.end(), [this](memory_cptr const &nalu) -> bool {
    if (nalu->get_size() < static_cast<std::size_t>(m_nalu_size_length + 1))
      return true;

    auto const type = *(nalu->get_buffer() + m_nalu_size_length) & 0x1f;
    return (type == NALU_TYPE_SEQ_PARAM) || (type == NALU_TYPE_PIC_PARAM);
  }), m_extra_data.end());

  std::vector<memory_cptr> tmp;
  tmp.reserve(m_extra_data.size() + m_sps_list.size() + m_pps_list.size());

  std::transform(m_sps_list.begin(), m_sps_list.end(), std::back_inserter(tmp), [this](memory_cptr const &nalu) { return create_nalu_with_size(nalu); });
  std::transform(m_pps_list.begin(), m_pps_list.end(), std::back_inserter(tmp), [this](memory_cptr const &nalu) { return create_nalu_with_size(nalu); });
  tmp.insert(tmp.end(), m_extra_data.begin(), m_extra_data.end());

  m_extra_data = std::move(tmp);
}

void
es_parser_c::handle_slice_nalu(memory_cptr const &nalu,
                               uint64_t nalu_pos) {
  if (!m_configuration_record_ready) {
    m_unhandled_nalus.emplace_back(nalu, nalu_pos);
    return;
  }

  mtx::avc_hevc::slice_info_t si;
  if (!parse_slice(nalu, si))   // no conversion to RBSP; the bit reader takes care of it
    return;

  if (NALU_TYPE_IDR_SLICE == si.nalu_type)
    ++m_stats.num_idr_slices;

  if (m_have_incomplete_frame && flush_decision(si, m_incomplete_frame.m_si))
    flush_incomplete_frame();

  if (m_have_incomplete_frame) {
    memory_c &mem = *(m_incomplete_frame.m_data.get());
    int offset    = mem.get_size();
    mem.resize(offset + m_nalu_size_length + nalu->get_size());
    mtx::mpeg::write_nalu_size(mem.get_buffer() + offset, nalu->get_size(), m_nalu_size_length);
    memcpy(mem.get_buffer() + offset + m_nalu_size_length, nalu->get_buffer(), nalu->get_size());

    return;
  }

  bool is_i_slice =  (SLICE_TYPE_I   == si.slice_type)
                  || (SLICE_TYPE2_I  == si.slice_type)
                  || (SLICE_TYPE_SI  == si.slice_type)
                  || (SLICE_TYPE2_SI == si.slice_type);
  bool is_b_slice =  (SLICE_TYPE_B   == si.slice_type)
                  || (SLICE_TYPE2_B  == si.slice_type);

  m_incomplete_frame.m_si       =  si;
  m_incomplete_frame.m_keyframe =  m_recovery_point_valid
                                || (   is_i_slice
                                    && (NALU_TYPE_IDR_SLICE == si.nalu_type))
                                || (   is_i_slice
                                    && m_all_i_slices_are_key_frames);
  m_incomplete_frame.m_type     =  m_incomplete_frame.m_keyframe ? 'I' : is_b_slice ? 'B' : 'P';
  m_incomplete_frame.m_position =  nalu_pos;
  m_recovery_point_valid        =  false;

  if (m_incomplete_frame.m_keyframe) {
    mxdebug_if(m_debug_keyframe_detection,
               fmt::format("AVC:handle_slice_nalu: have incomplete? {0} current is {1} incomplete key bottom field? {2}\n",
                           m_have_incomplete_frame,
                           !si.field_pic_flag                ? "frame" : si.bottom_field_flag              ? "bottom field" : "top field",
                           !m_current_key_frame_bottom_field ? "none"  : *m_current_key_frame_bottom_field ? "bottom field" : "top field"));

    // if (!m_first_keyframe_found) {
    //   mxinfo(fmt::format("first KF; num prov TC {0} last prov TC {1}\n", m_provided_timestamps.size(), mtx::string::format_timestamp(m_provided_timestamps.empty() ? -1 : m_provided_timestamps.back().first)));
    //   for (auto const &t : m_provided_timestamps)
    //     mxinfo(fmt::format("  {0} @ {1}\n", mtx::string::format_timestamp(t.first), t.second));

    // }

    m_first_keyframe_found    = true;
    m_b_frames_since_keyframe = false;
    if (!si.field_pic_flag || !m_current_key_frame_bottom_field || (*m_current_key_frame_bottom_field == si.bottom_field_flag)) {
      cleanup();

      if (m_configuration_record_changed)
        add_sps_and_pps_to_extra_data();
    }

    if (!si.field_pic_flag)
      m_current_key_frame_bottom_field = std::nullopt;

    else if (m_current_key_frame_bottom_field && (*m_current_key_frame_bottom_field != si.bottom_field_flag))
      m_current_key_frame_bottom_field = std::nullopt;

    else
      m_current_key_frame_bottom_field = si.bottom_field_flag;

  } else if (is_b_slice)
    m_b_frames_since_keyframe = true;

  m_incomplete_frame.m_data = create_nalu_with_size(nalu, true);
  m_have_incomplete_frame   = true;

  ++m_frame_number;
}

void
es_parser_c::handle_sps_nalu(memory_cptr const &nalu) {
  sps_info_t sps_info;

  auto parsed_nalu = parse_sps(mtx::mpeg::nalu_to_rbsp(nalu), sps_info, m_keep_ar_info, m_fix_bitstream_frame_rate, duration_for(0, true));
  if (!parsed_nalu)
    return;

  parsed_nalu = mtx::mpeg::rbsp_to_nalu(parsed_nalu);

  size_t i;
  for (i = 0; m_sps_info_list.size() > i; ++i)
    if (m_sps_info_list[i].id == sps_info.id)
      break;

  bool use_sps_info = true;
  if (m_sps_info_list.size() == i) {
    m_sps_list.push_back(parsed_nalu);
    m_sps_info_list.push_back(sps_info);

    if (m_configuration_record_ready)
      m_configuration_record_changed = true;

  } else if (m_sps_info_list[i].checksum != sps_info.checksum) {
    mxdebug_if(m_debug_sps_pps_changes, fmt::format("mpeg4::p10: SPS ID {0:04x} changed; checksum old {1:04x} new {2:04x}\n", sps_info.id, m_sps_info_list[i].checksum, sps_info.checksum));

    cleanup();

    m_sps_info_list[i] = sps_info;
    m_sps_list[i]      = parsed_nalu;

    if (m_configuration_record_ready)
      m_configuration_record_changed = true;

  } else
    use_sps_info = false;

  m_extra_data.push_back(create_nalu_with_size(parsed_nalu));

  if (use_sps_info && m_debug_sps_info)
    sps_info.dump();

  if (!use_sps_info)
    return;

  if (   !has_stream_default_duration()
      && sps_info.timing_info_valid()) {
    m_stream_default_duration = sps_info.timing_info.default_duration();
    mxdebug_if(m_debug_timestamps, fmt::format("Stream default duration: {0}\n", m_stream_default_duration));
  }

  if (   !m_par_found
      && sps_info.ar_found
      && (0 != sps_info.par_den)) {
    m_par_found = true;
    m_par       = mtx_mp_rational_t(sps_info.par_num, sps_info.par_den);
  }
}

void
es_parser_c::handle_pps_nalu(memory_cptr const &nalu) {
  pps_info_t pps_info;

  if (!parse_pps(mtx::mpeg::nalu_to_rbsp(nalu), pps_info))
    return;

  size_t i;
  for (i = 0; m_pps_info_list.size() > i; ++i)
    if (m_pps_info_list[i].id == pps_info.id)
      break;

  if (m_pps_info_list.size() == i) {
    m_pps_list.push_back(nalu);
    m_pps_info_list.push_back(pps_info);

    if (m_configuration_record_ready)
      m_configuration_record_changed = true;

  } else if (m_pps_info_list[i].checksum != pps_info.checksum) {
    mxdebug_if(m_debug_sps_pps_changes, fmt::format("mpeg4::p10: PPS ID {0:04x} changed; checksum old {1:04x} new {2:04x}\n", pps_info.id, m_pps_info_list[i].checksum, pps_info.checksum));

    if (m_pps_info_list[i].sps_id != pps_info.sps_id)
      cleanup();

    m_pps_info_list[i] = pps_info;
    m_pps_list[i]      = nalu;

    if (m_configuration_record_ready)
      m_configuration_record_changed = true;
  }

  m_extra_data.push_back(create_nalu_with_size(nalu));
}

void
es_parser_c::handle_sei_nalu(memory_cptr const &nalu) {
  try {
    ++m_stats.num_sei_nalus;

    auto nalu_as_rbsp = mtx::mpeg::nalu_to_rbsp(nalu);

    mtx::bits::reader_c r(nalu_as_rbsp->get_buffer(), nalu_as_rbsp->get_size());

    r.skip_bits(8);

    while (true) {
      int ptype = 0;
      int value;
      while ((value = r.get_bits(8)) == 0xff)
        ptype += value;
      ptype += value;

      int psize = 0;
      while ((value = r.get_bits(8)) == 0xff)
        psize += value;
      psize += value;

      if (6 == ptype) {         // recovery point
        m_recovery_point_valid = true;
        return;
      } else if (0x80 == ptype)
        return;

      r.skip_bits(psize * 8);
    }
  } catch (...) {
  }
}

void
es_parser_c::handle_nalu(memory_cptr const &nalu,
                         uint64_t nalu_pos) {
  if (1 > nalu->get_size())
    return;

  int type = *(nalu->get_buffer()) & 0x1f;

  mxdebug_if(m_debug_nalu_types, fmt::format("NALU type 0x{0:02x} ({1}) at {2} size {3}\n", type, get_nalu_type_name(type), nalu_pos, nalu->get_size()));

  ++m_stats.num_nalus_by_type[std::max(std::min(type, 13), 1) - 1];

  switch (type) {
    case NALU_TYPE_SEQ_PARAM:
      handle_sps_nalu(nalu);
      break;

    case NALU_TYPE_PIC_PARAM:
      handle_pps_nalu(nalu);
      break;

    case NALU_TYPE_END_OF_SEQ:
    case NALU_TYPE_END_OF_STREAM:
    case NALU_TYPE_ACCESS_UNIT:
      flush_incomplete_frame();
      break;

    case NALU_TYPE_FILLER_DATA:
      // Skip these.
      break;

    case NALU_TYPE_NON_IDR_SLICE:
    case NALU_TYPE_DP_A_SLICE:
    case NALU_TYPE_DP_B_SLICE:
    case NALU_TYPE_DP_C_SLICE:
    case NALU_TYPE_IDR_SLICE:
      if (!m_configuration_record_ready && !m_sps_info_list.empty() && !m_pps_info_list.empty()) {
        m_configuration_record_ready = true;
        flush_unhandled_nalus();
      }
      handle_slice_nalu(nalu, nalu_pos);
      break;

    default:
      flush_incomplete_frame();
      if (!m_configuration_record_ready && !m_sps_info_list.empty() && !m_pps_info_list.empty()) {
        m_configuration_record_ready = true;
        flush_unhandled_nalus();
      }
      m_extra_data.push_back(create_nalu_with_size(nalu));

      if (NALU_TYPE_SEI == type)
        handle_sei_nalu(nalu);

      break;
  }
}

bool
es_parser_c::parse_slice(memory_cptr const &nalu,
                         mtx::avc_hevc::slice_info_t &si) {
  try {
    mtx::bits::reader_c r(nalu->get_buffer(), nalu->get_size());
    r.enable_rbsp_mode();

    si.clear();

    r.skip_bit();                   // forbidden_zero_bit
    si.nal_ref_idc = r.get_bits(2); // nal_ref_idc
    si.nalu_type   = r.get_bits(5); // si.nalu_type
    if (   (NALU_TYPE_NON_IDR_SLICE != si.nalu_type)
        && (NALU_TYPE_DP_A_SLICE    != si.nalu_type)
        && (NALU_TYPE_IDR_SLICE     != si.nalu_type))
      return false;

    si.first_mb_in_slice = r.get_unsigned_golomb(); // first_mb_in_slice
    si.slice_type        = r.get_unsigned_golomb(); // slice_type

    ++m_stats.num_slices_by_type[9 < si.slice_type ? 10 : si.slice_type];

    if (9 < si.slice_type) {
      mxdebug_if(m_debug_errors, fmt::format("slice parser error: 9 < si.slice_type: {0}\n", si.slice_type));
      return false;
    }

    si.pps_id = r.get_unsigned_golomb();      // pps_id

    size_t pps_idx;
    for (pps_idx = 0; m_pps_info_list.size() > pps_idx; ++pps_idx)
      if (m_pps_info_list[pps_idx].id == si.pps_id)
        break;
    if (m_pps_info_list.size() == pps_idx) {
      mxdebug_if(m_debug_errors, fmt::format("slice parser error: PPS not found: {0}\n", si.pps_id));
      return false;
    }

    pps_info_t &pps = m_pps_info_list[pps_idx];
    size_t sps_idx;
    for (sps_idx = 0; m_sps_info_list.size() > sps_idx; ++sps_idx)
      if (m_sps_info_list[sps_idx].id == pps.sps_id)
        break;
    if (m_sps_info_list.size() == sps_idx)
      return false;

    si.sps = sps_idx;
    si.pps = pps_idx;

    sps_info_t &sps = m_sps_info_list[sps_idx];

    si.frame_num = r.get_bits(sps.log2_max_frame_num);

    if (!sps.frame_mbs_only) {
      si.field_pic_flag = r.get_bit();
      if (si.field_pic_flag)
        si.bottom_field_flag = r.get_bit();
    }

    if (NALU_TYPE_IDR_SLICE == si.nalu_type)
      si.idr_pic_id = r.get_unsigned_golomb();

    if (0 == sps.pic_order_cnt_type) {
      si.pic_order_cnt_lsb = r.get_bits(sps.log2_max_pic_order_cnt_lsb);
      if (pps.pic_order_present && !si.field_pic_flag)
        si.delta_pic_order_cnt_bottom = r.get_signed_golomb();
    }

    if ((1 == sps.pic_order_cnt_type) && !sps.delta_pic_order_always_zero_flag) {
      si.delta_pic_order_cnt[0] = r.get_signed_golomb();
      if (pps.pic_order_present && !si.field_pic_flag)
        si.delta_pic_order_cnt[1] = r.get_signed_golomb();
    }

    return true;
  } catch (...) {
    return false;
  }
}

int64_t
es_parser_c::duration_for(unsigned int sps,
                          bool field_pic_flag)
  const {
  int64_t duration = -1 != m_forced_default_duration                                            ? m_forced_default_duration
                   : (m_sps_info_list.size() > sps) && m_sps_info_list[sps].timing_info_valid() ? m_sps_info_list[sps].timing_info.default_duration()
                   : -1 != m_stream_default_duration                                            ? m_stream_default_duration
                   : -1 != m_container_default_duration                                         ? m_container_default_duration
                   :                                                                              20000000;
  return duration * (field_pic_flag ? 1 : 2);
}

void
es_parser_c::calculate_frame_order() {
  auto frames_begin           = m_frames.begin();
  auto frames_end             = m_frames.end();
  auto frame_itr              = frames_begin;

  auto const &idr             = frame_itr->m_si;
  auto const &sps             = m_sps_info_list[idr.sps];

  auto idx                    = 0u;
  auto prev_pic_order_cnt_msb = 0u;
  auto prev_pic_order_cnt_lsb = 0u;
  auto pic_order_cnt_msb      = 0u;

  m_simple_picture_order      = false;

  if (   (   (SLICE_TYPE_I   != idr.slice_type)
          && (SLICE_TYPE_SI  != idr.slice_type)
          && (SLICE_TYPE2_I  != idr.slice_type)
          && (SLICE_TYPE2_SI != idr.slice_type))
      || (0 == idr.nal_ref_idc)
      || (0 != sps.pic_order_cnt_type)) {
    m_simple_picture_order = true;
    // return;
  }

  while (frames_end != frame_itr) {
    auto const &si = frame_itr->m_si;

    if (si.sps != idr.sps) {
      m_simple_picture_order = true;
      break;
    }

    if (frames_begin == frame_itr)
      ;

    else if ((si.pic_order_cnt_lsb < prev_pic_order_cnt_lsb) && ((prev_pic_order_cnt_lsb - si.pic_order_cnt_lsb) >= (1u << (sps.log2_max_pic_order_cnt_lsb - 1))))
      pic_order_cnt_msb = prev_pic_order_cnt_msb + (1 << sps.log2_max_pic_order_cnt_lsb);

    else if ((si.pic_order_cnt_lsb > prev_pic_order_cnt_lsb) && ((si.pic_order_cnt_lsb - prev_pic_order_cnt_lsb) > (1u << (sps.log2_max_pic_order_cnt_lsb - 1))))
      pic_order_cnt_msb = prev_pic_order_cnt_msb - (1 << sps.log2_max_pic_order_cnt_lsb);

    else
      pic_order_cnt_msb = prev_pic_order_cnt_msb;

    frame_itr->m_presentation_order = pic_order_cnt_msb + si.pic_order_cnt_lsb;
    frame_itr->m_decode_order       = idx;

    ++frame_itr;
    ++idx;

    if (0 != si.nal_ref_idc) {
      prev_pic_order_cnt_lsb = si.pic_order_cnt_lsb;
      prev_pic_order_cnt_msb = pic_order_cnt_msb;
    }
  }
}

void
es_parser_c::calculate_frame_timestamps_and_references() {
  static auto s_debug_force_simple_picture_order = debugging_option_c{"avc_parser_force_simple_picture_order"};
  auto provided_timestamps_to_use                = calculate_provided_timestamps_to_use();

  if (!m_simple_picture_order && !s_debug_force_simple_picture_order)
    std::sort(m_frames.begin(), m_frames.end(), [](auto const &f1, auto const &f2) { return f1.m_presentation_order < f2.m_presentation_order; });

  auto frames_begin            = m_frames.begin();
  auto frames_end              = m_frames.end();
  auto frame_itr               = frames_begin;
  auto previous_frame_itr      = frames_begin;
  auto provided_timestamps_itr = provided_timestamps_to_use.begin();

  while (frames_end != frame_itr) {
    if (frame_itr->m_has_provided_timestamp) {
      frame_itr->m_start = *provided_timestamps_itr;
      ++provided_timestamps_itr;

      if (frames_begin != frame_itr)
        previous_frame_itr->m_end = frame_itr->m_start;

    } else {
      frame_itr->m_start = frames_begin == frame_itr ? m_max_timestamp : previous_frame_itr->m_end;
      ++m_stats.num_timestamps_generated;
    }

    frame_itr->m_end = frame_itr->m_start + duration_for(frame_itr->m_si);

    previous_frame_itr = frame_itr;
    ++frame_itr;
  }

  m_max_timestamp = m_frames.back().m_end;

  for (frame_itr = frames_begin; frames_end != frame_itr; ++frame_itr) {
    if (frame_itr->is_i_frame()) {
      m_previous_i_p_start = frame_itr->m_start;
      continue;
    }

    frame_itr->m_ref1 = m_previous_i_p_start - frame_itr->m_start;

    if (frame_itr->is_p_frame()) {
      m_previous_i_p_start = frame_itr->m_start;
      continue;
    }

    auto next_i_p_frame_itr = frame_itr + 1;

    while ((frames_end != next_i_p_frame_itr) && next_i_p_frame_itr->is_b_frame())
      ++next_i_p_frame_itr;

    auto forward_ref_start = frames_end != next_i_p_frame_itr ? next_i_p_frame_itr->m_start : m_max_timestamp;
    frame_itr->m_ref2      = forward_ref_start - frame_itr->m_start;
  }

  mxdebug_if(m_debug_timestamps, fmt::format("PRESENTATION order dump\n"));

  for (auto &frame : m_frames)
    mxdebug_if(m_debug_timestamps, fmt::format("  type {0} TS {1} ref1 {2} ref2 {3} decode_order {4}\n", frame.m_type, mtx::string::format_timestamp(frame.m_start), frame.m_ref1, frame.m_ref2, frame.m_decode_order));

  if (!m_simple_picture_order)
    std::sort(m_frames.begin(), m_frames.end(), [](auto const &f1, auto const &f2) { return f1.m_decode_order < f2.m_decode_order; });
}

void
es_parser_c::update_frame_stats() {
  mxdebug_if(m_debug_timestamps, fmt::format("DECODE order dump\n"));

  for (auto &frame : m_frames) {
    mxdebug_if(m_debug_timestamps, fmt::format("  type {0} TS {1} ref1 {2} ref2 {3}\n", frame.m_type, mtx::string::format_timestamp(frame.m_start), frame.m_ref1, frame.m_ref2));

    m_duration_frequency[frame.m_end - frame.m_start]++;

    if (frame.m_si.field_pic_flag)
      ++m_stats.num_field_slices;
    else
      ++m_stats.num_frame_slices;
  }
}

void
es_parser_c::calculate_frame_timestamps_references_and_update_stats() {
  calculate_frame_timestamps_and_references();
  update_frame_stats();
}

memory_cptr
es_parser_c::create_nalu_with_size(const memory_cptr &src,
                                   bool add_extra_data) {
  auto nalu = mtx::mpeg::create_nalu_with_size(src, m_nalu_size_length, add_extra_data ? m_extra_data : std::vector<memory_cptr>{});

  if (add_extra_data)
    m_extra_data.clear();

  return nalu;
}

memory_cptr
es_parser_c::get_avcc()
  const {
  return avcc_c{static_cast<unsigned int>(m_nalu_size_length), m_sps_list, m_pps_list}.pack();
}

void
es_parser_c::dump_info()
  const {
  mxinfo("Dumping m_frames_out:\n");
  for (auto &frame : m_frames_out) {
    mxinfo(fmt::format("size {0} key {1} start {2} end {3} ref1 {4} adler32 0x{5:08x}\n",
                       frame.m_data->get_size(),
                       frame.m_keyframe,
                       mtx::string::format_timestamp(frame.m_start),
                       mtx::string::format_timestamp(frame.m_end),
                       mtx::string::format_timestamp(frame.m_ref1),
                       mtx::checksum::calculate_as_uint(mtx::checksum::algorithm_e::adler32, *frame.m_data)));
  }
}

void
es_parser_c::init_nalu_names()
  const {
  if (!ms_nalu_names_by_type.empty())
    return;

  ms_nalu_names_by_type[NALU_TYPE_NON_IDR_SLICE] = "non IDR slice";
  ms_nalu_names_by_type[NALU_TYPE_DP_A_SLICE]    = "DP A slice";
  ms_nalu_names_by_type[NALU_TYPE_DP_B_SLICE]    = "DP B slice";
  ms_nalu_names_by_type[NALU_TYPE_DP_C_SLICE]    = "DP C slice";
  ms_nalu_names_by_type[NALU_TYPE_IDR_SLICE]     = "IDR slice";
  ms_nalu_names_by_type[NALU_TYPE_SEI]           = "SEI";
  ms_nalu_names_by_type[NALU_TYPE_SEQ_PARAM]     = "SEQ param";
  ms_nalu_names_by_type[NALU_TYPE_PIC_PARAM]     = "PIC param";
  ms_nalu_names_by_type[NALU_TYPE_ACCESS_UNIT]   = "access unit";
  ms_nalu_names_by_type[NALU_TYPE_END_OF_SEQ]    = "end of sequence";
  ms_nalu_names_by_type[NALU_TYPE_END_OF_STREAM] = "end of stream";
  ms_nalu_names_by_type[NALU_TYPE_FILLER_DATA]   = "filler";
}

}
