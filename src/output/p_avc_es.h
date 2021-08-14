/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   class definition for the MPEG 4 part 10 ES video output module

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#pragma once

#include "common/common_pch.h"

#include "common/avc/es_parser.h"
#include "output/p_avc_hevc_es.h"

class avc_es_video_packetizer_c: public avc_hevc_es_video_packetizer_c {
protected:
  mtx::avc::es_parser_c m_parser;

public:
  avc_es_video_packetizer_c(generic_reader_c *p_reader, track_info_c &p_ti);

  virtual void add_extra_data(memory_cptr data);
  virtual void set_container_default_field_duration(int64_t default_duration);
  virtual unsigned int get_nalu_size_length() const;

  virtual void flush_frames();

  virtual translatable_string_c get_format_name() const {
    return YT("AVC/H.264 (unframed)");
  };

  virtual void connect(generic_packetizer_c *src, int64_t p_append_timestamp_offset = -1);
  virtual connection_result_e can_connect_to(generic_packetizer_c *src, std::string &error_message);

protected:
  virtual void process_impl(packet_cptr const &packet) override;
  virtual void handle_delayed_headers();
  virtual void handle_aspect_ratio();
  virtual void handle_actual_default_duration();
  virtual void flush_impl();
};
