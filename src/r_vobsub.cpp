/*
  mkvmerge -- utility for splicing together matroska files
      from component media subtypes

  r_vobsub.h

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version $Id$
    \brief VobSub stream reader
    \author Moritz Bunkus <moritz@bunkus.org>
*/

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "mm_io.h"
#include "p_vobsub.h"
#include "r_vobsub.h"

using namespace std;

#define hexvalue(c) (isdigit(c) ? (c) - '0' : \
                     tolower(c) == 'a' ? 10 : \
                     tolower(c) == 'b' ? 11 : \
                     tolower(c) == 'c' ? 12 : \
                     tolower(c) == 'd' ? 13 : \
                     tolower(c) == 'e' ? 14 : 15)
#define istimecodestr(s)       (!strncmp(s, "timecode: ", 10))
#define istimestampstr(s)      (!strncmp(s, "timestamp: ", 11))
#define iscommafileposstr(s)   (!strncmp(s, ", filepos: ", 11))
#define iscolon(s)             (*(s) == ':')
#define istwodigits(s)         (isdigit(*(s)) && isdigit(*(s + 1)))
#define isthreedigits(s)       (isdigit(*(s)) && isdigit(*(s + 1)) && \
                                isdigit(*(s + 2)))
#define istwodigitscolon(s)    (istwodigits(s) && iscolon(s + 2))
#define istimecode(s)         (istwodigitscolon(s) && \
                                istwodigitscolon(s + 3) && \
                                istwodigitscolon(s + 6) && \
                                isthreedigits(s + 9))
#define ishexdigit(s)          (isdigit(s) || \
                                (strchr("abcdefABCDEF", s) != NULL))
#define isfilepos(s)           (ishexdigit(*(s)) && ishexdigit(*(s + 1)) && \
                                ishexdigit(*(s + 2)) && \
                                ishexdigit(*(s + 3)) && \
                                ishexdigit(*(s + 4)) && \
                                ishexdigit(*(s + 5)) && \
                                ishexdigit(*(s + 6)) && \
                                ishexdigit(*(s + 7)) && \
                                ishexdigit(*(s + 8)))
#define isvobsubline_v7(s)     ((strlen(s) >= 42) && \
                                istimestampstr(s) && istimecode(s + 11) && \
                                iscommafileposstr(s + 23) && \
                                isfilepos(s + 34))

#define PFX "vobsub_reader: "

int vobsub_reader_c::probe_file(mm_io_c *mm_io, int64_t size) {
  char chunk[80];

  try {
    mm_io->setFilePointer(0, seek_beginning);
    if (mm_io->read(chunk, 80) != 80)
      return 0;
    if (strncasecmp(chunk, "# VobSub index file, v",
                    strlen("# VobSub index file, v")))
      return 0;
    mm_io->setFilePointer(0, seek_beginning);
  } catch (...) {
    return 0;
  }
  return 1;
}

vobsub_reader_c::vobsub_reader_c(track_info_t *nti) throw (error_c):
  generic_reader_c(nti) {
  string sub_name, line;
  int len;
  
  try {
    idx_file = new mm_text_io_c(ti->fname);
  } catch (...) {
    throw error_c(PFX "Cound not open the source file.");
  }

  sub_name = ti->fname;
  len = sub_name.rfind(".");
  if (len >= 0)
    sub_name.erase(len);
  sub_name += ".sub";

  try {
    sub_file = new mm_io_c(sub_name.c_str(), MODE_READ);
  } catch (...) {
    string emsg = PFX "Could not open the sub file '";
    emsg += sub_name;
    emsg += "'.";
    throw error_c(emsg.c_str());
  }

  idx_data = "";
  act_wchar = 0;

  len = strlen("# VobSub index file, v");
  if (!idx_file->getline2(line) ||
      strncasecmp(line.c_str(), "# VobSub index file, v", len) ||
      (line.length() < (len + 1)))
    mxerror(PFX "No version number found.\n");

  version = line[len] - '0';
  len++;
  while ((len < line.length()) && isdigit(line[len])) {
    version = version * 10 + line[len] - '0';
    len++;
  }
  if (version < 7)
    mxerror(PFX "Only v7 and newer VobSub files are supported. If you have an "
            "older version then use the VSConv utility from "
            "http://sourceforge.net/projects/guliverkli/ to convert these "
            "files to v7 files.\n");

  parse_headers();
  mxinfo("Using VobSub subtitle reader for '%s' & '%s'.\n", ti->fname,
         sub_name.c_str());
  create_packetizers();
}

vobsub_reader_c::~vobsub_reader_c() {
  uint32_t i;

  for (i = 0; i < tracks.size(); i++)
    delete tracks[i];
  delete sub_file;
  delete idx_file;
}

void vobsub_reader_c::create_packetizers() {
  uint32_t i, k;
  int64_t avg_duration;

  for (i = 0; i < tracks.size(); i++) {
    if (!demuxing_requested('s', i))
      continue;

    ti->id = i;
//     ti.language = tracks[i]->language;
    tracks[i]->packetizer =
      new vobsub_packetizer_c(this, idx_data.c_str(), idx_data.length(),
                              COMPRESSION_ZLIB, COMPRESSION_NONE, ti);
    avg_duration = 0;
    for (k = 0; k < (tracks[i]->timecodes.size() - 1); k++) {
      tracks[i]->durations.push_back(tracks[i]->timecodes[k + 1] -
                                     tracks[i]->timecodes[k]);
      avg_duration += tracks[i]->timecodes[k + 1] - tracks[i]->timecodes[k];
    }
    if (tracks[i]->timecodes.size() == 0)
      avg_duration = 1000;
    else
      avg_duration /= (tracks[i]->timecodes.size() - 1);
    tracks[i]->durations.push_back(avg_duration);

    if (verbose)
      mxinfo("+-> Using VobSub subtitle output module for subtitle track "
             "%u (language: %s).\n", i, tracks[i]->language);
  }
}

