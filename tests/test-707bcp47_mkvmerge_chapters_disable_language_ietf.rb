#!/usr/bin/ruby -w

# T_707bcp47_mkvmerge_chapters_disable_language_ietf
describe "mkvmerge / BCP 47/RFC 5646 language tags: chapters (--disable-language-ietf)"

def compare_languages *expected_languages
  test "#{expected_languages}" do
    if expected_languages[0].is_a?(String)
      file_name = expected_languages.shift
    else
      file_name = tmp
    end

    xml, _ = extract(file_name, :mode => :chapters)
    tracks = []

    xml.each do |line|
      if %r{<ChapterAtom>}.match(line)
        tracks << {}

      elsif %r{<ChapterLanguage>(.+?)</ChapterLanguage>}.match(line)
        tracks.last[:language] = $1

      elsif %r{<ChapLanguageIETF>(.+?)</ChapLanguageIETF>}.match(line)
        tracks.last[:language_ietf] = $1
      end
    end

    fail "track number different: actual #{tracks.size} expected #{expected_languages.size}" if tracks.size != expected_languages.size

    (0..tracks.size - 1).each do |idx|
      props = tracks[idx]

      if props[:language] != expected_languages[idx][0]
        fail "track #{idx} actual language #{props[:language]} != expected #{expected_languages[idx][0]}"
      end

      if props[:language_ietf] != expected_languages[idx][1]
        fail "track #{idx} actual language_ietf #{props[:language_ietf]} != expected #{expected_languages[idx][1]}"
      end
    end

    "ok"
  end

  unlink_tmp_files
end

src1   = "data/subtitles/srt/vde-utf-8-bom.srt"
src2   = "data/avi/v.avi"
chaps1 = "data/chapters/shortchaps-utf8.txt"
chaps2 = "data/chapters/uk-and-gb.xml"
chaps3 = "data/chapters/uk-and-gb-chaplanguageietf.xml"

# simple chapters
test_merge src1, :keep_tmp => true, :args => "--disable-language-ietf --chapters #{chaps1}"
compare_languages(*([ [ "eng", nil ] ] * 5))

test_merge src1, :keep_tmp => true, :args => "--disable-language-ietf --chapter-language de --chapters #{chaps1}"
compare_languages(*([ [ "ger", nil] ] * 5))

test_merge src1, :keep_tmp => true, :args => "--disable-language-ietf --chapter-language de-latn-ch --chapters #{chaps1}"
compare_languages(*([ [ "ger", nil ] ] * 5))

# XML chapters
test_merge src1, :keep_tmp => true, :args => "--disable-language-ietf --chapters #{chaps2}"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))

test_merge src1, :keep_tmp => true, :args => "--disable-language-ietf --chapter-language de --chapters #{chaps2}"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))

test_merge src1, :keep_tmp => true, :args => "--disable-language-ietf --chapter-language de-latn-ch --chapters #{chaps2}"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))

# chapter generation
test_merge src2, :keep_tmp => true, :args => "--disable-language-ietf --no-audio --generate-chapters interval:10s"
compare_languages(*([ [ "eng", nil ] ] * 7))

test_merge src2, :keep_tmp => true, :args => "--disable-language-ietf --no-audio --chapter-language de --generate-chapters interval:10s"
compare_languages(*([ [ "ger", nil ] ] * 7))

test_merge src2, :keep_tmp => true, :args => "--disable-language-ietf --no-audio --chapter-language de-latn-ch --generate-chapters interval:10s"
compare_languages(*([ [ "ger", nil ] ] * 7))

test_merge src1, :keep_tmp => true, :args => "--disable-language-ietf --chapters #{chaps3}"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))


# simple chapters
test_merge src1, :keep_tmp => true, :args => "--chapters #{chaps1}", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "eng", nil ] ] * 5))

test_merge src1, :keep_tmp => true, :args => "--chapter-language de --chapters #{chaps1}", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "ger", nil] ] * 5))

test_merge src1, :keep_tmp => true, :args => "--chapter-language de-latn-ch --chapters #{chaps1}", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "ger", nil ] ] * 5))

# XML chapters
test_merge src1, :keep_tmp => true, :args => "--chapters #{chaps2}", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))

test_merge src1, :keep_tmp => true, :args => "--chapter-language de --chapters #{chaps2}", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))

test_merge src1, :keep_tmp => true, :args => "--chapter-language de-latn-ch --chapters #{chaps2}", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))

# chapter generation
test_merge src2, :keep_tmp => true, :args => "--no-audio --generate-chapters interval:10s", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "eng", nil ] ] * 7))

test_merge src2, :keep_tmp => true, :args => "--no-audio --chapter-language de --generate-chapters interval:10s", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "ger", nil ] ] * 7))

test_merge src2, :keep_tmp => true, :args => "--no-audio --chapter-language de-latn-ch --generate-chapters interval:10s", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "ger", nil ] ] * 7))

test_merge src1, :keep_tmp => true, :args => "--chapters #{chaps3}", :post_args => "--disable-language-ietf"
compare_languages(*([ [ "eng", nil ] ] * 3 + [ [ "ger", nil ] ] * 3))
