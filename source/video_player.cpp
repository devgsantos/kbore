#include "nstv/video_player.hpp"
#include <algorithm>
#include <cstring>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

#ifdef NSTV_USE_SDL
#include <SDL2/SDL.h>
#endif

namespace nstv {

#ifdef NSTV_USE_FFMPEG
struct VideoPlayer::Impl {
  AVFormatContext *format = nullptr;

  AVCodecContext *videoCodec = nullptr;
  AVCodecContext *audioCodec = nullptr;

  const AVCodec *videoDecoder = nullptr;
  const AVCodec *audioDecoder = nullptr;

  AVFrame *decodedVideo = nullptr;
  AVFrame *yuv = nullptr;
  AVFrame *decodedAudio = nullptr;

  AVPacket *packet = nullptr;

  SwsContext *sws = nullptr;
  SwrContext *swr = nullptr;

  int videoStream = -1;
  int audioStream = -1;

  int outputWidth = 0;
  int outputHeight = 0;

  std::vector<uint8_t> yuvBuffer;
  std::vector<uint8_t> audioBuffer;

#ifdef NSTV_USE_SDL
  SDL_AudioDeviceID audioDevice = 0;
#endif
};
#endif

namespace {

#ifdef NSTV_USE_FFMPEG
static std::string ffmpegError(int code) {
  char err[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, err, sizeof(err));
  return err;
}

static int scaledWidth(int width, int height, int maxWidth, int maxHeight) {
  if (width <= 0 || height <= 0) return width;
  float scale = std::min(float(maxWidth) / float(width), float(maxHeight) / float(height));
  scale = std::min(1.0f, scale);
  int out = std::max(2, int(width * scale));
  return out & ~1;
}

static int scaledHeight(int width, int height, int maxWidth, int maxHeight) {
  if (width <= 0 || height <= 0) return height;
  float scale = std::min(float(maxWidth) / float(width), float(maxHeight) / float(height));
  scale = std::min(1.0f, scale);
  int out = std::max(2, int(height * scale));
  return out & ~1;
}

static void copyPlane(std::vector<uint8_t> &dst, const uint8_t *src, int srcStride, int width, int height, int pitch) {
  dst.resize(std::size_t(pitch * height));
  for (int y = 0; y < height; ++y) {
    std::memcpy(dst.data() + std::size_t(y * pitch), src + std::size_t(y * srcStride), std::size_t(width));
  }
}
#endif

} // namespace

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

#ifdef NSTV_USE_SDL
  SDL_InitSubSystem(SDL_INIT_AUDIO);
#endif

  AVDictionary *options = nullptr;
  av_dict_set(&options, "user_agent", "NSTV-Switch/0.5", 0);
  av_dict_set(&options, "reconnect", "1", 0);
  av_dict_set(&options, "reconnect_streamed", "1", 0);
  av_dict_set(&options, "reconnect_delay_max", "5", 0);
  av_dict_set(&options, "timeout", "8000000", 0);
  av_dict_set(&options, "rw_timeout", "8000000", 0);
  av_dict_set(&options, "fflags", "nobuffer", 0);
  av_dict_set(&options, "flags", "low_delay", 0);
  av_dict_set(&options, "analyzeduration", "1000000", 0);
  av_dict_set(&options, "probesize", "65536", 0);

  int ret = avformat_open_input(&impl_->format, url.c_str(), nullptr, &options);
  av_dict_free(&options);

  if (ret < 0) {
    error_ = std::string("Could not open stream: ") + ffmpegError(ret);
    close();
    return false;
  }

  ret = avformat_find_stream_info(impl_->format, nullptr);
  if (ret < 0) {
    error_ = std::string("Could not read stream info: ") + ffmpegError(ret);
    close();
    return false;
  }

  for (unsigned int i = 0; i < impl_->format->nb_streams; ++i) {
    AVCodecParameters *params = impl_->format->streams[i]->codecpar;
    if (params->codec_type == AVMEDIA_TYPE_VIDEO && impl_->videoStream < 0) impl_->videoStream = int(i);
    if (params->codec_type == AVMEDIA_TYPE_AUDIO && impl_->audioStream < 0) impl_->audioStream = int(i);
  }

