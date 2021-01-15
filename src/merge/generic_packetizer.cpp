/*
   mkvmerge -- utility for splicing together matroska files
   from component media subtypes

   Distributed under the GPL v2
   see the file COPYING for details
   or visit https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

   the generic_packetizer_c implementation

   Written by Moritz Bunkus <moritz@bunkus.org>.
*/

#include "common/common_pch.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <matroska/KaxContentEncoding.h>
#include <matroska/KaxTag.h>
#include <matroska/KaxTracks.h>
#include <matroska/KaxTrackEntryData.h>
#include <matroska/KaxTrackAudio.h>
#include <matroska/KaxTrackVideo.h>

#include "common/compression.h"
#include "common/container.h"
#include "common/ebml.h"
#include "common/hacks.h"
#include "common/strings/formatting.h"
#include "common/unique_numbers.h"
#include "common/xml/ebml_tags_converter.h"
#include "merge/cluster_helper.h"
#include "merge/filelist.h"
#include "merge/generic_packetizer.h"
#include "merge/generic_reader.h"
#include "merge/output_control.h"
#include "merge/webm.h"

using namespace libmatroska;

#define TRACK_TYPE_TO_DEFTRACK_TYPE(track_type)      \
  (  track_audio == track_type ? DEFTRACK_TYPE_AUDIO \
   : track_video == track_type ? DEFTRACK_TYPE_VIDEO \
   :                             DEFTRACK_TYPE_SUBS)

#define LOOKUP_TRACK_ID(container)                    \
      mtx::includes(container, m_ti.m_id) ? m_ti.m_id \
    : mtx::includes(container, -1)        ? -1        \
    :                                       -2

// ---------------------------------------------------------------------

static std::unordered_map<std::string, bool> s_experimental_status_warning_shown;
std::vector<generic_packetizer_c *> ptzrs_in_header_order;

int generic_packetizer_c::ms_track_number = 0;

