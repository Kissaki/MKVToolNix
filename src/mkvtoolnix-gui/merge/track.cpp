#include "common/common_pch.h"

#include <QRegularExpression>

#include "common/iso639.h"
#include "common/list_utils.h"
#include "common/strings/editing.h"
#include "mkvtoolnix-gui/merge/enums.h"
#include "mkvtoolnix-gui/merge/mkvmerge_option_builder.h"
#include "mkvtoolnix-gui/merge/mux_config.h"
#include "mkvtoolnix-gui/merge/source_file.h"
#include "mkvtoolnix-gui/merge/track.h"
#include "mkvtoolnix-gui/util/config_file.h"
#include "mkvtoolnix-gui/util/settings.h"

#include <QVariant>

namespace mtx::gui::Merge {

using namespace mtx::gui;

Track::Track(SourceFile *file,
             TrackType type)
  : m_file{file}
  , m_type{type}
{
}

Track::~Track() {
}

bool
Track::isType(TrackType type)
  const {
  return type == m_type;
}

bool
Track::isAudio()
  const {
  return isType(TrackType::Audio);
}

bool
Track::isVideo()
  const {
  return isType(TrackType::Video);
}

bool
Track::isSubtitles()
  const {
  return isType(TrackType::Subtitles);
}

bool
Track::isButtons()
  const {
  return isType(TrackType::Buttons);
}

bool
Track::isChapters()
  const {
  return isType(TrackType::Chapters);
}

bool
Track::isGlobalTags()
  const {
  return isType(TrackType::GlobalTags);
}

bool
Track::isTags()
  const {
  return isType(TrackType::Tags);
}

bool
Track::isAttachment()
  const {
  return isType(TrackType::Attachment);
}

bool
Track::isAppended()
  const {
  return !m_file ? false : m_file->m_appended;
}

bool
Track::isRegular()
  const {
  return isAudio() || isVideo() || isSubtitles() || isButtons();
}

bool
Track::isPropertySet(QString const &property)
  const {
  if (!m_properties.contains(property))
    return false;

  auto var = m_properties.value(property);

  return var.isNull()                            ? false
       : !var.isValid()                          ? false
       : var.canConvert(QMetaType::QVariantList) ? !var.toList().isEmpty()
       : var.canConvert(QMetaType::QString)      ? !var.toString().isEmpty()
       :                                           true;
}

void
Track::setDefaultsNonRegular() {
  auto &settings = Util::Settings::get();

  if (!settings.m_enableMuxingTracksByTheseTypes.contains(m_type))
    m_muxThis = false;
}

void
Track::setDefaults(mtx::bcp47::language_c const &languageDerivedFromFileName) {
  if (!isRegular()) {
    setDefaultsNonRegular();
    return;
  }

  auto &settings = Util::Settings::get();

  if (isAudio() && settings.m_setAudioDelayFromFileName)
    m_delay = extractAudioDelayFromFileName();

  m_forcedTrackFlag            = m_properties.value("forced_track").toBool() ? 1 : 0;
  m_forcedTrackFlagWasSet      = m_forcedTrackFlag == 1;
  m_defaultTrackFlagWasSet     = m_properties.value("default_track").toBool();
  m_defaultTrackFlagWasPresent = m_properties.contains("default_track");
  m_name                       = m_properties.value("track_name").toString();
  m_nameWasPresent             = !m_name.isEmpty();
  m_cropping                   = m_properties.value("cropping").toString();
  m_aacSbrWasDetected          = m_properties.value("aac_is_sbr").toString().contains(QRegExp{"1|true"});
  m_stereoscopy                = m_properties.contains("stereo_mode") ? m_properties.value("stereo_mode").toUInt() + 1 : 0;
  auto encoding                = m_properties.value(Q("encoding")).toString();
  m_characterSet               = !encoding.isEmpty()   ? encoding
                               : canChangeSubCharset() ? settings.m_defaultSubtitleCharset
                               :                         Q("");

  auto languageProperty = m_properties.value("language_ietf").toString();
  if (languageProperty.isEmpty())
    languageProperty = m_properties.value("language").toString();

  auto language = mtx::bcp47::language_c::parse(to_utf8(languageProperty));

  if (languageDerivedFromFileName.is_valid()) {
    auto policy = isAudio()     ? settings.m_deriveAudioTrackLanguageFromFileNamePolicy
                : isVideo()     ? settings.m_deriveVideoTrackLanguageFromFileNamePolicy
                : isSubtitles() ? settings.m_deriveSubtitleTrackLanguageFromFileNamePolicy
                :                 Util::Settings::DeriveLanguageFromFileNamePolicy::Never;

    if (   ((policy != Util::Settings::DeriveLanguageFromFileNamePolicy::Never)                  && !language.is_valid())
        || ((policy == Util::Settings::DeriveLanguageFromFileNamePolicy::IfAbsentOrUndetermined) && (language.get_language() == "und"s)))
      language = languageDerivedFromFileName;
  }

  if (   !language.is_valid()
      || (   (Util::Settings::SetDefaultLanguagePolicy::IfAbsentOrUndetermined == settings.m_whenToSetDefaultLanguage)
          && (language.get_language() == "und"s)))
    language = isAudio()     ? settings.m_defaultAudioTrackLanguage
             : isVideo()     ? settings.m_defaultVideoTrackLanguage
             : isSubtitles() ? settings.m_defaultSubtitleTrackLanguage
             :                 mtx::bcp47::language_c{};

  if (language.is_valid())
    m_language = language;

  QRegExp re_displayDimensions{"^(\\d+)x(\\d+)$"};
  if (-1 != re_displayDimensions.indexIn(m_properties.value("display_dimensions").toString())) {
    m_displayWidth  = re_displayDimensions.cap(1);
    m_displayHeight = re_displayDimensions.cap(2);
  }

  if (canRemoveDialogNormalizationGain() && settings.m_mergeEnableDialogNormGainRemoval)
    m_removeDialogNormalizationGain = true;

  if (!settings.m_enableMuxingTracksByTheseTypes.contains(m_type))
    m_muxThis = false;

  else if (   !settings.m_enableMuxingTracksByLanguage
           || !mtx::included_in(m_type, TrackType::Video, TrackType::Audio, TrackType::Subtitles)
           || ((TrackType::Video     == m_type) && settings.m_enableMuxingAllVideoTracks)
           || ((TrackType::Audio     == m_type) && settings.m_enableMuxingAllAudioTracks)
           || ((TrackType::Subtitles == m_type) && settings.m_enableMuxingAllSubtitleTracks))
    m_muxThis = true;

  else {
    language  = !m_language.is_valid() ? mtx::bcp47::language_c::parse("und"s) : m_language;
    m_muxThis = settings.m_enableMuxingTracksByTheseLanguages.contains(Q(language.get_iso639_alpha_3_code()));
  }
}

QString
Track::extractAudioDelayFromFileName()
  const {
  QRegExp re{"delay\\s+(-?\\d+)", Qt::CaseInsensitive};
  if (-1 == re.indexIn(m_file->m_fileName))
    return "";
  return re.cap(1);
}

void
Track::saveSettings(Util::ConfigFile &settings)
  const {
  MuxConfig::saveProperties(settings, m_properties);

  QStringList appendedTracks;
  for (auto &track : m_appendedTracks)
    appendedTracks << QString::number(reinterpret_cast<qulonglong>(track));

  settings.setValue("objectID",                      reinterpret_cast<qulonglong>(this));
  settings.setValue("appendedTo",                    reinterpret_cast<qulonglong>(m_appendedTo));
  settings.setValue("appendedTracks",                appendedTracks);
  settings.setValue("type",                          static_cast<int>(m_type));
  settings.setValue("id",                            static_cast<qulonglong>(m_id));
  settings.setValue("muxThis",                       m_muxThis);
  settings.setValue("setAspectRatio",                m_setAspectRatio);
  settings.setValue("defaultTrackFlagWasSet",        m_defaultTrackFlagWasSet);
  settings.setValue("defaultTrackFlagWasPresent",    m_defaultTrackFlagWasPresent);
  settings.setValue("forcedTrackFlagWasSet",         m_forcedTrackFlagWasSet);
  settings.setValue("aacSbrWasDetected",             m_aacSbrWasDetected);
  settings.setValue("nameWasPresent",                m_nameWasPresent);
  settings.setValue("fixBitstreamTimingInfo",        m_fixBitstreamTimingInfo);
  settings.setValue("name",                          m_name);
  settings.setValue("codec",                         m_codec);
  settings.setValue("language",                      Q(m_language.format()));
  settings.setValue("tags",                          m_tags);
  settings.setValue("delay",                         m_delay);
  settings.setValue("stretchBy",                     m_stretchBy);
  settings.setValue("defaultDuration",               m_defaultDuration);
  settings.setValue("timestamps",                    m_timestamps);
  settings.setValue("aspectRatio",                   m_aspectRatio);
  settings.setValue("displayWidth",                  m_displayWidth);
  settings.setValue("displayHeight",                 m_displayHeight);
  settings.setValue("cropping",                      m_cropping);
  settings.setValue("characterSet",                  m_characterSet);
  settings.setValue("additionalOptions",             m_additionalOptions);
  settings.setValue("defaultTrackFlag",              m_defaultTrackFlag);
  settings.setValue("forcedTrackFlag",               m_forcedTrackFlag);
  settings.setValue("stereoscopy",                   m_stereoscopy);
  settings.setValue("naluSizeLength",                m_naluSizeLength);
  settings.setValue("cues",                          m_cues);
  settings.setValue("aacIsSBR",                      m_aacIsSBR);
  settings.setValue("reduceAudioToCore",             m_reduceAudioToCore);
  settings.setValue("removeDialogNormalizationGain", m_removeDialogNormalizationGain);
  settings.setValue("compression",                   static_cast<int>(m_compression));
  settings.setValue("size",                          static_cast<qulonglong>(m_size));
  settings.setValue("attachmentDescription",         m_attachmentDescription);
}

void
Track::loadSettings(MuxConfig::Loader &l) {
  MuxConfig::loadProperties(l.settings, m_properties);

  auto objectID = l.settings.value("objectID").toULongLong();
  if ((0 >= objectID) || l.objectIDToTrack.contains(objectID))
    throw InvalidSettingsX{};

  l.objectIDToTrack[objectID]     = this;
  m_type                          = static_cast<TrackType>(l.settings.value("type").toInt());
  m_id                            = l.settings.value("id").toULongLong();
  m_muxThis                       = l.settings.value("muxThis").toBool();
  m_setAspectRatio                = l.settings.value("setAspectRatio").toBool();
  m_defaultTrackFlagWasSet        = l.settings.value("defaultTrackFlagWasSet").toBool();
  m_defaultTrackFlagWasPresent    = l.settings.value("defaultTrackFlagWasPresent").toBool() || m_defaultTrackFlagWasSet;
  m_forcedTrackFlagWasSet         = l.settings.value("forcedTrackFlagWasSet").toBool();
  m_aacSbrWasDetected             = l.settings.value("aacSbrWasDetected").toBool();
  m_name                          = l.settings.value("name").toString();
  m_nameWasPresent                = l.settings.value("nameWasPresent").toBool();
  m_fixBitstreamTimingInfo        = l.settings.value("fixBitstreamTimingInfo").toBool();
  m_codec                         = l.settings.value("codec").toString();
  m_language                      = mtx::bcp47::language_c::parse(to_utf8(l.settings.value("language").toString()));
  m_tags                          = l.settings.value("tags").toString();
  m_delay                         = l.settings.value("delay").toString();
  m_stretchBy                     = l.settings.value("stretchBy").toString();
  m_defaultDuration               = l.settings.value("defaultDuration").toString();
  m_timestamps                    = l.settings.value("timestamps", l.settings.value("timecodes").toString()).toString();
  m_aspectRatio                   = l.settings.value("aspectRatio").toString();
  m_displayWidth                  = l.settings.value("displayWidth").toString();
  m_displayHeight                 = l.settings.value("displayHeight").toString();
  m_cropping                      = l.settings.value("cropping").toString();
  m_characterSet                  = l.settings.value("characterSet").toString();
  m_additionalOptions             = l.settings.value("additionalOptions").toString();
  m_defaultTrackFlag              = l.settings.value("defaultTrackFlag").toInt();
  m_forcedTrackFlag               = l.settings.value("forcedTrackFlag").toInt();
  m_stereoscopy                   = l.settings.value("stereoscopy").toInt();
  m_naluSizeLength                = l.settings.value("naluSizeLength").toInt();
  m_cues                          = l.settings.value("cues").toInt();
  m_aacIsSBR                      = l.settings.value("aacIsSBR").toInt();
  m_reduceAudioToCore             = l.settings.value("reduceAudioToCore").toBool();
  m_removeDialogNormalizationGain = l.settings.value("removeDialogNormalizationGain").toBool();
  m_compression                   = static_cast<TrackCompression>(l.settings.value("compression").toInt());
  m_size                          = l.settings.value("size").toULongLong();
  m_attachmentDescription         = l.settings.value("attachmentDescription").toString();

  if (   (TrackType::Min        > m_type)        || (TrackType::Max        < m_type)
      || (TrackCompression::Min > m_compression) || (TrackCompression::Max < m_compression))
    throw InvalidSettingsX{};
}

void
Track::fixAssociations(MuxConfig::Loader &l) {
  if (isRegular() && isAppended()) {
    auto appendedToID = l.settings.value("appendedTo").toULongLong();
    if ((0 >= appendedToID) || !l.objectIDToTrack.contains(appendedToID))
      throw InvalidSettingsX{};
    m_appendedTo = l.objectIDToTrack.value(appendedToID);
  }

  m_appendedTracks.clear();
  for (auto &appendedTrackID : l.settings.value("appendedTracks").toStringList()) {
    if (!l.objectIDToTrack.contains(appendedTrackID.toULongLong()))
      throw InvalidSettingsX{};
    m_appendedTracks << l.objectIDToTrack.value(appendedTrackID.toULongLong());
  }
}

std::string
Track::debugInfo()
  const {
  return fmt::format("{0}/{1}:{2}@{3}", static_cast<unsigned int>(m_type), m_id, m_codec, static_cast<void const *>(this));
}

void
Track::buildMkvmergeOptions(MkvmergeOptionBuilder &opt)
  const {
  ++opt.numTracksOfType[m_type];

  if (!m_muxThis || (m_appendedTo && !m_appendedTo->m_muxThis))
    return;

  auto sid = QString::number(m_id);
  opt.enabledTrackIds[m_type] << sid;

  if (isAudio()) {
    if (m_aacSbrWasDetected || (0 != m_aacIsSBR))
      opt.options << Q("--aac-is-sbr") << Q("%1:%2").arg(sid).arg((1 == m_aacIsSBR) || ((0 == m_aacIsSBR) && m_aacSbrWasDetected) ? 1 : 0);

    if (canReduceToAudioCore() && m_reduceAudioToCore)
      opt.options << Q("--reduce-to-core") << sid;

    if (canRemoveDialogNormalizationGain() && m_removeDialogNormalizationGain)
      opt.options << Q("--remove-dialog-normalization-gain") << sid;

  } else if (isVideo()) {
    if (!m_cropping.isEmpty())
      opt.options << Q("--cropping") << Q("%1:%2").arg(sid).arg(m_cropping);

  } else if (isSubtitles()) {
    if (canChangeSubCharset() && !m_characterSet.isEmpty())
      opt.options << Q("--sub-charset") << Q("%1:%2").arg(sid).arg(m_characterSet);

  } else if (isChapters()) {
    if (!m_characterSet.isEmpty())
      opt.options << Q("--chapter-charset") << m_characterSet;

    if (m_language.is_valid())
      opt.options << Q("--chapter-language") << Q(m_language.format());

    return;

  } else if (isTags() || isGlobalTags() || isAttachment())
    return;

  if (!m_appendedTo) {
    opt.options << Q("--language") << Q("%1:%2").arg(sid).arg(Q(m_language.format()));

    if (m_cues) {
      auto cues = 1 == m_cues ? Q(":iframes")
                : 2 == m_cues ? Q(":all")
                :               Q(":none");
      opt.options << Q("--cues") << (sid + cues);
    }

    if (!m_name.isEmpty() || m_nameWasPresent)
      opt.options << Q("--track-name") << Q("%1:%2").arg(sid).arg(m_name);

    if (0 != m_defaultTrackFlag)
      opt.options << Q("--default-track") << Q("%1:%2").arg(sid).arg(m_defaultTrackFlag == 1 ? Q("yes") : Q("no"));

    if (m_forcedTrackFlagWasSet != !!m_forcedTrackFlag)
      opt.options << Q("--forced-track") << Q("%1:%2").arg(sid).arg(m_forcedTrackFlag == 1 ? Q("yes") : Q("no"));

    if (!m_tags.isEmpty())
      opt.options << Q("--tags") << Q("%1:%2").arg(sid).arg(m_tags);

    if (m_setAspectRatio && !m_aspectRatio.isEmpty())
      opt.options << Q("--aspect-ratio") << Q("%1:%2").arg(sid).arg(m_aspectRatio);

    else if (!m_setAspectRatio && !m_displayHeight.isEmpty() && !m_displayWidth.isEmpty())
      opt.options << Q("--display-dimensions") << Q("%1:%2x%3").arg(sid).arg(m_displayWidth).arg(m_displayHeight);

    if (m_stereoscopy)
      opt.options << Q("--stereo-mode") << Q("%1:%2").arg(sid).arg(m_stereoscopy - 1);

    if (m_naluSizeLength)
      opt.options << Q("--nalu-size-length") << Q("%1:%2").arg(sid).arg(m_naluSizeLength);

    if (m_compression != TrackCompression::Default)
      opt.options << Q("--compression") << Q("%1:%2").arg(sid).arg(TrackCompression::None == m_compression ? Q("none") : Q("zlib"));
  }

  if (!m_delay.isEmpty() || !m_stretchBy.isEmpty())
    opt.options << Q("--sync") << Q("%1:%2").arg(sid).arg(MuxConfig::formatDelayAndStretchBy(m_delay, m_stretchBy));

  if (!m_defaultDuration.isEmpty()) {
    auto unit = m_defaultDuration.contains(QRegExp{"\\d$"}) ? Q("fps") : Q("");
    opt.options << Q("--default-duration") << Q("%1:%2%3").arg(sid).arg(m_defaultDuration).arg(unit);
  }

  if (!m_timestamps.isEmpty())
    opt.options << Q("--timestamps") << Q("%1:%2").arg(sid).arg(m_timestamps);

  if (m_fixBitstreamTimingInfo)
    opt.options << Q("--fix-bitstream-timing-information") << Q("%1:1").arg(sid);

  auto additionalOptions = Q(mtx::string::strip_copy(to_utf8(m_additionalOptions)));
  if (!additionalOptions.isEmpty())
    opt.options += additionalOptions.replace(Q("<TID>"), sid).split(QRegExp{" +"});
}

QString
Track::nameForType(TrackType type) {
  return TrackType::Audio      == type ? QY("Audio")
       : TrackType::Video      == type ? QY("Video")
       : TrackType::Subtitles  == type ? QY("Subtitles")
       : TrackType::Buttons    == type ? QY("Buttons")
       : TrackType::Attachment == type ? QY("Attachment")
       : TrackType::Chapters   == type ? QY("Chapters")
       : TrackType::Tags       == type ? QY("Tags")
       : TrackType::GlobalTags == type ? QY("Global tags")
       :                                 Q("INTERNAL ERROR");
}

QString
Track::nameForType()
  const {
  return nameForType(m_type);
}

bool
Track::canChangeSubCharset()
  const {
  if (   isSubtitles()
      && m_properties.value(Q("text_subtitles")).toBool()
      && (   m_properties.value(Q("encoding")).toString().isEmpty()
          || mtx::included_in(m_file->m_type, mtx::file_type_e::matroska, mtx::file_type_e::mpeg_ts)))
    return true;

  if (   isChapters()
       && mtx::included_in(m_file->m_type, mtx::file_type_e::qtmp4, mtx::file_type_e::mpeg_ts, mtx::file_type_e::ogm))
    return true;

  return false;
}

bool
Track::canReduceToAudioCore()
  const {
  return isAudio()
      && m_codec.contains(QRegularExpression{Q("dts"), QRegularExpression::CaseInsensitiveOption});
}

bool
Track::canRemoveDialogNormalizationGain()
  const {
  return isAudio()
      && m_codec.contains(QRegularExpression{Q("ac-?3|dts|truehd"), QRegularExpression::CaseInsensitiveOption});
}

bool
Track::canSetAacToSbr()
  const {
  return isAudio()
      && m_codec.contains(QRegularExpression{Q("aac"), QRegularExpression::CaseInsensitiveOption})
      && mtx::included_in(m_file->m_type, mtx::file_type_e::aac, mtx::file_type_e::matroska, mtx::file_type_e::real);
}

}