  if (impl_->videoStream < 0) {
    error_ = "No video stream found";
    close();
    return false;
  }

  AVCodecParameters *videoParams = impl_->format->streams[impl_->videoStream]->codecpar;
  impl_->videoDecoder = avcodec_find_decoder(videoParams->codec_id);
  if (!impl_->videoDecoder) {
    error_ = "Unsupported video codec";
    close();
    return false;
  }

  impl_->videoCodec = avcodec_alloc_context3(impl_->videoDecoder);
  if (!impl_->videoCodec) {
    error_ = "Could not allocate video codec context";
    close();
    return false;
  }

  ret = avcodec_parameters_to_context(impl_->videoCodec, videoParams);
  if (ret < 0) {
    error_ = "Could not copy video codec parameters";
    close();
    return false;
  }

  impl_->videoCodec->thread_count = 2;
  impl_->videoCodec->thread_type = FF_THREAD_FRAME;
  impl_->videoCodec->flags2 |= AV_CODEC_FLAG2_FAST;

  AVDictionary *codecOptions = nullptr;
  av_dict_set(&codecOptions, "threads", "2", 0);
  av_dict_set(&codecOptions, "flags2", "+fast", 0);

  ret = avcodec_open2(impl_->videoCodec, impl_->videoDecoder, &codecOptions);
  av_dict_free(&codecOptions);

  if (ret < 0) {
    error_ = std::string("Could not open video codec: ") + ffmpegError(ret);
    close();
    return false;
  }

  impl_->decodedVideo = av_frame_alloc();
  impl_->yuv = av_frame_alloc();
  impl_->packet = av_packet_alloc();

  if (!impl_->decodedVideo || !impl_->yuv || !impl_->packet) {
    error_ = "Could not allocate video frames";
    close();
    return false;
  }

  const int srcW = impl_->videoCodec->width;
  const int srcH = impl_->videoCodec->height;
  if (srcW <= 0 || srcH <= 0) {
    error_ = "Invalid video dimensions";
    close();
    return false;
  }

  // 960x540 is a practical software decode/render target on Switch. It keeps
  // aspect ratio, reduces memory bandwidth, and avoids RGBA conversion.
  impl_->outputWidth = scaledWidth(srcW, srcH, 960, 540);
  impl_->outputHeight = scaledHeight(srcW, srcH, 960, 540);