generic_packetizer_c::generic_packetizer_c(generic_reader_c *reader,
                                           track_info_c &ti)
  : m_num_packets{}
  , m_next_packet_wo_assigned_timestamp{}
  , m_free_refs{-1}
  , m_next_free_refs{-1}
  , m_enqueued_bytes{}
  , m_safety_last_timestamp{}
  , m_safety_last_duration{}
  , m_track_entry{}
  , m_hserialno{-1}
  , m_htrack_type{-1}
  , m_htrack_default_duration{-1}
  , m_htrack_default_duration_indicates_fields{}
  , m_default_duration_forced{true}
  , m_default_track_warning_printed{}
  , m_huid{}
  , m_htrack_max_add_block_ids{-1}
  , m_haudio_sampling_freq{-1.0}
  , m_haudio_output_sampling_freq{-1.0}
  , m_haudio_channels{-1}
  , m_haudio_bit_depth{-1}
  , m_hvideo_interlaced_flag{-1}
  , m_hvideo_pixel_width{-1}
  , m_hvideo_pixel_height{-1}
  , m_hvideo_display_width{-1}
  , m_hvideo_display_height{-1}
  , m_hvideo_display_unit{ddu_pixels}
  , m_hcompression{COMPRESSION_UNSPECIFIED}
  , m_timestamp_factory_application_mode{TFA_AUTOMATIC}
  , m_last_cue_timestamp{-1}
  , m_has_been_flushed{}
  , m_prevent_lacing{}
  , m_connected_successor{}
  , m_ti{ti}
  , m_reader{reader}
  , m_connected_to{}
  , m_correction_timestamp_offset{}
  , m_append_timestamp_offset{}
  , m_max_timestamp_seen{}
  , m_relaxed_timestamp_checking{}
{
  // Let's see if the user specified timestamp sync for this track.
  if (mtx::includes(m_ti.m_timestamp_syncs, m_ti.m_id))
    m_ti.m_tcsync = m_ti.m_timestamp_syncs[m_ti.m_id];
  else if (mtx::includes(m_ti.m_timestamp_syncs, -1))
    m_ti.m_tcsync = m_ti.m_timestamp_syncs[-1];
  if (0 == m_ti.m_tcsync.factor)
    m_ti.m_tcsync.factor = int64_rational_c{1, 1};

  // Let's see if the user specified "reset timestamps" for this track.
  m_ti.m_reset_timestamps = mtx::includes(m_ti.m_reset_timestamps_specs, m_ti.m_id) || mtx::includes(m_ti.m_reset_timestamps_specs, -1);

  // Let's see if the user has specified which cues he wants for this track.
  if (mtx::includes(m_ti.m_cue_creations, m_ti.m_id))
    m_ti.m_cues = m_ti.m_cue_creations[m_ti.m_id];
  else if (mtx::includes(m_ti.m_cue_creations, -1))
    m_ti.m_cues = m_ti.m_cue_creations[-1];

  // Let's see if the user has given a default track flag for this track.
  if (mtx::includes(m_ti.m_default_track_flags, m_ti.m_id))
    m_ti.m_default_track = m_ti.m_default_track_flags[m_ti.m_id];
  else if (mtx::includes(m_ti.m_default_track_flags, -1))
    m_ti.m_default_track = m_ti.m_default_track_flags[-1];

  // Let's see if the user has given a fix avc fps flag for this track.
  if (mtx::includes(m_ti.m_fix_bitstream_frame_rate_flags, m_ti.m_id))
    m_ti.m_fix_bitstream_frame_rate = m_ti.m_fix_bitstream_frame_rate_flags[m_ti.m_id];
  else if (mtx::includes(m_ti.m_fix_bitstream_frame_rate_flags, -1))
    m_ti.m_fix_bitstream_frame_rate = m_ti.m_fix_bitstream_frame_rate_flags[-1];

  // Let's see if the user has given a forced track flag for this track.
  if (mtx::includes(m_ti.m_forced_track_flags, m_ti.m_id))
    m_ti.m_forced_track = m_ti.m_forced_track_flags[m_ti.m_id];
  else if (mtx::includes(m_ti.m_forced_track_flags, -1))
    m_ti.m_forced_track = m_ti.m_forced_track_flags[-1];

  // Let's see if the user has given a enabled track flag for this track.
  if (mtx::includes(m_ti.m_enabled_track_flags, m_ti.m_id))
    m_ti.m_enabled_track = m_ti.m_enabled_track_flags[m_ti.m_id];
  else if (mtx::includes(m_ti.m_enabled_track_flags, -1))
    m_ti.m_enabled_track = m_ti.m_enabled_track_flags[-1];

  // Let's see if the user has specified a language for this track.
  if (mtx::includes(m_ti.m_languages, m_ti.m_id))
    m_ti.m_language = m_ti.m_languages[m_ti.m_id];
  else if (mtx::includes(m_ti.m_languages, -1))
    m_ti.m_language = m_ti.m_languages[-1];

  // Let's see if the user has specified a sub charset for this track.
  if (mtx::includes(m_ti.m_sub_charsets, m_ti.m_id))
    m_ti.m_sub_charset = m_ti.m_sub_charsets[m_ti.m_id];
  else if (mtx::includes(m_ti.m_sub_charsets, -1))
    m_ti.m_sub_charset = m_ti.m_sub_charsets[-1];

  // Let's see if the user has specified a sub charset for this track.
  if (mtx::includes(m_ti.m_all_tags, m_ti.m_id))
    m_ti.m_tags_file_name = m_ti.m_all_tags[m_ti.m_id];
  else if (mtx::includes(m_ti.m_all_tags, -1))
    m_ti.m_tags_file_name = m_ti.m_all_tags[-1];
  if (!m_ti.m_tags_file_name.empty())
    m_ti.m_tags = mtx::xml::ebml_tags_converter_c::parse_file(m_ti.m_tags_file_name, false);

  // Let's see if the user has specified how this track should be compressed.
  if (mtx::includes(m_ti.m_compression_list, m_ti.m_id))
    m_ti.m_compression = m_ti.m_compression_list[m_ti.m_id];
  else if (mtx::includes(m_ti.m_compression_list, -1))
    m_ti.m_compression = m_ti.m_compression_list[-1];

  // Let's see if the user has specified a name for this track.
  if (mtx::includes(m_ti.m_track_names, m_ti.m_id))
    m_ti.m_track_name = m_ti.m_track_names[m_ti.m_id];
  else if (mtx::includes(m_ti.m_track_names, -1))
    m_ti.m_track_name = m_ti.m_track_names[-1];

  // Let's see if the user has specified external timestamps for this track.
  if (mtx::includes(m_ti.m_all_ext_timestamps, m_ti.m_id))
    m_ti.m_ext_timestamps = m_ti.m_all_ext_timestamps[m_ti.m_id];
  else if (mtx::includes(m_ti.m_all_ext_timestamps, -1))
    m_ti.m_ext_timestamps = m_ti.m_all_ext_timestamps[-1];

  // Let's see if the user has specified an aspect ratio or display dimensions
  // for this track.
  int i = LOOKUP_TRACK_ID(m_ti.m_display_properties);
  if (-2 != i) {
    display_properties_t &dprop = m_ti.m_display_properties[i];
    if (0 > dprop.aspect_ratio) {
      set_video_display_dimensions(dprop.width, dprop.height, generic_packetizer_c::ddu_pixels, OPTION_SOURCE_COMMAND_LINE);
    } else {
      set_video_aspect_ratio(dprop.aspect_ratio, dprop.ar_factor, OPTION_SOURCE_COMMAND_LINE);
      m_ti.m_aspect_ratio_given = true;
    }
  }

  if (m_ti.m_aspect_ratio_given && m_ti.m_display_dimensions_given) {
    if (m_ti.m_aspect_ratio_is_factor)
      mxerror_tid(m_ti.m_fname, m_ti.m_id, fmt::format(Y("Both the aspect ratio factor and '--display-dimensions' were given.\n")));
    else
      mxerror_tid(m_ti.m_fname, m_ti.m_id, fmt::format(Y("Both the aspect ratio and '--display-dimensions' were given.\n")));
  }

  // Let's see if the user has specified a FourCC for this track.
  if (mtx::includes(m_ti.m_all_fourccs, m_ti.m_id))
    m_ti.m_fourcc = m_ti.m_all_fourccs[m_ti.m_id];
  else if (mtx::includes(m_ti.m_all_fourccs, -1))
    m_ti.m_fourcc = m_ti.m_all_fourccs[-1];

  // Let's see if the user has specified cropping parameters for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_pixel_crop_list);
  if (-2 != i)
    set_video_pixel_cropping(m_ti.m_pixel_crop_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified colour matrix for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_colour_matrix_coeff_list);
  if (-2 != i)
    set_video_colour_matrix(m_ti.m_colour_matrix_coeff_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified bits per channel parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_bits_per_channel_list);
  if (-2 != i)
    set_video_bits_per_channel(m_ti.m_bits_per_channel_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified chroma subsampling parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_chroma_subsample_list);
  if (-2 != i)
    set_video_chroma_subsample(m_ti.m_chroma_subsample_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified Cb subsampling parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_cb_subsample_list);
  if (-2 != i)
    set_video_cb_subsample(m_ti.m_cb_subsample_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified chroma siting parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_chroma_siting_list);
  if (-2 != i)
    set_video_chroma_siting(m_ti.m_chroma_siting_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified colour range parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_colour_range_list);
  if (-2 != i)
    set_video_colour_range(m_ti.m_colour_range_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified colour transfer characteristics parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_colour_transfer_list);
  if (-2 != i)
    set_video_colour_transfer_character(m_ti.m_colour_transfer_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified colour primaries parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_colour_primaries_list);
  if (-2 != i)
    set_video_colour_primaries(m_ti.m_colour_primaries_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified max content light parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_max_cll_list);
  if (-2 != i)
    set_video_max_cll(m_ti.m_max_cll_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified max frame light parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_max_fall_list);
  if (-2 != i)
    set_video_max_fall(m_ti.m_max_fall_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified chromaticity coordinates parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_chroma_coordinates_list);
  if (-2 != i)
    set_video_chroma_coordinates(m_ti.m_chroma_coordinates_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified white colour coordinates parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_white_coordinates_list);
  if (-2 != i)
    set_video_white_colour_coordinates(m_ti.m_white_coordinates_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified max luminance parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_max_luminance_list);
  if (-2 != i)
    set_video_max_luminance(m_ti.m_max_luminance_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified min luminance parameter for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_min_luminance_list);
  if (-2 != i)
    set_video_min_luminance(m_ti.m_min_luminance_list[i], OPTION_SOURCE_COMMAND_LINE);

  i = LOOKUP_TRACK_ID(m_ti.m_projection_type_list);
  if (-2 != i)
    set_video_projection_type(m_ti.m_projection_type_list[i], OPTION_SOURCE_COMMAND_LINE);

  i = LOOKUP_TRACK_ID(m_ti.m_projection_private_list);
  if (-2 != i)
    set_video_projection_private(m_ti.m_projection_private_list[i], OPTION_SOURCE_COMMAND_LINE);

  i = LOOKUP_TRACK_ID(m_ti.m_projection_pose_yaw_list);
  if (-2 != i)
    set_video_projection_pose_yaw(m_ti.m_projection_pose_yaw_list[i], OPTION_SOURCE_COMMAND_LINE);

  i = LOOKUP_TRACK_ID(m_ti.m_projection_pose_pitch_list);
  if (-2 != i)
    set_video_projection_pose_pitch(m_ti.m_projection_pose_pitch_list[i], OPTION_SOURCE_COMMAND_LINE);

  i = LOOKUP_TRACK_ID(m_ti.m_projection_pose_roll_list);
  if (-2 != i)
    set_video_projection_pose_roll(m_ti.m_projection_pose_roll_list[i], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified a field order for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_field_order_list);
  if (-2 != i)
    set_video_field_order(m_ti.m_field_order_list[m_ti.m_id], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified a stereo mode for this track.
  i = LOOKUP_TRACK_ID(m_ti.m_stereo_mode_list);
  if (-2 != i)
    set_video_stereo_mode(m_ti.m_stereo_mode_list[m_ti.m_id], OPTION_SOURCE_COMMAND_LINE);

  // Let's see if the user has specified a default duration for this track.
  if (mtx::includes(m_ti.m_default_durations, m_ti.m_id)) {
    m_htrack_default_duration                  = m_ti.m_default_durations[m_ti.m_id].first;
    m_htrack_default_duration_indicates_fields = m_ti.m_default_durations[m_ti.m_id].second;

  } else if (mtx::includes(m_ti.m_default_durations, -1)) {
    m_htrack_default_duration                  = m_ti.m_default_durations[-1].first;
    m_htrack_default_duration_indicates_fields = m_ti.m_default_durations[-1].second;

  } else
    m_default_duration_forced = false;

  // Let's see if the user has set a max_block_add_id
  if (mtx::includes(m_ti.m_max_blockadd_ids, m_ti.m_id))
    m_htrack_max_add_block_ids = m_ti.m_max_blockadd_ids[m_ti.m_id];
  else if (mtx::includes(m_ti.m_max_blockadd_ids, -1))
    m_htrack_max_add_block_ids = m_ti.m_max_blockadd_ids[-1];

  // Let's see if the user has specified a NALU size length for this track.
  if (mtx::includes(m_ti.m_nalu_size_lengths, m_ti.m_id))
    m_ti.m_nalu_size_length = m_ti.m_nalu_size_lengths[m_ti.m_id];
  else if (mtx::includes(m_ti.m_nalu_size_lengths, -1))
    m_ti.m_nalu_size_length = m_ti.m_nalu_size_lengths[-1];

  // Let's see if the user has specified a compression scheme for this track.
  if (COMPRESSION_UNSPECIFIED != m_ti.m_compression)
    m_hcompression = m_ti.m_compression;

  // Set default header values to 'unset'.
  if (!m_reader->m_appending) {
    m_hserialno                             = create_track_number();
    g_packetizers_by_track_num[m_hserialno] = this;
  }

  m_timestamp_factory = timestamp_factory_c::create(m_ti.m_ext_timestamps, m_ti.m_fname, m_ti.m_id);

  // If no external timestamp file but a default duration has been
  // given then create a simple timestamp factory that generates the
  // timestamps for the given FPS.
  if (!m_timestamp_factory && (-1 != m_htrack_default_duration)) {
    auto divisor        = m_htrack_default_duration_indicates_fields ? 2 : 1;
    m_timestamp_factory = timestamp_factory_c::create_fps_factory(m_htrack_default_duration / divisor, m_ti.m_tcsync);
  }
}

generic_packetizer_c::~generic_packetizer_c() {
}

void
generic_packetizer_c::set_tag_track_uid() {
  if (!m_ti.m_tags)
    return;

  mtx::tags::convert_old(*m_ti.m_tags);
  size_t idx_tags;
  for (idx_tags = 0; m_ti.m_tags->ListSize() > idx_tags; ++idx_tags) {
    KaxTag *tag = (KaxTag *)(*m_ti.m_tags)[idx_tags];

    mtx::tags::remove_track_uid_targets(tag);

    GetChild<KaxTagTrackUID>(GetChild<KaxTagTargets>(tag)).SetValue(m_huid);

    fix_mandatory_elements(tag);

    if (!tag->CheckMandatory())
      mxerror(fmt::format(Y("The tags in '{0}' could not be parsed: some mandatory elements are missing.\n"),
                          m_ti.m_tags_file_name != "" ? m_ti.m_tags_file_name : m_ti.m_fname));
  }
}

bool
generic_packetizer_c::set_uid(uint64_t uid) {
  if (!is_unique_number(uid, UNIQUE_TRACK_IDS))
    return false;

  add_unique_number(uid, UNIQUE_TRACK_IDS);
  m_huid = uid;
  if (m_track_entry)
    GetChild<KaxTrackUID>(m_track_entry).SetValue(m_huid);

  return true;
}

void
generic_packetizer_c::set_track_type(int type,
                                     timestamp_factory_application_e tfa_mode) {
  m_htrack_type = type;

  if (CUE_STRATEGY_UNSPECIFIED == get_cue_creation())
    m_ti.m_cues = track_audio    == type ? CUE_STRATEGY_SPARSE
                : track_video    == type ? CUE_STRATEGY_IFRAMES
                : track_subtitle == type ? CUE_STRATEGY_IFRAMES
                :                          CUE_STRATEGY_UNSPECIFIED;

  if (track_audio == type)
    m_reader->m_num_audio_tracks++;

  else if (track_video == type)
    m_reader->m_num_video_tracks++;

  else
    m_reader->m_num_subtitle_tracks++;

  g_cluster_helper->register_new_packetizer(*this);

  if (   (TFA_AUTOMATIC == tfa_mode)
      && (TFA_AUTOMATIC == m_timestamp_factory_application_mode))
    m_timestamp_factory_application_mode
      = (track_video    == type) ? TFA_FULL_QUEUEING
      : (track_subtitle == type) ? TFA_IMMEDIATE
      : (track_buttons  == type) ? TFA_IMMEDIATE
      :                            TFA_FULL_QUEUEING;

  else if (TFA_AUTOMATIC != tfa_mode)
    m_timestamp_factory_application_mode = tfa_mode;

  if (m_timestamp_factory && (track_video != type) && (track_audio != type))
    m_timestamp_factory->set_preserve_duration(true);
}

void
generic_packetizer_c::set_track_name(const std::string &name) {
  m_ti.m_track_name = name;
  if (m_track_entry && !name.empty())
    GetChild<KaxTrackName>(m_track_entry).SetValueUTF8(m_ti.m_track_name);
}

void
generic_packetizer_c::set_codec_id(const std::string &id) {
  m_hcodec_id = id;
  if (m_track_entry && !id.empty())
    GetChild<KaxCodecID>(m_track_entry).SetValue(m_hcodec_id);
}

void
generic_packetizer_c::set_codec_private(memory_cptr const &buffer) {
  if (buffer && buffer->get_size()) {
    m_hcodec_private = buffer->clone();

    if (m_track_entry)
      GetChild<KaxCodecPrivate>(*m_track_entry).CopyBuffer(static_cast<binary *>(m_hcodec_private->get_buffer()), m_hcodec_private->get_size());

  } else
    m_hcodec_private.reset();
}

void
generic_packetizer_c::set_codec_name(std::string const &name) {
  m_hcodec_name = name;
  if (m_track_entry && !name.empty())
    GetChild<KaxCodecName>(m_track_entry).SetValueUTF8(m_hcodec_name);
}

void
generic_packetizer_c::set_track_default_duration(int64_t def_dur,
                                                 bool force) {
  if (!force && m_default_duration_forced)
    return;

  m_htrack_default_duration = boost::rational_cast<int64_t>(m_ti.m_tcsync.factor * def_dur);

  if (m_track_entry) {
    if (m_htrack_default_duration)
      GetChild<KaxTrackDefaultDuration>(m_track_entry).SetValue(m_htrack_default_duration);
    else
      DeleteChildren<KaxTrackDefaultDuration>(m_track_entry);
  }
}

void
generic_packetizer_c::set_track_max_additionals(int max_add_block_ids) {
  m_htrack_max_add_block_ids = max_add_block_ids;
  if (m_track_entry)
    GetChild<KaxMaxBlockAdditionID>(m_track_entry).SetValue(max_add_block_ids);
}

int64_t
generic_packetizer_c::get_track_default_duration()
  const {
  return m_htrack_default_duration;
}

void
generic_packetizer_c::set_track_forced_flag(bool forced_track) {
  m_ti.m_forced_track = forced_track;
  if (m_track_entry)
    GetChild<KaxTrackFlagForced>(m_track_entry).SetValue(forced_track ? 1 : 0);
}

void
generic_packetizer_c::set_track_enabled_flag(bool enabled_track) {
  m_ti.m_enabled_track = enabled_track;
  if (m_track_entry)
    GetChild<KaxTrackFlagEnabled>(m_track_entry).SetValue(enabled_track ? 1 : 0);
}

void
generic_packetizer_c::set_track_seek_pre_roll(timestamp_c const &seek_pre_roll) {
  m_seek_pre_roll = seek_pre_roll;
  if (m_track_entry)
    GetChild<KaxSeekPreRoll>(m_track_entry).SetValue(seek_pre_roll.to_ns());
}

void
generic_packetizer_c::set_codec_delay(timestamp_c const &codec_delay) {
  m_codec_delay = codec_delay;
  if (m_track_entry)
    GetChild<KaxCodecDelay>(m_track_entry).SetValue(codec_delay.to_ns());
}

void
generic_packetizer_c::set_audio_sampling_freq(double freq) {
  m_haudio_sampling_freq = freq;
  if (m_track_entry)
    GetChild<KaxAudioSamplingFreq>(GetChild<KaxTrackAudio>(m_track_entry)).SetValue(m_haudio_sampling_freq);
}

void
generic_packetizer_c::set_audio_output_sampling_freq(double freq) {
  m_haudio_output_sampling_freq = freq;
  if (m_track_entry)
    GetChild<KaxAudioOutputSamplingFreq>(GetChild<KaxTrackAudio>(m_track_entry)).SetValue(m_haudio_output_sampling_freq);
}

void
generic_packetizer_c::set_audio_channels(int channels) {
  m_haudio_channels = channels;
  if (m_track_entry)
    GetChild<KaxAudioChannels>(GetChild<KaxTrackAudio>(*m_track_entry)).SetValue(m_haudio_channels);
}

void
generic_packetizer_c::set_audio_bit_depth(int bit_depth) {
  m_haudio_bit_depth = bit_depth;
  if (m_track_entry)
    GetChild<KaxAudioBitDepth>(GetChild<KaxTrackAudio>(*m_track_entry)).SetValue(m_haudio_bit_depth);
}

void
generic_packetizer_c::set_video_interlaced_flag(bool interlaced) {
  m_hvideo_interlaced_flag = interlaced ? 1 : 0;
  if (m_track_entry)
    GetChild<KaxVideoFlagInterlaced>(GetChild<KaxTrackVideo>(*m_track_entry)).SetValue(m_hvideo_interlaced_flag);
}

void
generic_packetizer_c::set_video_pixel_width(int width) {
  m_hvideo_pixel_width = width;
  if (m_track_entry)
    GetChild<KaxVideoPixelWidth>(GetChild<KaxTrackVideo>(*m_track_entry)).SetValue(m_hvideo_pixel_width);
}

void
generic_packetizer_c::set_video_pixel_height(int height) {
  m_hvideo_pixel_height = height;
  if (m_track_entry)
    GetChild<KaxVideoPixelHeight>(GetChild<KaxTrackVideo>(*m_track_entry)).SetValue(m_hvideo_pixel_height);
}

void
generic_packetizer_c::set_video_pixel_dimensions(int width,
                                                 int height) {
  set_video_pixel_width(width);
  set_video_pixel_height(height);
}

void
generic_packetizer_c::set_video_display_width(int width) {
  m_hvideo_display_width = width;
  if (m_track_entry)
    GetChild<KaxVideoDisplayWidth>(GetChild<KaxTrackVideo>(*m_track_entry)).SetValue(m_hvideo_display_width);
}

void
generic_packetizer_c::set_video_display_height(int height) {
  m_hvideo_display_height = height;
  if (m_track_entry)
    GetChild<KaxVideoDisplayHeight>(GetChild<KaxTrackVideo>(*m_track_entry)).SetValue(m_hvideo_display_height);
}

void
generic_packetizer_c::set_video_display_unit(int unit) {
  m_hvideo_display_unit = unit;
  if (   m_track_entry
      && (   (unit != ddu_pixels)
          || (unit != FindChildValue<KaxVideoDisplayUnit>(GetChild<KaxTrackVideo>(*m_track_entry), ddu_pixels))))
    GetChild<KaxVideoDisplayUnit>(GetChild<KaxTrackVideo>(*m_track_entry)).SetValue(m_hvideo_display_unit);
}

void
generic_packetizer_c::set_video_display_dimensions(int width,
                                                   int height,
                                                   int unit,
                                                   option_source_e source) {
  if (display_dimensions_or_aspect_ratio_set() && (m_ti.m_display_dimensions_source >= source))
    return;

  m_ti.m_display_width             = width;
  m_ti.m_display_height            = height;
  m_ti.m_display_unit              = unit;
  m_ti.m_display_dimensions_source = source;
  m_ti.m_display_dimensions_given  = true;
  m_ti.m_aspect_ratio_given        = false;

  set_video_display_width(width);
  set_video_display_height(height);
  set_video_display_unit(unit);
}

void
generic_packetizer_c::set_video_aspect_ratio(double aspect_ratio,
                                             bool is_factor,
                                             option_source_e source) {
  if (display_dimensions_or_aspect_ratio_set() && (m_ti.m_display_dimensions_source >= source))
    return;

  m_ti.m_aspect_ratio              = aspect_ratio;
  m_ti.m_aspect_ratio_is_factor    = is_factor;
  m_ti.m_display_unit              = ddu_pixels;
  m_ti.m_display_dimensions_source = source;
  m_ti.m_display_dimensions_given  = false;
  m_ti.m_aspect_ratio_given        = true;
}

void
generic_packetizer_c::set_as_default_track(int type,
                                           int priority) {
  if (g_default_tracks_priority[type] < priority) {
    g_default_tracks_priority[type] = priority;
    g_default_tracks[type]          = m_hserialno;

  } else if (   (DEFAULT_TRACK_PRIORITY_CMDLINE == priority)
             && (g_default_tracks[type] != m_hserialno)
             && !m_default_track_warning_printed) {
    mxwarn(fmt::format(Y("Another default track for {0} tracks has already been set. The 'default' flag for track {1} of '{2}' will not be set.\n"),
                       DEFTRACK_TYPE_AUDIO == type ? "audio" : DEFTRACK_TYPE_VIDEO == type ? "video" : "subtitle", m_ti.m_id, m_ti.m_fname));
    m_default_track_warning_printed = true;
  }
}

void
generic_packetizer_c::set_language(mtx::bcp47::language_c const &language) {
  if (!language.is_valid())
    return;

  m_ti.m_language = language;

  if (!m_track_entry || !language.is_valid())
    return;

  if (language.has_valid_iso639_code())
    GetChild<KaxTrackLanguage>(m_track_entry).SetValue(language.get_iso639_alpha_3_code());
  if (!mtx::bcp47::language_c::is_disabled())
    GetChild<KaxLanguageIETF>(m_track_entry).SetValue(language.format());
}

void
generic_packetizer_c::set_video_pixel_cropping(int left,
                                               int top,
                                               int right,
                                               int bottom,
                                               option_source_e source) {
  m_ti.m_pixel_cropping.set(pixel_crop_t{left, top, right, bottom}, source);

  if (m_track_entry) {
    KaxTrackVideo &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto crop            = m_ti.m_pixel_cropping.get();

    GetChild<KaxVideoPixelCropLeft  >(video).SetValue(crop.left);
    GetChild<KaxVideoPixelCropTop   >(video).SetValue(crop.top);
    GetChild<KaxVideoPixelCropRight >(video).SetValue(crop.right);
    GetChild<KaxVideoPixelCropBottom>(video).SetValue(crop.bottom);
  }
}

void
generic_packetizer_c::set_video_colour_matrix(int matrix_index,
                                              option_source_e source) {
  m_ti.m_colour_matrix_coeff.set(matrix_index, source);
  if (   m_track_entry
      && (matrix_index >= 0)
      && (matrix_index <= 10)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoColourMatrix>(color).SetValue(m_ti.m_colour_matrix_coeff.get());
  }
}

void
generic_packetizer_c::set_video_bits_per_channel(int num_bits,
                                                 option_source_e source) {
  m_ti.m_bits_per_channel.set(num_bits, source);
  if (m_track_entry && (num_bits >= 0)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoBitsPerChannel>(color).SetValue(m_ti.m_bits_per_channel.get());
  }
}

void
generic_packetizer_c::set_video_chroma_subsample(const chroma_subsample_t &subsample,
                                                 option_source_e source) {
  m_ti.m_chroma_subsample.set(chroma_subsample_t(subsample.hori, subsample.vert), source);
  if (   m_track_entry
      && (   (subsample.hori >= 0)
          || (subsample.vert >= 0))) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    if (subsample.hori >= 0)
      GetChild<KaxVideoChromaSubsampHorz>(color).SetValue(subsample.hori);
    if (subsample.vert >= 0)
      GetChild<KaxVideoChromaSubsampVert>(color).SetValue(subsample.vert);
  }
}

