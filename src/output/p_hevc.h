/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   class definition for the HEVC video output module

*/

#pragma once

#include "common/common_pch.h"

#include "output/p_generic_video.h"

class hevc_video_packetizer_private_c;
class hevc_video_packetizer_c: public generic_video_packetizer_c {
protected:
  MTX_DECLARE_PRIVATE(hevc_video_packetizer_private_c)

  std::unique_ptr<hevc_video_packetizer_private_c> const p_ptr;

  explicit hevc_video_packetizer_c(hevc_video_packetizer_private_c &p);

public:
  hevc_video_packetizer_c(generic_reader_c *p_reader, track_info_c &p_ti, double fps, int width, int height);
  virtual int process(packet_cptr packet);
  virtual void set_headers();

  virtual connection_result_e can_connect_to(generic_packetizer_c *src, std::string &error_message);

  virtual translatable_string_c get_format_name() const {
    return YT("HEVC/H.265");
  }

protected:
  virtual void extract_aspect_ratio();

  virtual void flush_impl() override;
  virtual void flush_frames();
};
