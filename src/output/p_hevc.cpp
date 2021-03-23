/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   HEVC video output module

*/

#include "common/common_pch.h"

#include <cmath>

#include "common/codec.h"
#include "common/endian.h"
#include "common/hacks.h"
#include "common/hevc.h"
#include "common/hevc_es_parser.h"
#include "common/strings/formatting.h"
#include "merge/output_control.h"
#include "output/p_hevc.h"

class hevc_video_packetizer_private_c {
public:
  int nalu_size_len{};
  int64_t max_nalu_size{};
  mtx::hevc::es_parser_c parser;
};

hevc_video_packetizer_c::
hevc_video_packetizer_c(generic_reader_c *p_reader,
                        track_info_c &p_ti,
                        double fps,
                        int width,
                        int height)
  : generic_video_packetizer_c{p_reader, p_ti, MKV_V_MPEGH_HEVC, fps, width, height}
  , p_ptr{new hevc_video_packetizer_private_c}
{
  auto &p = *p_func();

  m_relaxed_timestamp_checking = true;

  if (23 <= m_ti.m_private_data->get_size())
    p_ptr->nalu_size_len = (m_ti.m_private_data->get_buffer()[21] & 0x03) + 1;

  set_codec_private(m_ti.m_private_data);

  p.parser.normalize_parameter_sets();
  p.parser.set_hevcc(m_hcodec_private);
  p.parser.set_container_default_duration(m_htrack_default_duration);
}

void
hevc_video_packetizer_c::set_headers() {
  if (m_ti.m_private_data && m_ti.m_private_data->get_size())
    extract_aspect_ratio();

  if (m_ti.m_private_data && m_ti.m_private_data->get_size() && m_ti.m_fix_bitstream_frame_rate)
    set_codec_private(m_ti.m_private_data);

  generic_video_packetizer_c::set_headers();
}

void
hevc_video_packetizer_c::extract_aspect_ratio() {
  auto result = mtx::hevc::extract_par(m_ti.m_private_data);

  set_codec_private(result.new_hevcc);

  if (!result.is_valid() || display_dimensions_or_aspect_ratio_set())
    return;

  auto par = static_cast<double>(result.numerator) / static_cast<double>(result.denominator);

  set_video_display_dimensions(1 <= par ? std::llround(m_width * par) : m_width,
                               1 <= par ? m_height                    : std::llround(m_height / par),
                               generic_packetizer_c::ddu_pixels,
                               OPTION_SOURCE_BITSTREAM);

  mxinfo_tid(m_ti.m_fname, m_ti.m_id,
             fmt::format(Y("Extracted the aspect ratio information from the HEVC video data and set the display dimensions to {0}/{1}.\n"),
                         m_ti.m_display_width, m_ti.m_display_height));
}

int
hevc_video_packetizer_c::process(packet_cptr packet) {
  auto &p = *p_func();

  if (packet->is_key_frame() && (VFT_PFRAMEAUTOMATIC != packet->bref))
    p.parser.set_next_i_slice_is_key_frame();

  if (packet->has_timestamp())
    p.parser.add_timestamp(packet->timestamp);

  p.parser.add_bytes_framed(packet->data, p.nalu_size_len);

  flush_frames();

  return FILE_STATUS_MOREDATA;
}

connection_result_e
hevc_video_packetizer_c::can_connect_to(generic_packetizer_c *src,
                                        std::string &error_message) {
  hevc_video_packetizer_c *vsrc = dynamic_cast<hevc_video_packetizer_c *>(src);
  if (!vsrc)
    return CAN_CONNECT_NO_FORMAT;

  if (m_ti.m_private_data && vsrc->m_ti.m_private_data && memcmp(m_ti.m_private_data->get_buffer(), vsrc->m_ti.m_private_data->get_buffer(), m_ti.m_private_data->get_size())) {
    error_message = fmt::format(Y("The codec's private data does not match. Both have the same length ({0}) but different content."), m_ti.m_private_data->get_size());
    return CAN_CONNECT_MAYBE_CODECPRIVATE;
  }

  return CAN_CONNECT_YES;
}

void
hevc_video_packetizer_c::flush_impl() {
  auto &p = *p_func();

  p.parser.flush();
  flush_frames();
}

void
hevc_video_packetizer_c::flush_frames() {
  auto &p = *p_func();

  while (p.parser.frame_available()) {
    auto frame = p.parser.get_frame();
    add_packet(new packet_t(frame.m_data, frame.m_start,
                            frame.m_end > frame.m_start ? frame.m_end - frame.m_start : m_htrack_default_duration,
                            frame.m_keyframe            ? -1                          : frame.m_start + frame.m_ref1));
  }
}