void
generic_packetizer_c::set_video_cb_subsample(const cb_subsample_t &subsample,
                                             option_source_e source) {
  m_ti.m_cb_subsample.set(cb_subsample_t(subsample.hori, subsample.vert), source);
  if (   m_track_entry
      && (   (subsample.hori >= 0)
          || (subsample.vert >= 0))) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    if (subsample.hori >= 0)
      GetChild<KaxVideoCbSubsampHorz>(color).SetValue(subsample.hori);
    if (subsample.vert >= 0)
      GetChild<KaxVideoCbSubsampVert>(color).SetValue(subsample.vert);
  }
}

void
generic_packetizer_c::set_video_chroma_siting(const chroma_siting_t &siting,
                                              option_source_e source) {
  m_ti.m_chroma_siting.set(chroma_siting_t(siting.hori, siting.vert), source);
  if (   m_track_entry
      && (   ((siting.hori >= 0) && (siting.hori <= 2))
          || ((siting.vert >= 0) && (siting.hori <= 2)))) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    if ((siting.hori >= 0) && (siting.hori <= 2))
      GetChild<KaxVideoChromaSitHorz>(color).SetValue(siting.hori);
    if ((siting.vert >= 0) && (siting.hori <= 2))
      GetChild<KaxVideoChromaSitVert>(color).SetValue(siting.vert);
  }
}

