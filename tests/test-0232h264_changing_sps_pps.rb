#!/usr/bin/ruby -w

class T_0232h264_changing_sps_pps < Test
  def description
    return "mkvmerge / H.264 with changing SPS/PPS NALUs / in(h264)"
  end

  def run
    merge("data/h264/changing-sps-pps.h264")
    return hash_tmp
  end
end
