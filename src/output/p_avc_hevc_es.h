/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   class definition for the  HEVC ES video output module

*/

#pragma once

#include "common/common_pch.h"

#include "common/hevc/es_parser.h"
#include "merge/generic_packetizer.h"

class avc_hevc_es_video_packetizer_c: public generic_packetizer_c {
protected:
  std::unique_ptr<mtx::avc_hevc::es_parser_c> m_parser_base;

  int64_t m_default_duration_for_interlaced_content{-1};
  std::optional<int64_t> m_parser_default_duration_to_force;
  bool m_first_frame{true}, m_set_display_dimensions{false};
  debugging_option_c m_debug_timestamps, m_debug_aspect_ratio;

public:
  avc_hevc_es_video_packetizer_c(generic_reader_c *p_reader, track_info_c &p_ti, std::string const &p_debug_type, std::unique_ptr<mtx::avc_hevc::es_parser_c> &&parser_base);

  virtual void set_headers() override;

  virtual void add_extra_data(memory_cptr const &data);
  virtual void set_container_default_field_duration(int64_t default_duration);
  virtual unsigned int get_nalu_size_length() const;

  virtual void flush_frames() = 0;

protected:
  virtual void flush_impl() override;
};