void
generic_packetizer_c::set_video_colour_range(int range,
                                             option_source_e source) {
  m_ti.m_colour_range.set(range, source);
  if (m_track_entry && (range >= 0) && (range <= 3)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoColourRange>(color).SetValue(range);
  }
}

void
generic_packetizer_c::set_video_colour_transfer_character(int transfer_index,
                                                          option_source_e source) {
  m_ti.m_colour_transfer.set(transfer_index, source);
  if (m_track_entry && (transfer_index >= 0) && (transfer_index <= 18)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoColourTransferCharacter>(color).SetValue(transfer_index);
  }
}

void
generic_packetizer_c::set_video_colour_primaries(int primary_index,
                                                 option_source_e source) {
  m_ti.m_colour_primaries.set(primary_index, source);
  if (     m_track_entry
      && (primary_index >= 0)
      && ((primary_index <= 10) || (primary_index == 22))) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoColourPrimaries>(color).SetValue(primary_index);
  }
}

void
generic_packetizer_c::set_video_max_cll(int max_cll,
                                        option_source_e source) {
  m_ti.m_max_cll.set(max_cll, source);
  if (m_track_entry && (max_cll >= 0)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoColourMaxCLL>(color).SetValue(max_cll);
  }
}

void
generic_packetizer_c::set_video_max_fall(int max_fall,
                                         option_source_e source) {
  m_ti.m_max_fall.set(max_fall, source);
  if (m_track_entry && (max_fall >= 0)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoColourMaxFALL>(color).SetValue(max_fall);
  }
}

void
generic_packetizer_c::set_video_chroma_coordinates(chroma_coordinates_t const &coordinates,
                                                   option_source_e source) {
  m_ti.m_chroma_coordinates.set(coordinates, source);
  if (   m_track_entry
      && (   ((coordinates.red_x   >= 0) && (coordinates.red_x   <= 1))
          || ((coordinates.red_y   >= 0) && (coordinates.red_y   <= 1))
          || ((coordinates.green_x >= 0) && (coordinates.green_x <= 1))
          || ((coordinates.green_y >= 0) && (coordinates.green_y <= 1))
          || ((coordinates.blue_x  >= 0) && (coordinates.blue_x  <= 1))
          || ((coordinates.blue_y  >= 0) && (coordinates.blue_y  <= 1)))) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    if ((coordinates.red_x >= 0) && (coordinates.red_x <= 1))
      GetChild<KaxVideoRChromaX>(color).SetValue(coordinates.red_x);
    if ((coordinates.red_y >= 0) && (coordinates.red_y <= 1))
      GetChild<KaxVideoRChromaY>(color).SetValue(coordinates.red_y);
    if ((coordinates.green_x >= 0) && (coordinates.green_x <= 1))
      GetChild<KaxVideoGChromaX>(color).SetValue(coordinates.green_x);
    if ((coordinates.green_y >= 0) && (coordinates.green_y <= 1))
      GetChild<KaxVideoGChromaY>(color).SetValue(coordinates.green_y);
    if ((coordinates.blue_x >= 0) && (coordinates.blue_x <= 1))
      GetChild<KaxVideoBChromaX>(color).SetValue(coordinates.blue_x);
    if ((coordinates.blue_y >= 0) && (coordinates.blue_y <= 1))
      GetChild<KaxVideoBChromaY>(color).SetValue(coordinates.blue_y);
  }
}

void
generic_packetizer_c::set_video_white_colour_coordinates(white_colour_coordinates_t const &coordinates,
                                                         option_source_e source) {
  m_ti.m_white_coordinates.set(white_colour_coordinates_t(coordinates.x, coordinates.y), source);
  if (   m_track_entry
      && (   ((coordinates.x >= 0) && (coordinates.x <= 1))
          || ((coordinates.y >= 0) && (coordinates.y <= 1)))) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    if ((coordinates.x >= 0) && (coordinates.x <= 1))
      GetChild<KaxVideoWhitePointChromaX>(color).SetValue(coordinates.x);
    if ((coordinates.y >= 0) && (coordinates.y <= 1))
      GetChild<KaxVideoWhitePointChromaY>(color).SetValue(coordinates.y);
  }
}

void
generic_packetizer_c::set_video_max_luminance(double luminance,
                                              option_source_e source) {
  m_ti.m_max_luminance.set(luminance, source);
  if (   m_track_entry
      && (luminance >= 0)
      && (luminance <= 9999.99)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoLuminanceMax>(color).SetValue(luminance);
  }
}

void
generic_packetizer_c::set_video_min_luminance(double luminance,
                                              option_source_e source) {
  m_ti.m_min_luminance.set(luminance, source);
  if (m_track_entry && (luminance >= 0) && (luminance <= 999.9999)) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    auto &color = GetChild<KaxVideoColour>(video);
    GetChild<KaxVideoLuminanceMin>(color).SetValue(luminance);
  }
}

void
generic_packetizer_c::set_video_projection_type(uint64_t value,
                                                option_source_e source) {
  m_ti.m_projection_type.set(value, source);
  if (m_track_entry) {
    auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(GetChild<KaxTrackVideo>(m_track_entry));
    GetChild<KaxVideoProjectionType>(projection).SetValue(value);
  }
}

