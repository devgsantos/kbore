#include "nstv/video_player.hpp"
#include <algorithm>
#include <cstring>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace nstv {

#ifdef NSTV_USE_FFMPEG
struct VideoPlayer::Impl {
  AVFormatContext *format = nullptr;
  AVCodecContext *codec = nullptr;
  const AVCodec *decoder = nullptr;
  AVFrame *decoded = nullptr;
  AVFrame *rgba = nullptr;
  AVPacket *packet = nullptr;
  SwsContext *sws = nullptr;
  int videoStream = -1;
  std::vector<uint8_t> rgbaBuffer;
};
#endif

VideoPlayer::VideoPlayer() {
#ifdef NSTV_USE_FFMPEG
  impl_ = new Impl();
  avformat_network_init();
#endif
}

VideoPlayer::~VideoPlayer() {
  close();

#ifdef NSTV_USE_FFMPEG
  avformat_network_deinit();
  delete impl_;
  impl_ = nullptr;
#endif
}

bool VideoPlayer::open(const std::string &url) {
  close();

  url_ = url;
  error_.clear();
  paused_ = false;

#ifndef NSTV_USE_FFMPEG
  error_ = "Video player not enabled: build with NSTV_USE_FFMPEG and FFmpeg/libav libraries.";
  return false;
#else
  if (!impl_) {
    error_ = "Video player internal state unavailable";
    return false;
  }

  AVDictionary *options = nullptr;
  av_dict_set(&options, "user_agent", "NSTV-Switch/0.3", 0);
  av_dict_set(&options, "reconnect", "1", 0);
  av_dict_set(&options, "reconnect_streamed", "1", 0);
  av_dict_set(&options, "reconnect_delay_max", "5", 0);
  av_dict_set(&options, "timeout", "12000000", 0);
  av_dict_set(&options, "rw_timeout", "12000000", 0);

  int ret = avformat_open_input(&impl_->format, url.c_str(), nullptr, &options);
  av_dict_free(&options);

  if (ret < 0) {
    char err[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(ret, err, sizeof(err));
    error_ = std::string("Could not open stream: ") + err;
    close();
    return false;
  }

  ret = avformat_find_stream_info(impl_->format, nullptr);

  if (ret < 0) {
    char err[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(ret, err, sizeof(err));
    error_ = std::string("Could not read stream info: ") + err;
    close();
    return false;
  }

  for (unsigned int i = 0; i < impl_->format->nb_streams; ++i) {
    if (impl_->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      impl_->videoStream = static_cast<int>(i);
      break;
    }
  }

  if (impl_->videoStream < 0) {
    error_ = "No video stream found";
    close();
    return false;
  }

  AVCodecParameters *params = impl_->format->streams[impl_->videoStream]->codecpar;
  impl_->decoder = avcodec_find_decoder(params->codec_id);

  if (!impl_->decoder) {
    error_ = "Unsupported video codec";
    close();
    return false;
  }

  impl_->codec = avcodec_alloc_context3(impl_->decoder);

  if (!impl_->codec) {
    error_ = "Could not allocate codec context";
    close();
    return false;
  }

  ret = avcodec_parameters_to_context(impl_->codec, params);

  if (ret < 0) {
    error_ = "Could not copy codec parameters";
    close();
    return false;
  }

  ret = avcodec_open2(impl_->codec, impl_->decoder, nullptr);

  if (ret < 0) {
    char err[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(ret, err, sizeof(err));
    error_ = std::string("Could not open codec: ") + err;
    close();
    return false;
  }

  impl_->decoded = av_frame_alloc();
  impl_->rgba = av_frame_alloc();
  impl_->packet = av_packet_alloc();

  if (!impl_->decoded || !impl_->rgba || !impl_->packet) {
    error_ = "Could not allocate video frames";
    close();
    return false;
  }

  const int width = impl_->codec->width;
  const int height = impl_->codec->height;

  if (width <= 0 || height <= 0) {
    error_ = "Invalid video dimensions";
    close();
    return false;
  }

  frame_.width = width;
  frame_.height = height;
  frame_.rgba.assign(static_cast<std::size_t>(width * height * 4), 0);

  impl_->rgbaBuffer.resize(static_cast<std::size_t>(width * height * 4));
  av_image_fill_arrays(
    impl_->rgba->data,
    impl_->rgba->linesize,
    impl_->rgbaBuffer.data(),
    AV_PIX_FMT_RGBA,
    width,
    height,
    1
  );

  impl_->sws = sws_getContext(
    width,
    height,
    impl_->codec->pix_fmt,
    width,
    height,
    AV_PIX_FMT_RGBA,
    SWS_BILINEAR,
    nullptr,
    nullptr,
    nullptr
  );

  if (!impl_->sws) {
    error_ = "Could not create video scaler";
    close();
    return false;
  }

  open_ = true;
  update();

  return true;
#endif
}

void VideoPlayer::close() {
#ifdef NSTV_USE_FFMPEG
  if (impl_) {
    if (impl_->packet) {
      av_packet_free(&impl_->packet);
    }

    if (impl_->decoded) {
      av_frame_free(&impl_->decoded);
    }

    if (impl_->rgba) {
      av_frame_free(&impl_->rgba);
    }

    if (impl_->sws) {
      sws_freeContext(impl_->sws);
      impl_->sws = nullptr;
    }

    if (impl_->codec) {
      avcodec_free_context(&impl_->codec);
    }

    if (impl_->format) {
      avformat_close_input(&impl_->format);
    }

    impl_->videoStream = -1;
    impl_->decoder = nullptr;
    impl_->rgbaBuffer.clear();
  }
#endif

  open_ = false;
  paused_ = false;
  frame_ = Bitmap{};
}

bool VideoPlayer::update() {
  if (!open_ || paused_) {
    return false;
  }

#ifndef NSTV_USE_FFMPEG
  return false;
#else
  if (!impl_ || !impl_->format || !impl_->codec || !impl_->packet) {
    return false;
  }

  for (int attempts = 0; attempts < 64; ++attempts) {
    int ret = av_read_frame(impl_->format, impl_->packet);

    if (ret < 0) {
      char err[AV_ERROR_MAX_STRING_SIZE] = {};
      av_strerror(ret, err, sizeof(err));
      error_ = std::string("Stream read finished or failed: ") + err;
      return false;
    }

    if (impl_->packet->stream_index != impl_->videoStream) {
      av_packet_unref(impl_->packet);
      continue;
    }

    ret = avcodec_send_packet(impl_->codec, impl_->packet);
    av_packet_unref(impl_->packet);

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
      continue;
    }

    ret = avcodec_receive_frame(impl_->codec, impl_->decoded);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      continue;
    }

    if (ret < 0) {
      continue;
    }

    sws_scale(
      impl_->sws,
      impl_->decoded->data,
      impl_->decoded->linesize,
      0,
      impl_->codec->height,
      impl_->rgba->data,
      impl_->rgba->linesize
    );

    const int width = impl_->codec->width;
    const int height = impl_->codec->height;
    const int srcStride = impl_->rgba->linesize[0];

    if (frame_.width != width || frame_.height != height) {
      frame_.width = width;
      frame_.height = height;
      frame_.rgba.resize(static_cast<std::size_t>(width * height * 4));
    }

    for (int y = 0; y < height; ++y) {
      std::memcpy(
        frame_.rgba.data() + static_cast<std::size_t>(y * width * 4),
        impl_->rgba->data[0] + static_cast<std::size_t>(y * srcStride),
        static_cast<std::size_t>(width * 4)
      );
    }

    av_frame_unref(impl_->decoded);
    return true;
  }

  return false;
#endif
}

void VideoPlayer::togglePause() {
  if (!open_) {
    return;
  }

  paused_ = !paused_;
}

} // namespace nstv