void vobsub_reader_c::parse_headers() {
  string line;
  const char *sline;
  char language[3];
  vobsub_track_c *track, *last_track;
  int64_t filepos, last_pos, timestamp;
  int hour, minute, second, msecond, idx;
  uint32_t i;

  language[0] = 0;
  track = NULL;
  last_track = NULL;
  last_pos = -1;

  while (1) {
    if (!idx_file->getline2(line))
      break;

    if ((line.length() == 0) || (line[0] == '#')) {
//       idx_data += line;
//       idx_data += "\n";
      continue;
    }

    sline = line.c_str();

    if (!strncasecmp(sline, "id:", 3)) {
      if (line.length() >= 6) {
        language[0] = sline[4];
        language[1] = sline[5];
        language[2] = 0;
      } else
        language[0] = 0;
      last_track = track;
      track = new vobsub_track_c(language);
      tracks.push_back(track);
      continue;
    }

    if (!strncasecmp(sline, "alt:", 4))
      continue;

    if ((version == 7) && isvobsubline_v7(sline)) {
      if (track == NULL)
        mxerror(PFX ".idx file does not contain an 'id: ...' line to indicate "
                "the language.\n");

      idx = 34;
      filepos = hexvalue(sline[idx]);
      idx++;
      while ((idx < line.length()) && ishexdigit(sline[idx])) {
        filepos = filepos * 16 + hexvalue(sline[idx]);
        idx++;
      }

      if (last_pos != -1) {
        if (last_track != NULL) {
          last_track->sizes.push_back(filepos - last_pos);
          last_track = NULL;
        } else
          track->sizes.push_back(filepos - last_pos);
      }
      last_pos = filepos;
      track->positions.push_back(filepos);

      sscanf(&sline[11], "%02d:%02d:%02d:%03d", &hour, &minute, &second,
             &msecond);
      timestamp = (int64_t)hour * 60 * 60 * 1000 +
        (int64_t)minute * 60 * 1000 + (int64_t)second * 1000 +
        (int64_t)msecond;
      track->timecodes.push_back(timestamp);

      continue;
    }

    idx_data += line;
    idx_data += "\n";
  }

  if ((last_pos != -1) && (track != NULL)) {
    sub_file->setFilePointer(0, seek_end);
    track->sizes.push_back(sub_file->getFilePointer() - last_pos);
    sub_file->setFilePointer(0);
  }

  for (i = 0; i < tracks.size(); i++)
    if ((tracks[i]->positions.size() != tracks[i]->timecodes.size()) ||
        (tracks[i]->positions.size() != tracks[i]->sizes.size()))
      mxerror(PFX "Have %u positions, %u sizes and %u timecodes. This should "
              "not have happened. Please file a bug report.\n",
              tracks[i]->positions.size(), tracks[i]->sizes.size(),
              tracks[i]->timecodes.size());
}

int vobsub_reader_c::read(generic_packetizer_c *ptzr) {
  vobsub_track_c *track;
  unsigned char *data;
  uint32_t i, id;

  track = NULL;
  for (i = 0; i < tracks.size(); i++)
    if (tracks[i]->packetizer == ptzr) {
      track = tracks[i];
      break;
    }

  if ((track == NULL) || (track->idx >= track->positions.size()))
    return 0;

  id = i;
  i = track->idx;
  sub_file->setFilePointer(track->positions[i]);
  data = (unsigned char *)safemalloc(track->sizes[i]);
  if (sub_file->read(data, track->sizes[i]) != track->sizes[i]) {
    mxwarn(PFX "Could not read %lld bytes from the .sub file. Aborting.\n",
           track->sizes[i]);
    safefree(data);
    return 0;
  }
  mxverb(2, PFX "track: %u, size: %lld, at: %lld, timecode: %lld, duration: "
         "%lld\n", id, track->sizes[i], track->positions[i],
         track->timecodes[i], track->durations[i]);
  track->packetizer->process(data, track->sizes[i], track->timecodes[i],
                             track->durations[i]);
  safefree(data);
  track->idx++;

  if (track->idx >= track->sizes.size())
    return 0;
  else
    return EMOREDATA;
}

int vobsub_reader_c::display_priority() {
  return DISPLAYPRIORITY_LOW;
}

static char wchar[] = "-\\|/-\\|/-";

void vobsub_reader_c::display_progress(bool final) {
  mxinfo("working... %c\r", wchar[act_wchar]);
  act_wchar++;
  if (act_wchar == strlen(wchar))
    act_wchar = 0;
}

void vobsub_reader_c::identify() {
  uint32_t i;

  mxinfo("File '%s': container: VobSub\n", ti->fname);
  for (i = 0; i < tracks.size(); i++)
    mxinfo("Track ID %u: subtitles (VobSub)\n", i);
}

void vobsub_reader_c::set_headers() {
  uint32_t i;

  for (i = 0; i < tracks.size(); i++)
    if (tracks[i]->packetizer != NULL)
      tracks[i]->packetizer->set_headers();
}