void
generic_packetizer_c::set_video_projection_private(memory_cptr const &value,
                                                   option_source_e source) {
  m_ti.m_projection_private.set(value, source);
  if (m_track_entry && value) {
    auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(GetChild<KaxTrackVideo>(m_track_entry));
    if (value->get_size())
      GetChild<KaxVideoProjectionPrivate>(projection).CopyBuffer(value->get_buffer(), value->get_size());
    else
      DeleteChildren<KaxVideoProjectionPrivate>(projection);
  }
}

void
generic_packetizer_c::set_video_projection_pose_yaw(double value,
                                                    option_source_e source) {
  m_ti.m_projection_pose_yaw.set(value, source);
  if (m_track_entry && (value >= 0)) {
    auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(GetChild<KaxTrackVideo>(m_track_entry));
    GetChild<KaxVideoProjectionPoseYaw>(projection).SetValue(value);
  }
}

void
generic_packetizer_c::set_video_projection_pose_pitch(double value,
                                                      option_source_e source) {
  m_ti.m_projection_pose_pitch.set(value, source);
  if (m_track_entry && (value >= 0)) {
    auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(GetChild<KaxTrackVideo>(m_track_entry));
    GetChild<KaxVideoProjectionPosePitch>(projection).SetValue(value);
  }
}

void
generic_packetizer_c::set_video_projection_pose_roll(double value,
                                                     option_source_e source) {
  m_ti.m_projection_pose_roll.set(value, source);
  if (m_track_entry && (value >= 0)) {
    auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(GetChild<KaxTrackVideo>(m_track_entry));
    GetChild<KaxVideoProjectionPoseRoll>(projection).SetValue(value);
  }
}

void
generic_packetizer_c::set_video_pixel_cropping(const pixel_crop_t &cropping,
                                               option_source_e source) {
  set_video_pixel_cropping(cropping.left, cropping.top, cropping.right, cropping.bottom, source);
}

void
generic_packetizer_c::set_video_field_order(uint64_t order,
                                            option_source_e source) {
  m_ti.m_field_order.set(order, source);
  if (m_track_entry && m_ti.m_field_order) {
    auto &video = GetChild<KaxTrackVideo>(m_track_entry);
    GetChild<KaxVideoFieldOrder>(video).SetValue(m_ti.m_field_order.get());
  }
}

void
generic_packetizer_c::set_video_stereo_mode(stereo_mode_c::mode stereo_mode,
                                            option_source_e source) {
  m_ti.m_stereo_mode.set(stereo_mode, source);

  if (m_track_entry && (stereo_mode_c::unspecified != m_ti.m_stereo_mode.get()))
    set_video_stereo_mode_impl(GetChild<KaxTrackVideo>(*m_track_entry), m_ti.m_stereo_mode.get());
}

void
generic_packetizer_c::set_video_stereo_mode_impl(EbmlMaster &video,
                                                 stereo_mode_c::mode stereo_mode) {
  GetChild<KaxVideoStereoMode>(video).SetValue(stereo_mode);
}

void
generic_packetizer_c::set_video_colour_space(memory_cptr const &value,
                                             option_source_e source) {
  m_ti.m_colour_space.set(value, source);

  if (m_track_entry && value && value->get_size())
    GetChild<KaxVideoColourSpace>(m_track_entry).CopyBuffer(value->get_buffer(), value->get_size());
}

void
generic_packetizer_c::set_block_addition_mappings(std::vector<block_addition_mapping_t> const &mappings) {
  m_block_addition_mappings = mappings;
  apply_block_addition_mappings();
}