  int yuvSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, impl_->outputWidth, impl_->outputHeight, 1);
  impl_->yuvBuffer.resize(std::size_t(std::max(0, yuvSize)));
  av_image_fill_arrays(
    impl_->yuv->data,
    impl_->yuv->linesize,
    impl_->yuvBuffer.data(),
    AV_PIX_FMT_YUV420P,
    impl_->outputWidth,
    impl_->outputHeight,
    1
  );

  impl_->sws = sws_getContext(
    srcW,
    srcH,
    impl_->videoCodec->pix_fmt,
    impl_->outputWidth,
    impl_->outputHeight,
    AV_PIX_FMT_YUV420P,
    SWS_FAST_BILINEAR,
    nullptr,
    nullptr,
    nullptr
  );

  if (!impl_->sws) {
    error_ = "Could not create video scaler";
    close();
    return false;
  }

  yuvFrame_.width = impl_->outputWidth;
  yuvFrame_.height = impl_->outputHeight;
  yuvFrame_.yPitch = impl_->outputWidth;
  yuvFrame_.uPitch = impl_->outputWidth / 2;
  yuvFrame_.vPitch = impl_->outputWidth / 2;

  // Audio is optional. If it fails, video continues.
  if (impl_->audioStream >= 0) {
    AVCodecParameters *audioParams = impl_->format->streams[impl_->audioStream]->codecpar;
    impl_->audioDecoder = avcodec_find_decoder(audioParams->codec_id);
    if (impl_->audioDecoder) {
      impl_->audioCodec = avcodec_alloc_context3(impl_->audioDecoder);
      if (impl_->audioCodec && avcodec_parameters_to_context(impl_->audioCodec, audioParams) >= 0) {
        impl_->audioCodec->thread_count = 1;
        if (avcodec_open2(impl_->audioCodec, impl_->audioDecoder, nullptr) >= 0) {
          impl_->decodedAudio = av_frame_alloc();

#ifdef NSTV_USE_SDL
          SDL_AudioSpec desired{};
          desired.freq = 48000;
          desired.format = AUDIO_S16SYS;
          desired.channels = 2;
          desired.samples = 2048;
          desired.callback = nullptr;
          SDL_AudioSpec obtained{};
          impl_->audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
          if (impl_->audioDevice != 0) SDL_PauseAudioDevice(impl_->audioDevice, 0);
#endif

          AVChannelLayout inLayout;
          AVChannelLayout outLayout;
          av_channel_layout_default(&outLayout, 2);

          if (impl_->audioCodec->ch_layout.nb_channels > 0) {
            av_channel_layout_copy(&inLayout, &impl_->audioCodec->ch_layout);
          } else {
            av_channel_layout_default(&inLayout, 2);
          }

          int swrRet = swr_alloc_set_opts2(
            &impl_->swr,
            &outLayout,
            AV_SAMPLE_FMT_S16,
            48000,
            &inLayout,
            impl_->audioCodec->sample_fmt,
            impl_->audioCodec->sample_rate,
            0,
            nullptr
          );

          if (swrRet >= 0 && impl_->swr) swrRet = swr_init(impl_->swr);

          av_channel_layout_uninit(&inLayout);
          av_channel_layout_uninit(&outLayout);

          if (swrRet < 0 && impl_->swr) swr_free(&impl_->swr);
        }
      }
    }
  }

  open_ = true;
  for (int i = 0; i < 6 && !yuvFrame_.valid(); ++i) update();
  return true;
#endif
}

void VideoPlayer::close() {
#ifdef NSTV_USE_FFMPEG
  if (impl_) {
#ifdef NSTV_USE_SDL
    if (impl_->audioDevice != 0) {
      SDL_ClearQueuedAudio(impl_->audioDevice);
      SDL_CloseAudioDevice(impl_->audioDevice);
      impl_->audioDevice = 0;
    }
#endif
    if (impl_->packet) av_packet_free(&impl_->packet);
    if (impl_->decodedVideo) av_frame_free(&impl_->decodedVideo);
    if (impl_->decodedAudio) av_frame_free(&impl_->decodedAudio);
    if (impl_->yuv) av_frame_free(&impl_->yuv);
    if (impl_->sws) { sws_freeContext(impl_->sws); impl_->sws = nullptr; }
    if (impl_->swr) swr_free(&impl_->swr);
    if (impl_->videoCodec) avcodec_free_context(&impl_->videoCodec);
    if (impl_->audioCodec) avcodec_free_context(&impl_->audioCodec);
    if (impl_->format) avformat_close_input(&impl_->format);

    impl_->videoStream = -1;
    impl_->audioStream = -1;
    impl_->videoDecoder = nullptr;
    impl_->audioDecoder = nullptr;
    impl_->outputWidth = 0;
    impl_->outputHeight = 0;
    impl_->yuvBuffer.clear();
    impl_->audioBuffer.clear();
  }
#endif

  open_ = false;
  paused_ = false;
  frame_ = Bitmap{};
  yuvFrame_ = YuvFrame{};
}

