#!/usr/bin/ruby -w

class T_0049ass < Test
  def description
    return "mkvmerge / ASS subtitles / in(ASS)"
  end

  def run
    merge("--sub-charset 0:ISO-8859-1 data/subtitles/ssa-ass/11.Magyar.ass")
    return hash_tmp
  end
end