void
generic_packetizer_c::set_headers() {
  if (0 < m_connected_to) {
    mxerror(fmt::format("generic_packetizer_c::set_headers(): connected_to > 0 (type: {0}). {1}\n", typeid(*this).name(), BUGMSG));
    return;
  }

  bool found = false;
  size_t idx;
  for (idx = 0; ptzrs_in_header_order.size() > idx; ++idx)
    if (this == ptzrs_in_header_order[idx]) {
      found = true;
      break;
    }

  if (!found)
    ptzrs_in_header_order.push_back(this);

  if (!m_track_entry) {
    m_track_entry    = !g_kax_last_entry ? &GetChild<KaxTrackEntry>(*g_kax_tracks) : &GetNextChild<KaxTrackEntry>(*g_kax_tracks, *g_kax_last_entry);
    g_kax_last_entry = m_track_entry;
    m_track_entry->SetGlobalTimecodeScale((int64_t)g_timestamp_scale);
  }

  GetChild<KaxTrackNumber>(m_track_entry).SetValue(m_hserialno);

  if (0 == m_huid)
    m_huid = create_unique_number(UNIQUE_TRACK_IDS);

  GetChild<KaxTrackUID>(m_track_entry).SetValue(m_huid);

  if (-1 != m_htrack_type)
    GetChild<KaxTrackType>(m_track_entry).SetValue(m_htrack_type);

  if (!m_hcodec_id.empty())
    GetChild<KaxCodecID>(m_track_entry).SetValue(m_hcodec_id);

  if (m_hcodec_private)
    GetChild<KaxCodecPrivate>(*m_track_entry).CopyBuffer(static_cast<binary *>(m_hcodec_private->get_buffer()), m_hcodec_private->get_size());

  if (!m_hcodec_name.empty())
    GetChild<KaxCodecName>(m_track_entry).SetValueUTF8(m_hcodec_name);

  if (!outputting_webm()) {
    if (-1 != m_htrack_max_add_block_ids)
      GetChild<KaxMaxBlockAdditionID>(m_track_entry).SetValue(m_htrack_max_add_block_ids);
  }

  if (m_timestamp_factory)
    m_htrack_default_duration = (int64_t)m_timestamp_factory->get_default_duration(m_htrack_default_duration);
  if (-1.0 != m_htrack_default_duration)
    GetChild<KaxTrackDefaultDuration>(m_track_entry).SetValue(m_htrack_default_duration);

  idx = TRACK_TYPE_TO_DEFTRACK_TYPE(m_htrack_type);

  if (!m_ti.m_default_track.has_value())
    set_as_default_track(idx, DEFAULT_TRACK_PRIORITY_FROM_TYPE);
  else if (m_ti.m_default_track.value())
    set_as_default_track(idx, DEFAULT_TRACK_PRIORITY_CMDLINE);
  else if (g_default_tracks[idx] == m_hserialno)
    g_default_tracks[idx] = 0;

  auto iso639_alpha_3_code = m_ti.m_language.has_valid_iso639_code() ? m_ti.m_language.get_iso639_alpha_3_code() : g_default_language.get_iso639_alpha_3_code();
  auto language            = m_ti.m_language.is_valid()              ? m_ti.m_language                           : g_default_language;
  GetChild<KaxTrackLanguage>(m_track_entry).SetValue(iso639_alpha_3_code);
  if (!mtx::bcp47::language_c::is_disabled())
    GetChild<KaxLanguageIETF>(m_track_entry).SetValue(language.format());

  if (!m_ti.m_track_name.empty())
    GetChild<KaxTrackName>(m_track_entry).SetValueUTF8(m_ti.m_track_name);

  if (m_ti.m_forced_track.has_value())
    GetChild<KaxTrackFlagForced>(m_track_entry).SetValue(m_ti.m_forced_track.value() ? 1 : 0);

  if (m_ti.m_enabled_track.has_value())
    GetChild<KaxTrackFlagEnabled>(m_track_entry).SetValue(m_ti.m_enabled_track.value() ? 1 : 0);

  if (m_seek_pre_roll.valid())
    GetChild<KaxSeekPreRoll>(m_track_entry).SetValue(m_seek_pre_roll.to_ns());

  if (m_codec_delay.valid())
    GetChild<KaxCodecDelay>(m_track_entry).SetValue(m_codec_delay.to_ns());

  if (track_video == m_htrack_type) {
    KaxTrackVideo &video = GetChild<KaxTrackVideo>(m_track_entry);

    if (-1 != m_hvideo_interlaced_flag)
      set_video_interlaced_flag(m_hvideo_interlaced_flag != 0);

    if ((-1 != m_hvideo_pixel_height) && (-1 != m_hvideo_pixel_width)) {
      if ((-1 == m_hvideo_display_width) || (-1 == m_hvideo_display_height) || m_ti.m_aspect_ratio_given || m_ti.m_display_dimensions_given) {
        if (m_ti.m_display_dimensions_given) {
          m_hvideo_display_width  = m_ti.m_display_width;
          m_hvideo_display_height = m_ti.m_display_height;

        } else {
          if (!m_ti.m_aspect_ratio_given)
            m_ti.m_aspect_ratio = static_cast<double>(m_hvideo_pixel_width)                       / m_hvideo_pixel_height;

          else if (m_ti.m_aspect_ratio_is_factor)
            m_ti.m_aspect_ratio = static_cast<double>(m_hvideo_pixel_width) * m_ti.m_aspect_ratio / m_hvideo_pixel_height;

          if (m_ti.m_aspect_ratio > (static_cast<double>(m_hvideo_pixel_width) / m_hvideo_pixel_height)) {
            m_hvideo_display_width  = std::llround(m_hvideo_pixel_height * m_ti.m_aspect_ratio);
            m_hvideo_display_height = m_hvideo_pixel_height;

          } else {
            m_hvideo_display_width  = m_hvideo_pixel_width;
            m_hvideo_display_height = std::llround(m_hvideo_pixel_width / m_ti.m_aspect_ratio);
          }
        }
      }

      GetChild<KaxVideoPixelWidth   >(video).SetValue(m_hvideo_pixel_width);
      GetChild<KaxVideoPixelHeight  >(video).SetValue(m_hvideo_pixel_height);

      GetChild<KaxVideoDisplayWidth >(video).SetValue(m_hvideo_display_width);
      GetChild<KaxVideoDisplayHeight>(video).SetValue(m_hvideo_display_height);

      GetChild<KaxVideoDisplayWidth >(video).SetDefaultSize(4);
      GetChild<KaxVideoDisplayHeight>(video).SetDefaultSize(4);

      if (m_hvideo_display_unit != ddu_pixels)
        GetChild<KaxVideoDisplayUnit>(video).SetValue(m_hvideo_display_unit);

      if (m_ti.m_colour_space)
        GetChild<KaxVideoColourSpace>(video).CopyBuffer(m_ti.m_colour_space.get()->get_buffer(), m_ti.m_colour_space.get()->get_size());

      if (m_ti.m_pixel_cropping) {
        auto crop = m_ti.m_pixel_cropping.get();
        GetChild<KaxVideoPixelCropLeft  >(video).SetValue(crop.left);
        GetChild<KaxVideoPixelCropTop   >(video).SetValue(crop.top);
        GetChild<KaxVideoPixelCropRight >(video).SetValue(crop.right);
        GetChild<KaxVideoPixelCropBottom>(video).SetValue(crop.bottom);
      }

      if (m_ti.m_colour_matrix_coeff) {
        int colour_matrix = m_ti.m_colour_matrix_coeff.get();
        auto &colour      = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoColourMatrix>(colour).SetValue(colour_matrix);
      }

      if (m_ti.m_bits_per_channel) {
        int bits     = m_ti.m_bits_per_channel.get();
        auto &colour = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoBitsPerChannel>(colour).SetValue(bits);
      }

      if (m_ti.m_chroma_subsample) {
        auto const &subsample = m_ti.m_chroma_subsample.get();
        auto &colour          = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoChromaSubsampHorz>(colour).SetValue(subsample.hori);
        GetChild<KaxVideoChromaSubsampVert>(colour).SetValue(subsample.vert);
      }

      if (m_ti.m_cb_subsample) {
        auto const &subsample = m_ti.m_cb_subsample.get();
        auto &colour          = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoCbSubsampHorz>(colour).SetValue(subsample.hori);
        GetChild<KaxVideoCbSubsampVert>(colour).SetValue(subsample.vert);
      }

      if (m_ti.m_chroma_siting) {
        auto const &siting = m_ti.m_chroma_siting.get();
        auto &colour       = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoChromaSitHorz>(colour).SetValue(siting.hori);
        GetChild<KaxVideoChromaSitVert>(colour).SetValue(siting.vert);
      }

      if (m_ti.m_colour_range) {
        int range_index = m_ti.m_colour_range.get();
        auto &colour    = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoColourRange>(colour).SetValue(range_index);
      }

      if (m_ti.m_colour_transfer) {
        int transfer_index = m_ti.m_colour_transfer.get();
        auto &colour       = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoColourTransferCharacter>(colour).SetValue(transfer_index);
      }

      if (m_ti.m_colour_primaries) {
        int primary_index = m_ti.m_colour_primaries.get();
        auto &colour      = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoColourPrimaries>(colour).SetValue(primary_index);
      }

      if (m_ti.m_max_cll) {
        int cll_index = m_ti.m_max_cll.get();
        auto &colour  = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoColourMaxCLL>(colour).SetValue(cll_index);
      }

      if (m_ti.m_max_fall) {
        int fall_index = m_ti.m_max_fall.get();
        auto &colour   = GetChild<KaxVideoColour>(video);
        GetChild<KaxVideoColourMaxFALL>(colour).SetValue(fall_index);
      }

      if (m_ti.m_chroma_coordinates) {
        auto const &coordinates = m_ti.m_chroma_coordinates.get();
        auto &colour            = GetChild<KaxVideoColour>(video);
        auto &master_meta       = GetChild<KaxVideoColourMasterMeta>(colour);
        GetChild<KaxVideoRChromaX>(master_meta).SetValue(coordinates.red_x);
        GetChild<KaxVideoRChromaY>(master_meta).SetValue(coordinates.red_y);
        GetChild<KaxVideoGChromaX>(master_meta).SetValue(coordinates.green_x);
        GetChild<KaxVideoGChromaY>(master_meta).SetValue(coordinates.green_y);
        GetChild<KaxVideoBChromaX>(master_meta).SetValue(coordinates.blue_x);
        GetChild<KaxVideoBChromaY>(master_meta).SetValue(coordinates.blue_y);
      }

      if (m_ti.m_white_coordinates) {
        auto const &coordinates = m_ti.m_white_coordinates.get();
        auto &colour            = GetChild<KaxVideoColour>(video);
        auto &master_meta       = GetChild<KaxVideoColourMasterMeta>(colour);
        GetChild<KaxVideoWhitePointChromaX>(master_meta).SetValue(coordinates.x);
        GetChild<KaxVideoWhitePointChromaY>(master_meta).SetValue(coordinates.y);
      }

      if (m_ti.m_max_luminance) {
        auto luminance    = m_ti.m_max_luminance.get();
        auto &colour      = GetChild<KaxVideoColour>(video);
        auto &master_meta = GetChild<KaxVideoColourMasterMeta>(colour);
        GetChild<KaxVideoLuminanceMax>(master_meta).SetValue(luminance);
      }

      if (m_ti.m_min_luminance) {
        auto luminance    = m_ti.m_min_luminance.get();
        auto &colour      = GetChild<KaxVideoColour>(video);
        auto &master_meta = GetChild<KaxVideoColourMasterMeta>(colour);
        GetChild<KaxVideoLuminanceMin>(master_meta).SetValue(luminance);
      }

      if (m_ti.m_projection_type) {
        auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(video);
        GetChild<KaxVideoProjectionType>(projection).SetValue(m_ti.m_projection_type.get());
      }

      if (m_ti.m_projection_private) {
        auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(video);
        GetChild<KaxVideoProjectionPrivate>(projection).CopyBuffer(m_ti.m_projection_private.get()->get_buffer(), m_ti.m_projection_private.get()->get_size());
      }

      if (m_ti.m_projection_pose_yaw) {
        auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(video);
        GetChild<KaxVideoProjectionPoseYaw>(projection).SetValue(m_ti.m_projection_pose_yaw.get());
      }

      if (m_ti.m_projection_pose_pitch) {
        auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(video);
        GetChild<KaxVideoProjectionPosePitch>(projection).SetValue(m_ti.m_projection_pose_pitch.get());
      }

      if (m_ti.m_projection_pose_roll) {
        auto &projection = GetChildEmptyIfNew<KaxVideoProjection>(video);
        GetChild<KaxVideoProjectionPoseRoll>(projection).SetValue(m_ti.m_projection_pose_roll.get());
      }

      if (m_ti.m_field_order)
        GetChild<KaxVideoFieldOrder>(video).SetValue(m_ti.m_field_order.get());

      if (m_ti.m_stereo_mode && (stereo_mode_c::unspecified != m_ti.m_stereo_mode.get()))
        set_video_stereo_mode_impl(video, m_ti.m_stereo_mode.get());
    }

  } else if (track_audio == m_htrack_type) {
    KaxTrackAudio &audio = GetChild<KaxTrackAudio>(m_track_entry);

    if (-1   != m_haudio_sampling_freq)
      GetChild<KaxAudioSamplingFreq>(audio).SetValue(m_haudio_sampling_freq);

    if (-1.0 != m_haudio_output_sampling_freq)
      GetChild<KaxAudioOutputSamplingFreq>(audio).SetValue(m_haudio_output_sampling_freq);

    if (-1   != m_haudio_channels)
      GetChild<KaxAudioChannels>(audio).SetValue(m_haudio_channels);

    if (-1   != m_haudio_bit_depth)
      GetChild<KaxAudioBitDepth>(audio).SetValue(m_haudio_bit_depth);

  } else if (track_buttons == m_htrack_type) {
    if ((-1 != m_hvideo_pixel_height) && (-1 != m_hvideo_pixel_width)) {
      KaxTrackVideo &video = GetChild<KaxTrackVideo>(m_track_entry);

      GetChild<KaxVideoPixelWidth >(video).SetValue(m_hvideo_pixel_width);
      GetChild<KaxVideoPixelHeight>(video).SetValue(m_hvideo_pixel_height);
    }

  }

  if ((COMPRESSION_UNSPECIFIED != m_hcompression) && (COMPRESSION_NONE != m_hcompression)) {
    KaxContentEncoding &c_encoding = GetChild<KaxContentEncoding>(GetChild<KaxContentEncodings>(m_track_entry));

    GetChild<KaxContentEncodingOrder>(c_encoding).SetValue(0); // First modification.
    GetChild<KaxContentEncodingType >(c_encoding).SetValue(0); // It's a compression.
    GetChild<KaxContentEncodingScope>(c_encoding).SetValue(1); // Only the frame contents have been compresed.

    m_compressor = compressor_c::create(m_hcompression);
    m_compressor->set_track_headers(c_encoding);
  }

  apply_block_addition_mappings();

  if (g_no_lacing)
    m_track_entry->EnableLacing(false);

  set_tag_track_uid();
  if (m_ti.m_tags) {
    while (m_ti.m_tags->ListSize() != 0) {
      add_tags(static_cast<KaxTag &>(*(*m_ti.m_tags)[0]));
      m_ti.m_tags->Remove(0);
    }
  }
}

