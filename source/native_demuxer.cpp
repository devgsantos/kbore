#include "nstv/native_demuxer.hpp"

#include <sstream>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/version.h>
}
#endif

namespace nstv {

#ifdef NSTV_USE_FFMPEG
struct NativeDemuxer::Impl {
  AVFormatContext *format = nullptr;
};
#endif

namespace {

#ifdef NSTV_USE_FFMPEG

std::string ffmpegError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

std::string codecName(AVCodecID codecId) {
  const char *name = avcodec_get_name(codecId);

  if (name && name[0] != '\0') {
    return name;
  }

  const AVCodecDescriptor *descriptor = avcodec_descriptor_get(codecId);

  if (descriptor && descriptor->name) {
    return descriptor->name;
  }

  return "unknown";
}

int audioChannels(const AVCodecParameters *params) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
  return params->ch_layout.nb_channels;
#else
  return params->channels;
#endif
}

NativeStreamInfo buildVideoInfo(int index, const AVCodecParameters *params) {
  NativeStreamInfo info;

  info.exists = true;
  info.index = index;
  info.type = "video";
  info.codecName = codecName(params->codec_id);
  info.width = params->width;
  info.height = params->height;

  return info;
}

NativeStreamInfo buildAudioInfo(int index, const AVCodecParameters *params) {
  NativeStreamInfo info;

  info.exists = true;
  info.index = index;
  info.type = "audio";
  info.codecName = codecName(params->codec_id);
  info.sampleRate = params->sample_rate;
  info.channels = audioChannels(params);

  return info;
}

#endif

} // namespace

NativeDemuxer::NativeDemuxer() {
#ifdef NSTV_USE_FFMPEG
  impl_ = new Impl();
  avformat_network_init();
#endif
}

NativeDemuxer::~NativeDemuxer() {
  close();

#ifdef NSTV_USE_FFMPEG
  avformat_network_deinit();

  delete impl_;
  impl_ = nullptr;
#endif
}

void NativeDemuxer::resetState() {
  open_ = false;

  url_.clear();
  error_.clear();
  summary_.clear();

  video_ = NativeStreamInfo{};
  audio_ = NativeStreamInfo{};
}

bool NativeDemuxer::open(const std::string &url) {
  close();

  resetState();

  url_ = url;

#ifndef NSTV_USE_FFMPEG
  error_ = "NativeDemuxer requires NSTV_USE_FFMPEG.";
  return false;
#else
  if (!impl_) {
    error_ = "NativeDemuxer internal state is unavailable.";
    return false;
  }

  AVDictionary *options = nullptr;

  av_dict_set(&options, "user_agent", "NSTV-NativeDemuxer/0.1", 0);
  av_dict_set(&options, "reconnect", "1", 0);
  av_dict_set(&options, "reconnect_streamed", "1", 0);
  av_dict_set(&options, "reconnect_delay_max", "5", 0);
  av_dict_set(&options, "timeout", "8000000", 0);
  av_dict_set(&options, "rw_timeout", "8000000", 0);
  av_dict_set(&options, "analyzeduration", "1000000", 0);
  av_dict_set(&options, "probesize", "65536", 0);

  int ret = avformat_open_input(
    &impl_->format,
    url.c_str(),
    nullptr,
    &options
  );

  av_dict_free(&options);

  if (ret < 0) {
    error_ = "NativeDemuxer could not open input: " + ffmpegError(ret);
    close();
    return false;
  }

  ret = avformat_find_stream_info(impl_->format, nullptr);

  if (ret < 0) {
    error_ = "NativeDemuxer could not read stream info: " + ffmpegError(ret);
    close();
    return false;
  }

  for (unsigned int i = 0; i < impl_->format->nb_streams; ++i) {
    AVStream *stream = impl_->format->streams[i];

    if (!stream || !stream->codecpar) {
      continue;
    }

    AVCodecParameters *params = stream->codecpar;

    if (params->codec_type == AVMEDIA_TYPE_VIDEO && !video_.exists) {
      video_ = buildVideoInfo(static_cast<int>(i), params);
      continue;
    }

    if (params->codec_type == AVMEDIA_TYPE_AUDIO && !audio_.exists) {
      audio_ = buildAudioInfo(static_cast<int>(i), params);
      continue;
    }
  }

  if (!video_.exists) {
    error_ = "NativeDemuxer did not find a video stream.";
    close();
    return false;
  }

  std::ostringstream out;

  out
    << "video="
    << video_.codecName
    << " "
    << video_.width
    << "x"
    << video_.height
    << " stream="
    << video_.index;

  if (audio_.exists) {
    out
      << " | audio="
      << audio_.codecName
      << " "
      << audio_.channels
      << "ch "
      << audio_.sampleRate
      << "Hz"
      << " stream="
      << audio_.index;
  } else {
    out << " | audio=none";
  }

  out << " | streams=" << impl_->format->nb_streams;

  summary_ = out.str();
  open_ = true;

  return true;
#endif
}

void NativeDemuxer::close() {
#ifdef NSTV_USE_FFMPEG
  if (impl_ && impl_->format) {
    avformat_close_input(&impl_->format);
    impl_->format = nullptr;
  }
#endif

  open_ = false;
}

#ifdef NSTV_USE_FFMPEG
AVFormatContext *NativeDemuxer::formatContext() const {
  return impl_ ? impl_->format : nullptr;
}
#endif

} // namespace nstv