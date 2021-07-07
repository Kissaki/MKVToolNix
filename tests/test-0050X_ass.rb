#!/usr/bin/ruby -w

class T_0050X_ass < Test
  def description
    return "mkvextract / ASS subtitles / out(ASS)"
  end

  def run
    my_hash = hash_file("data/subtitles/ssa-ass/11.Magyar.ass")
    my_tmp = tmp
    merge("--sub-charset 0:ISO-8859-1 data/subtitles/ssa-ass/11.Magyar.ass")
    @tmp = nil
    xtr_tracks(my_tmp, "-c ISO-8859-1 0:#{tmp}")
    File.unlink(my_tmp)
    return my_hash + "-" + hash_tmp
  end
end