void
generic_packetizer_c::apply_block_addition_mappings() {
  if (!m_track_entry)
    return;

  DeleteChildren<KaxBlockAdditionMapping>(m_track_entry);

  for (auto const &mapping : m_block_addition_mappings) {
    if (!mapping.is_valid())
      continue;

    auto &kmapping = AddEmptyChild<KaxBlockAdditionMapping>(m_track_entry);

    if (!mapping.id_name.empty())
      GetChild<KaxBlockAddIDName>(kmapping).SetValue(mapping.id_name);

    if (mapping.id_type)
      GetChild<KaxBlockAddIDType>(kmapping).SetValue(*mapping.id_type);

    if (mapping.id_value)
      GetChild<KaxBlockAddIDValue>(kmapping).SetValue(*mapping.id_value);

    if (mapping.id_extra_data && mapping.id_extra_data->get_size())
      GetChild<KaxBlockAddIDExtraData>(kmapping).CopyBuffer(mapping.id_extra_data->get_buffer(), mapping.id_extra_data->get_size());
  }
}

void
generic_packetizer_c::fix_headers() {
  GetChild<KaxTrackFlagDefault>(m_track_entry).SetValue(g_default_tracks[TRACK_TYPE_TO_DEFTRACK_TYPE(m_htrack_type)] == m_hserialno ? 1 : 0);

  m_track_entry->SetGlobalTimecodeScale((int64_t)g_timestamp_scale);
}

void
generic_packetizer_c::compress_packet(packet_t &packet) {
  if (!m_compressor) {
    return;
  }

  try {
    packet.data = m_compressor->compress(packet.data);
    size_t i;
    for (i = 0; packet.data_adds.size() > i; ++i)
      packet.data_adds[i] = m_compressor->compress(packet.data_adds[i]);

  } catch (mtx::compression_x &e) {
    mxerror_tid(m_ti.m_fname, m_ti.m_id, fmt::format(Y("Compression failed: {0}\n"), e.error()));
  }
}

void
generic_packetizer_c::account_enqueued_bytes(packet_t &packet,
                                             int64_t factor) {
  m_enqueued_bytes += packet.calculate_uncompressed_size() * factor;
}

void
generic_packetizer_c::add_packet(packet_cptr pack) {
  if ((0 == m_num_packets) && m_ti.m_reset_timestamps)
    m_ti.m_tcsync.displacement = -pack->timestamp;

  ++m_num_packets;

  if (!m_reader->m_ptzr_first_packet)
    m_reader->m_ptzr_first_packet = this;

  // strip elements to be removed
  if (   (-1                     != m_htrack_max_add_block_ids)
      && (pack->data_adds.size()  > static_cast<size_t>(m_htrack_max_add_block_ids)))
    pack->data_adds.resize(m_htrack_max_add_block_ids);

  pack->data->take_ownership();
  for (auto &data_add : pack->data_adds)
    data_add->take_ownership();

  pack->source = this;

  if ((0 > pack->bref) && (0 <= pack->fref))
    std::swap(pack->bref, pack->fref);

  account_enqueued_bytes(*pack, +1);

  if (1 != m_connected_to)
    add_packet2(pack);
  else
    m_deferred_packets.push_back(pack);
}

#define ADJUST_TIMESTAMP(x) boost::rational_cast<int64_t>(m_ti.m_tcsync.factor * (x + m_correction_timestamp_offset + m_append_timestamp_offset)) + m_ti.m_tcsync.displacement

void
generic_packetizer_c::add_packet2(packet_cptr pack) {
  pack->timestamp   = ADJUST_TIMESTAMP(pack->timestamp);
  if (pack->has_bref())
    pack->bref     = ADJUST_TIMESTAMP(pack->bref);
  if (pack->has_fref())
    pack->fref     = ADJUST_TIMESTAMP(pack->fref);
  if (pack->has_duration()) {
    pack->duration = boost::rational_cast<int64_t>(m_ti.m_tcsync.factor * pack->duration);
    if (pack->has_discard_padding())
      pack->duration -= std::min(pack->duration, pack->discard_padding.to_ns());
  }

  if (0 > pack->timestamp)
    return;

  // 'timestamp < safety_last_timestamp' may only occur for B frames. In this
  // case we have the coding order, e.g. IPB1B2 and the timestamps
  // I: 0, P: 120, B1: 40, B2: 80.
  if (!m_relaxed_timestamp_checking && (pack->timestamp < m_safety_last_timestamp) && (0 > pack->fref) && mtx::hacks::is_engaged(mtx::hacks::ENABLE_TIMESTAMP_WARNING)) {
    if (track_audio == m_htrack_type) {
      int64_t needed_timestamp_offset  = m_safety_last_timestamp + m_safety_last_duration - pack->timestamp;
      m_correction_timestamp_offset   += needed_timestamp_offset;
      pack->timestamp                 += needed_timestamp_offset;
      if (pack->has_bref())
        pack->bref += needed_timestamp_offset;
      if (pack->has_fref())
        pack->fref += needed_timestamp_offset;

      mxwarn_tid(m_ti.m_fname, m_ti.m_id,
                 fmt::format(Y("The current packet's timestamp is smaller than that of the previous packet. "
                               "This usually means that the source file is a Matroska file that has not been created 100% correctly. "
                               "The timestamps of all packets will be adjusted by {0}ms in order not to lose any data. "
                               "This may throw audio/video synchronization off, but that can be corrected with mkvmerge's \"--sync\" option. "
                               "If you already use \"--sync\" and you still get this warning then do NOT worry -- this is normal. "
                               "If this error happens more than once and you get this message more than once for a particular track "
                               "then either is the source file badly mastered, or mkvmerge contains a bug. "
                               "In this case you should contact the author Moritz Bunkus <moritz@bunkus.org>.\n"),
                             (needed_timestamp_offset + 500000) / 1000000));

    } else
      mxwarn_tid(m_ti.m_fname, m_ti.m_id,
                 fmt::format("generic_packetizer_c::add_packet2: timestamp < last_timestamp ({0} < {1}). {2}\n",
                             mtx::string::format_timestamp(pack->timestamp), mtx::string::format_timestamp(m_safety_last_timestamp), BUGMSG));
  }

  m_safety_last_timestamp        = pack->timestamp;
  m_safety_last_duration        = pack->duration;
  pack->timestamp_before_factory = pack->timestamp;

  m_packet_queue.push_back(pack);
  if (!m_timestamp_factory || (TFA_IMMEDIATE == m_timestamp_factory_application_mode))
    apply_factory_once(pack);
  else
    apply_factory();

  after_packet_timestamped(*pack);

  compress_packet(*pack);
}

void
generic_packetizer_c::process_deferred_packets() {
  for (auto &packet : m_deferred_packets)
    add_packet2(packet);
  m_deferred_packets.clear();
}

packet_cptr
generic_packetizer_c::get_packet() {
  if (m_packet_queue.empty() || !m_packet_queue.front()->factory_applied)
    return packet_cptr{};

  packet_cptr pack = m_packet_queue.front();
  m_packet_queue.pop_front();

  pack->output_order_timestamp = timestamp_c::ns(pack->assigned_timestamp - std::max(m_codec_delay.to_ns(0), m_seek_pre_roll.to_ns(0)));

  account_enqueued_bytes(*pack, -1);

  --m_next_packet_wo_assigned_timestamp;
  if (0 > m_next_packet_wo_assigned_timestamp)
    m_next_packet_wo_assigned_timestamp = 0;

  return pack;
}

void
generic_packetizer_c::apply_factory_once(packet_cptr &packet) {
  if (!m_timestamp_factory) {
    packet->assigned_timestamp = packet->timestamp;
    packet->gap_following      = false;
  } else
    packet->gap_following      = m_timestamp_factory->get_next(packet);

  packet->factory_applied      = true;

  mxverb(4,
         fmt::format("apply_factory_once(): source {0} t {1} tbf {2} at {3}\n",
                     packet->source->get_source_track_num(), packet->timestamp, packet->timestamp_before_factory, packet->assigned_timestamp));

  m_max_timestamp_seen           = std::max(m_max_timestamp_seen, packet->assigned_timestamp + packet->duration);
  m_reader->m_max_timestamp_seen = std::max(m_max_timestamp_seen, m_reader->m_max_timestamp_seen);

  ++m_next_packet_wo_assigned_timestamp;
}

void
generic_packetizer_c::apply_factory() {
  if (m_packet_queue.empty())
    return;

  // Find the first packet to which the factory hasn't been applied yet.
  packet_cptr_di p_start = m_packet_queue.begin() + m_next_packet_wo_assigned_timestamp;

  while ((m_packet_queue.end() != p_start) && (*p_start)->factory_applied)
    ++p_start;

  if (m_packet_queue.end() == p_start)
    return;

  if (TFA_SHORT_QUEUEING == m_timestamp_factory_application_mode)
    apply_factory_short_queueing(p_start);

  else
    apply_factory_full_queueing(p_start);
}