bool VideoPlayer::update() {
  if (!open_ || paused_) return false;

#ifndef NSTV_USE_FFMPEG
  return false;
#else
  if (!impl_ || !impl_->format || !impl_->videoCodec || !impl_->packet) return false;

  bool gotVideo = false;

  for (int attempts = 0; attempts < 192; ++attempts) {
    int ret = av_read_frame(impl_->format, impl_->packet);

    if (ret < 0) {
      if (!yuvFrame_.valid()) error_ = std::string("Stream read failed: ") + ffmpegError(ret);
      return gotVideo;
    }

    if (impl_->packet->stream_index == impl_->videoStream) {
      ret = avcodec_send_packet(impl_->videoCodec, impl_->packet);
      av_packet_unref(impl_->packet);

      if (ret < 0 && ret != AVERROR(EAGAIN)) continue;

      while (true) {
        ret = avcodec_receive_frame(impl_->videoCodec, impl_->decodedVideo);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        sws_scale(
          impl_->sws,
          impl_->decodedVideo->data,
          impl_->decodedVideo->linesize,
          0,
          impl_->videoCodec->height,
          impl_->yuv->data,
          impl_->yuv->linesize
        );

        yuvFrame_.width = impl_->outputWidth;
        yuvFrame_.height = impl_->outputHeight;
        yuvFrame_.yPitch = impl_->outputWidth;
        yuvFrame_.uPitch = impl_->outputWidth / 2;
        yuvFrame_.vPitch = impl_->outputWidth / 2;

        copyPlane(yuvFrame_.y, impl_->yuv->data[0], impl_->yuv->linesize[0], impl_->outputWidth, impl_->outputHeight, yuvFrame_.yPitch);
        copyPlane(yuvFrame_.u, impl_->yuv->data[1], impl_->yuv->linesize[1], impl_->outputWidth / 2, impl_->outputHeight / 2, yuvFrame_.uPitch);
        copyPlane(yuvFrame_.v, impl_->yuv->data[2], impl_->yuv->linesize[2], impl_->outputWidth / 2, impl_->outputHeight / 2, yuvFrame_.vPitch);

        // Mark bitmap invalid. The renderer should use the YUV path.
        frame_ = Bitmap{};

        av_frame_unref(impl_->decodedVideo);
        gotVideo = true;

        if (attempts > 12) return true;
      }

      continue;
    }

    if (impl_->packet->stream_index == impl_->audioStream && impl_->audioCodec && impl_->decodedAudio) {
      ret = avcodec_send_packet(impl_->audioCodec, impl_->packet);
      av_packet_unref(impl_->packet);

      if (ret < 0 && ret != AVERROR(EAGAIN)) continue;

      while (true) {
        ret = avcodec_receive_frame(impl_->audioCodec, impl_->decodedAudio);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        if (impl_->swr) {
          const int outSamples = int(av_rescale_rnd(
            swr_get_delay(impl_->swr, impl_->audioCodec->sample_rate) + impl_->decodedAudio->nb_samples,
            48000,
            impl_->audioCodec->sample_rate,
            AV_ROUND_UP
          ));

          const int outBytes = outSamples * 2 * int(sizeof(int16_t));
          impl_->audioBuffer.resize(std::size_t(outBytes));
          uint8_t *outPlanes[2] = { impl_->audioBuffer.data(), nullptr };

          int converted = swr_convert(
            impl_->swr,
            outPlanes,
            outSamples,
            const_cast<const uint8_t **>(impl_->decodedAudio->extended_data),
            impl_->decodedAudio->nb_samples
          );

          if (converted > 0) {
            int bytes = converted * 2 * int(sizeof(int16_t));
#ifdef NSTV_USE_SDL
            if (impl_->audioDevice != 0) {
              Uint32 queued = SDL_GetQueuedAudioSize(impl_->audioDevice);
              if (queued < 48000 * 2 * 2) {
                SDL_QueueAudio(impl_->audioDevice, impl_->audioBuffer.data(), Uint32(bytes));
              }
            }
#endif
          }
        }

        av_frame_unref(impl_->decodedAudio);
      }

      continue;
    }

    av_packet_unref(impl_->packet);
    if (gotVideo && attempts > 24) return true;
  }

  return gotVideo;
#endif
}

void VideoPlayer::togglePause() {
  if (!open_) return;

  paused_ = !paused_;

#ifdef NSTV_USE_FFMPEG
#ifdef NSTV_USE_SDL
  if (impl_ && impl_->audioDevice != 0) SDL_PauseAudioDevice(impl_->audioDevice, paused_ ? 1 : 0);
#endif
#endif
}

} // namespace nstv