void
generic_packetizer_c::apply_factory_short_queueing(packet_cptr_di &p_start) {
  while (m_packet_queue.end() != p_start) {
    // Find the next packet with a timestamp bigger than the start packet's
    // timestamp. All packets between those two including the start packet
    // and excluding the end packet can be timestamped.
    packet_cptr_di p_end = p_start + 1;
    while ((m_packet_queue.end() != p_end) && ((*p_end)->timestamp_before_factory < (*p_start)->timestamp_before_factory))
      ++p_end;

    // Abort if no such packet was found, but keep on assigning if the
    // packetizer has been flushed already.
    if (!m_has_been_flushed && (m_packet_queue.end() == p_end))
      return;

    // Now assign timestamps to the ones between p_start and p_end...
    packet_cptr_di p_current;
    for (p_current = p_start + 1; p_current != p_end; ++p_current)
      apply_factory_once(*p_current);
    // ...and to p_start itself.
    apply_factory_once(*p_start);

    p_start = p_end;
  }
}

struct packet_sorter_t {
  int m_index;
  static std::deque<packet_cptr> *m_packet_queue;

  packet_sorter_t(int index)
    : m_index(index)
  {
  }

  bool operator <(const packet_sorter_t &cmp) const {
    return (*m_packet_queue)[m_index]->timestamp < (*m_packet_queue)[cmp.m_index]->timestamp;
  }
};

std::deque<packet_cptr> *packet_sorter_t::m_packet_queue = nullptr;

void
generic_packetizer_c::apply_factory_full_queueing(packet_cptr_di &p_start) {
  packet_sorter_t::m_packet_queue = &m_packet_queue;

  while (m_packet_queue.end() != p_start) {
    // Find the next I frame packet.
    packet_cptr_di p_end = p_start + 1;
    while ((m_packet_queue.end() != p_end) && !(*p_end)->is_key_frame())
      ++p_end;

    // Abort if no such packet was found, but keep on assigning if the
    // packetizer has been flushed already.
    if (!m_has_been_flushed && (m_packet_queue.end() == p_end))
      return;

    // Now sort the frames by their timestamp as the factory has to be
    // applied to the packets in the same order as they're timestamped.
    std::vector<packet_sorter_t> sorter;
    bool needs_sorting         = false;
    int64_t previous_timestamp = 0;
    size_t i                   = distance(m_packet_queue.begin(), p_start);

    packet_cptr_di p_current;
    for (p_current = p_start; p_current != p_end; ++i, ++p_current) {
      sorter.push_back(packet_sorter_t(i));
      if (m_packet_queue[i]->timestamp < previous_timestamp)
        needs_sorting = true;
      previous_timestamp = m_packet_queue[i]->timestamp;
    }

    if (needs_sorting)
      std::sort(sorter.begin(), sorter.end());

    // Finally apply the factory.
    for (i = 0; sorter.size() > i; ++i)
      apply_factory_once(m_packet_queue[sorter[i].m_index]);

    p_start = p_end;
  }
}

void
generic_packetizer_c::force_duration_on_last_packet() {
  if (m_packet_queue.empty()) {
    mxverb_tid(3, m_ti.m_fname, m_ti.m_id, "force_duration_on_last_packet: packet queue is empty\n");
    return;
  }
  packet_cptr &packet        = m_packet_queue.back();
  packet->duration_mandatory = true;
  mxverb_tid(3, m_ti.m_fname, m_ti.m_id,
             fmt::format("force_duration_on_last_packet: forcing at {0} with {1:.3f}ms\n", mtx::string::format_timestamp(packet->timestamp), packet->duration / 1000.0));
}

int64_t
generic_packetizer_c::calculate_avi_audio_sync(int64_t num_bytes,
                                               int64_t samples_per_packet,
                                               int64_t packet_duration) {
  if (!m_ti.m_avi_audio_sync_enabled || mtx::hacks::is_engaged(mtx::hacks::NO_DELAY_FOR_GARBAGE_IN_AVI))
    return -1;

  if (m_ti.m_avi_audio_data_rate)
    return num_bytes * 1000000000 / m_ti.m_avi_audio_data_rate;

  return ((num_bytes + samples_per_packet - 1) / samples_per_packet) * packet_duration;
}

void
generic_packetizer_c::connect(generic_packetizer_c *src,
                              int64_t append_timestamp_offset) {
  m_free_refs                   = src->m_free_refs;
  m_next_free_refs              = src->m_next_free_refs;
  m_track_entry                 = src->m_track_entry;
  m_hserialno                   = src->m_hserialno;
  m_htrack_type                 = src->m_htrack_type;
  m_htrack_default_duration     = src->m_htrack_default_duration;
  m_huid                        = src->m_huid;
  m_hcompression                = src->m_hcompression;
  m_compressor                  = compressor_c::create(m_hcompression);
  m_last_cue_timestamp          = src->m_last_cue_timestamp;
  m_timestamp_factory           = src->m_timestamp_factory;
  m_correction_timestamp_offset = 0;

  if (-1 == append_timestamp_offset)
    m_append_timestamp_offset = src->m_max_timestamp_seen;
  else
    m_append_timestamp_offset = append_timestamp_offset;

  m_connected_to++;
  if (2 == m_connected_to) {
    process_deferred_packets();
    src->m_connected_successor = this;
  }
}

split_result_e
generic_packetizer_c::can_be_split(std::string &/* error_message */) {
  return CAN_SPLIT_YES;
}

void
generic_packetizer_c::set_displacement_maybe(int64_t displacement) {
  if ((1 == m_ti.m_tcsync.factor) && (0 == m_ti.m_tcsync.displacement))
    m_ti.m_tcsync.displacement = displacement;
}

bool
generic_packetizer_c::contains_gap() {
  return m_timestamp_factory ? m_timestamp_factory->contains_gap() : false;
}

void
generic_packetizer_c::flush() {
  flush_impl();

  m_has_been_flushed = true;
  apply_factory();
}

bool
generic_packetizer_c::display_dimensions_or_aspect_ratio_set() {
  return m_ti.display_dimensions_or_aspect_ratio_set();
}

bool
generic_packetizer_c::is_compatible_with(output_compatibility_e compatibility) {
  return OC_MATROSKA == compatibility;
}

void
generic_packetizer_c::discard_queued_packets() {
  m_packet_queue.clear();
  m_enqueued_bytes = 0;
}

bool
generic_packetizer_c::wants_cue_duration()
  const {
  return get_track_type() == track_subtitle;
}

void
generic_packetizer_c::show_experimental_status_version(std::string const &codec_id) {
  auto idx = get_format_name().get_untranslated();
  if (s_experimental_status_warning_shown[idx])
    return;

  s_experimental_status_warning_shown[idx] = true;
  mxwarn(fmt::format(Y("Note that the Matroska specifications regarding the storage of '{0}' have not been finalized yet. "
                       "mkvmerge's support for it is therefore subject to change and uses the CodecID '{1}/EXPERIMENTAL' instead of '{1}'. "
                       "This warning will be removed once the specifications have been finalized and mkvmerge has been updated accordingly.\n"),
                     get_format_name().get_translated(), codec_id));
}

int64_t
generic_packetizer_c::create_track_number() {
  int file_num = -1;
  size_t i;

  // Determine current file's file number regarding --track-order.
  for (i = 0; i < g_files.size(); i++)
    if (g_files[i]->reader.get() == this->m_reader) {
      file_num = i;
      break;
    }

  if (file_num == -1)
    mxerror(fmt::format(Y("create_track_number: file_num not found. {0}\n"), BUGMSG));

  // Determine current track's track number regarding --track-order
  // and look up the pair in g_track_order.
  int tnum = -1;
  for (i = 0; i < g_track_order.size(); i++)
    if (   (g_track_order[i].file_id  == file_num)
        && (g_track_order[i].track_id == m_ti.m_id)) {
      tnum = i + 1;
      break;
    }

  // If the track's file/track number pair was found in g_track_order,
  // use the resulting track number unconditionally.
  if (tnum > 0)
    return tnum;

  // The file/track number pair wasn't found. Create a new track
  // number. Don't use numbers that might be assigned by
  // --track-order.

  ++ms_track_number;
  return ms_track_number + g_track_order.size();
}

file_status_e
generic_packetizer_c::read(bool force) {
  return m_reader->read_next(this, force);
}

void
generic_packetizer_c::prevent_lacing() {
  m_prevent_lacing = true;
}

bool
generic_packetizer_c::is_lacing_prevented()
  const {
  return m_prevent_lacing;
}

void
generic_packetizer_c::after_packet_timestamped(packet_t &) {
}

void
generic_packetizer_c::after_packet_rendered(packet_t const &) {
}

void
generic_packetizer_c::before_file_finished() {
}

void
generic_packetizer_c::after_file_created() {
}

generic_packetizer_c *
generic_packetizer_c::get_connected_successor()
  const {
  return m_connected_successor;
}

void
generic_packetizer_c::set_source_id(std::string const &source_id) {
  m_source_id = source_id;
}

std::string
generic_packetizer_c::get_source_id()
  const {
  return m_source_id;
}
