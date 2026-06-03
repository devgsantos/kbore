#include "nstv/native_decoder.hpp"
#include "nstv/log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libavutil/version.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#endif

#ifdef NSTV_USE_SDL
#include <SDL2/SDL.h>
#endif

namespace nstv {

#ifdef NSTV_USE_FFMPEG
struct NativeDecoder::Impl {
  AVCodecContext *videoCodec = nullptr;
  AVCodecContext *audioCodec = nullptr;

  const AVCodec *videoDecoder = nullptr;
  const AVCodec *audioDecoder = nullptr;

  AVPacket *packet = nullptr;
  AVFrame *videoFrame = nullptr;
  AVFrame *transferredFrame = nullptr;
  AVFrame *convertedYuvFrame = nullptr;

  AVFrame *audioFrame = nullptr;
  SwrContext *swr = nullptr;

  std::vector<uint8_t> audioBuffer;

  #ifdef NSTV_USE_SDL
  SDL_AudioDeviceID audioDevice = 0;
  SDL_AudioSpec audioSpec{};
  #endif

  bool audioStarted = false;
  int audioOutSampleRate = 48000;
  int audioOutChannels = 2;
  AVSampleFormat audioOutFormat = AV_SAMPLE_FMT_S16;

  SwsContext *sws = nullptr;

  std::vector<uint8_t> convertedYuvBuffer;
  uint8_t *alignedTransferBuffer = nullptr;
  std::size_t alignedTransferBufferSize = 0;

  int swsSrcWidth = 0;
  int swsSrcHeight = 0;
  int swsDstWidth = 0;
  int swsDstHeight = 0;
  AVPixelFormat swsSrcFormat = AV_PIX_FMT_NONE;

  bool usingHardware = false;
  AVPixelFormat selectedHwPixelFormat = AV_PIX_FMT_NONE;

  NativeVideoSurfaceInfo latestNativeSurfaceInfo;
  bool latestHardwareFrameRetained = false;
  unsigned int nativeSurfaceLogCount = 0;
  unsigned int retainedHardwareFrameLogCount = 0;
};
#endif

namespace {

#ifdef NSTV_USE_FFMPEG

std::string ffmpegError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

void noopBufferFree(void *opaque, uint8_t *data) {
  (void)opaque;
  (void)data;
}

int audioChannels(const AVCodecContext *ctx) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
  return ctx->ch_layout.nb_channels;
#else
  return ctx->channels;
#endif
}

AVChannelLayout makeDefaultLayout(int channels) {
  AVChannelLayout layout;
  std::memset(&layout, 0, sizeof(layout));

  if (channels <= 0) {
    channels = 2;
  }

  av_channel_layout_default(&layout, channels);
  return layout;
}

int safeAudioSampleRate(AVCodecContext *ctx) {
  if (!ctx || ctx->sample_rate <= 0) {
    return 48000;
  }

  return ctx->sample_rate;
}

AVSampleFormat safeAudioSampleFormat(AVCodecContext *ctx) {
  if (!ctx || ctx->sample_fmt == AV_SAMPLE_FMT_NONE) {
    return AV_SAMPLE_FMT_FLTP;
  }

  return ctx->sample_fmt;
}

int safeAudioChannels(AVCodecContext *ctx) {
  if (!ctx) {
    return 2;
  }

#if LIBAVUTIL_VERSION_MAJOR >= 57
  if (ctx->ch_layout.nb_channels > 0) {
    return ctx->ch_layout.nb_channels;
  }
#else
  if (ctx->channels > 0) {
    return ctx->channels;
  }
#endif

  return 2;
}

AVChannelLayout safeInputLayout(AVCodecContext *ctx) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
  if (ctx && ctx->ch_layout.nb_channels > 0) {
    AVChannelLayout copy;
    std::memset(&copy, 0, sizeof(copy));

    if (av_channel_layout_copy(&copy, &ctx->ch_layout) == 0) {
      return copy;
    }
  }
#endif

  return makeDefaultLayout(safeAudioChannels(ctx));
}

std::string pixelFormatName(AVPixelFormat format) {
  const char *name = av_get_pix_fmt_name(format);

  if (name && name[0] != '\0') {
    return name;
  }

  return "unknown";
}

AVPixelFormat getHardwareFormat(AVCodecContext *ctx, const AVPixelFormat *formats) {
  AVPixelFormat wanted = static_cast<AVPixelFormat>(
    static_cast<intptr_t>(reinterpret_cast<intptr_t>(ctx->opaque))
  );

  for (const AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; ++p) {
    if (*p == wanted) {
      return *p;
    }
  }

  return formats[0];
}

int64_t framePtsMs(AVFrame *frame, AVStream *stream) {
  if (!frame || !stream) {
    return -1;
  }

  int64_t pts = frame->best_effort_timestamp;

  if (pts == AV_NOPTS_VALUE) {
    pts = frame->pts;
  }

  if (pts == AV_NOPTS_VALUE) {
    return -1;
  }

  AVRational milliseconds;
  milliseconds.num = 1;
  milliseconds.den = 1000;

  return av_rescale_q(pts, stream->time_base, milliseconds);
}

void copyPlane(
  std::vector<uint8_t> &dst,
  const uint8_t *src,
  int srcStride,
  int width,
  int height,
  int dstPitch
) {
  dst.resize(static_cast<std::size_t>(dstPitch * height));

  for (int y = 0; y < height; ++y) {
    std::memcpy(
      dst.data() + static_cast<std::size_t>(y * dstPitch),
      src + static_cast<std::size_t>(y * srcStride),
      static_cast<std::size_t>(width)
    );
  }
}

bool copyAvFrameToYuvFrame(
  AVFrame *frame,
  YuvFrame &out,
  std::string &error
) {
  if (!frame) {
    error = "copyAvFrameToYuvFrame received a null frame.";
    return false;
  }

  const int width = frame->width;
  const int height = frame->height;

  if (width <= 0 || height <= 0) {
    error = "copyAvFrameToYuvFrame received invalid frame dimensions.";
    return false;
  }

  const AVPixelFormat pixelFormat = static_cast<AVPixelFormat>(frame->format);

  if (pixelFormat == AV_PIX_FMT_NV12) {
    out.width = width;
    out.height = height;
    out.format = YuvFrame::Format::NV12;
    out.yPitch = width;
    out.uPitch = width;
    out.vPitch = 0;

    copyPlane(
      out.y,
      frame->data[0],
      frame->linesize[0],
      width,
      height,
      out.yPitch
    );

    copyPlane(
      out.u,
      frame->data[1],
      frame->linesize[1],
      width,
      (height + 1) / 2,
      out.uPitch
    );

    out.v.clear();
    return true;
  }

  if (pixelFormat != AV_PIX_FMT_YUV420P && pixelFormat != AV_PIX_FMT_YUVJ420P) {
    error =
      "copyAvFrameToYuvFrame expected yuv420p/yuvj420p/nv12 but received: " +
      pixelFormatName(pixelFormat);

    return false;
  }

  out.width = width;
  out.height = height;
  out.format = YuvFrame::Format::IYUV;

  out.yPitch = width;
  out.uPitch = width / 2;
  out.vPitch = width / 2;

  copyPlane(
    out.y,
    frame->data[0],
    frame->linesize[0],
    width,
    height,
    out.yPitch
  );

  copyPlane(
    out.u,
    frame->data[1],
    frame->linesize[1],
    width / 2,
    height / 2,
    out.uPitch
  );

  copyPlane(
    out.v,
    frame->data[2],
    frame->linesize[2],
    width / 2,
    height / 2,
    out.vPitch
  );

  return true;
}

bool prepareAlignedTransferFrame(
  AVFrame *source,
  AVFrame *target,
  uint8_t *&alignedTransferBuffer,
  std::size_t &alignedTransferBufferSize,
  AVPixelFormat &transferFormat,
  std::string &error
) {
  transferFormat = AV_PIX_FMT_NONE;

  if (!source || !source->hw_frames_ctx || !target) {
    error = "NativeDecoder cannot prepare aligned transfer frame without a hardware frame context.";
    return false;
  }

  AVPixelFormat *formats = nullptr;
  int ret = av_hwframe_transfer_get_formats(
    source->hw_frames_ctx,
    AV_HWFRAME_TRANSFER_DIRECTION_FROM,
    &formats,
    0
  );

  if (ret < 0 || !formats || formats[0] == AV_PIX_FMT_NONE) {
    if (formats) {
      av_freep(&formats);
    }

    error = "NativeDecoder could not query NVTEGRA transfer formats: " + ffmpegError(ret);
    return false;
  }

  transferFormat = formats[0];
  av_freep(&formats);

  if (transferFormat != AV_PIX_FMT_NV12 &&
      transferFormat != AV_PIX_FMT_YUV420P &&
      transferFormat != AV_PIX_FMT_YUVJ420P) {
    av_frame_unref(target);
    target->format = transferFormat;
    target->width = source->width;
    target->height = source->height;
    return true;
  }

  const int width = source->width;
  const int height = source->height;
  const int bufferSize = av_image_get_buffer_size(
    transferFormat,
    width,
    height,
    256
  );

  if (width <= 0 || height <= 0 || bufferSize <= 0) {
    error = "NativeDecoder could not size aligned transfer buffer.";
    return false;
  }

  const std::size_t required = static_cast<std::size_t>((bufferSize + 255) & ~255);

  if (required > alignedTransferBufferSize) {
    if (alignedTransferBuffer) {
      std::free(alignedTransferBuffer);
      alignedTransferBuffer = nullptr;
      alignedTransferBufferSize = 0;
    }

    alignedTransferBuffer = static_cast<uint8_t *>(std::aligned_alloc(256, required));

    if (!alignedTransferBuffer) {
      error = "NativeDecoder could not allocate aligned transfer buffer.";
      return false;
    }

    alignedTransferBufferSize = required;
  }

  av_frame_unref(target);
  target->format = transferFormat;
  target->width = width;
  target->height = height;

  ret = av_image_fill_arrays(
    target->data,
    target->linesize,
    alignedTransferBuffer,
    transferFormat,
    width,
    height,
    256
  );

  if (ret < 0) {
    error = "NativeDecoder could not fill aligned transfer frame: " + ffmpegError(ret);
    return false;
  }

  target->buf[0] = av_buffer_create(
    alignedTransferBuffer,
    required,
    noopBufferFree,
    nullptr,
    0
  );

  if (!target->buf[0]) {
    error = "NativeDecoder could not create aligned transfer buffer reference.";
    return false;
  }

  return true;
}

#endif

} // namespace

NativeDecoder::NativeDecoder() {
#ifdef NSTV_USE_FFMPEG
  impl_ = new Impl();
#endif
}

NativeDecoder::~NativeDecoder() {
  close();

#ifdef NSTV_USE_FFMPEG
  delete impl_;
  impl_ = nullptr;
#endif
}

void NativeDecoder::resetState() {
  video_ = NativeDecoderInfo{};
  audio_ = NativeDecoderInfo{};

  firstVideoFrame_ = NativeFrameInfo{};
  latestFrameInfo_ = NativeFrameInfo{};
#ifdef NSTV_USE_FFMPEG
  if (impl_) {
    impl_->latestNativeSurfaceInfo = NativeVideoSurfaceInfo{};
    impl_->nativeSurfaceLogCount = 0;
    impl_->retainedHardwareFrameLogCount = 0;
  }
#endif

  firstYuvFrame_ = YuvFrame{};
  latestYuvFrame_ = YuvFrame{};

  error_.clear();
  summary_.clear();
}

void NativeDecoder::rebuildSummary() {
  std::ostringstream out;

  if (firstVideoFrame_.decoded) {
    out
      << "first frame="
      << firstVideoFrame_.width
      << "x"
      << firstVideoFrame_.height
      << " output="
      << firstVideoFrame_.outputWidth
      << "x"
      << firstVideoFrame_.outputHeight
      << " pix_fmt="
      << firstVideoFrame_.pixelFormat
      << " hwFrame="
      << (firstVideoFrame_.hardwareFrame ? "yes" : "no")
      << " nativeSurface="
      << (firstVideoFrame_.nativeSurfaceAvailable ? "yes" : "no")
      << " transferred="
      << (firstVideoFrame_.transferredFromHardware ? "yes" : "no")
      << " ptsMs="
      << firstVideoFrame_.ptsMs
      << " stream="
      << firstVideoFrame_.streamIndex
      << " | ";
  }

  if (latestFrameInfo_.decoded) {
    out
      << "latest frame ptsMs="
      << latestFrameInfo_.ptsMs
      << " pix_fmt="
      << latestFrameInfo_.pixelFormat
      << " hwFrame="
      << (latestFrameInfo_.hardwareFrame ? "yes" : "no")
      << " nativeSurface="
      << (latestFrameInfo_.nativeSurfaceAvailable ? "yes" : "no")
      << " transferred="
      << (latestFrameInfo_.transferredFromHardware ? "yes" : "no")
      << " | ";
  }

  if (video_.opened) {
    out
      << "video decoder="
      << video_.decoderName
      << " codec="
      << video_.codecName
      << " "
      << video_.width
      << "x"
      << video_.height
      << " stream="
      << video_.streamIndex
      << " usingHardware="
      << (video_.usingHardware ? "yes" : "no")
      << " hwPixFmt="
      << (video_.hwPixelFormat.empty() ? "none" : video_.hwPixelFormat);
  } else {
    out << "video decoder=not-opened";
  }

  if (audio_.opened) {
    out
      << " | audio decoder="
      << audio_.decoderName
      << " codec="
      << audio_.codecName
      << " "
      << audio_.channels
      << "ch "
      << audio_.sampleRate
      << "Hz"
      << " stream="
      << audio_.streamIndex;
  } else {
    out << " | audio decoder=not-opened";
  }

  if (latestYuvFrame_.valid()) {
    out
      << " | latestYuvFrame="
      << latestYuvFrame_.width
      << "x"
      << latestYuvFrame_.height;
  }

  summary_ = out.str();
}

bool NativeDecoder::openVideo(const NativeDemuxer &demuxer) {
#ifndef NSTV_USE_FFMPEG
  error_ = "NativeDecoder requires NSTV_USE_FFMPEG.";
  return false;
#else
  return openVideoInternal(demuxer, nullptr, AV_PIX_FMT_NONE, false);
#endif
}

#ifdef NSTV_USE_FFMPEG
bool NativeDecoder::openVideoHardware(
  const NativeDemuxer &demuxer,
  AVBufferRef *deviceContext,
  AVPixelFormat hwPixelFormat
) {
  return openVideoInternal(demuxer, deviceContext, hwPixelFormat, true);
}
#endif

#ifdef NSTV_USE_FFMPEG
const NativeVideoSurfaceInfo &NativeDecoder::latestNativeSurfaceInfo() const {
  static const NativeVideoSurfaceInfo empty;

  if (!impl_) {
    return empty;
  }

  return impl_->latestNativeSurfaceInfo;
}
#endif

#ifdef NSTV_USE_FFMPEG
bool NativeDecoder::openVideoInternal(
  const NativeDemuxer &demuxer,
  AVBufferRef *deviceContext,
  AVPixelFormat hwPixelFormat,
  bool useHardware
) {
  if (!impl_) {
    error_ = "NativeDecoder internal state is unavailable.";
    return false;
  }

  if (!demuxer.isOpen()) {
    error_ = "NativeDecoder requires an open NativeDemuxer.";
    return false;
  }

  if (!demuxer.video().exists) {
    error_ = "NativeDecoder did not receive a valid video stream.";
    return false;
  }

  if (useHardware && !deviceContext) {
    error_ = "NativeDecoder hardware open requested, but deviceContext is null.";
    return false;
  }

  if (useHardware && hwPixelFormat == AV_PIX_FMT_NONE) {
    error_ = "NativeDecoder hardware open requested, but hwPixelFormat is AV_PIX_FMT_NONE.";
    return false;
  }

  close();
  resetState();

  impl_->usingHardware = useHardware;
  impl_->selectedHwPixelFormat = hwPixelFormat;

  const NativeStreamInfo &videoStream = demuxer.video();

  AVFormatContext *format = demuxer.formatContext();

  if (!format) {
    error_ = "NativeDecoder could not access AVFormatContext from NativeDemuxer.";
    return false;
  }

  if (videoStream.index < 0 || videoStream.index >= static_cast<int>(format->nb_streams)) {
    error_ = "NativeDecoder received an invalid video stream index.";
    return false;
  }

  AVStream *stream = format->streams[videoStream.index];

  if (!stream || !stream->codecpar) {
    error_ = "NativeDecoder could not access video codec parameters.";
    return false;
  }

  AVCodecParameters *params = stream->codecpar;

  impl_->videoDecoder = avcodec_find_decoder(params->codec_id);

  if (!impl_->videoDecoder) {
    error_ = "NativeDecoder could not find video decoder for codec: " + videoStream.codecName;
    return false;
  }

  impl_->videoCodec = avcodec_alloc_context3(impl_->videoDecoder);

  if (!impl_->videoCodec) {
    error_ = "NativeDecoder could not allocate video AVCodecContext.";
    return false;
  }

  int ret = avcodec_parameters_to_context(impl_->videoCodec, params);

  if (ret < 0) {
    error_ = "NativeDecoder could not copy video parameters: " + ffmpegError(ret);
    close();
    return false;
  }

  impl_->videoCodec->thread_count = useHardware ? 1 : 2;
  impl_->videoCodec->thread_type = useHardware ? 0 : FF_THREAD_FRAME;
  impl_->videoCodec->flags2 |= AV_CODEC_FLAG2_FAST;

  if (useHardware) {
    impl_->videoCodec->hw_device_ctx = av_buffer_ref(deviceContext);

    if (!impl_->videoCodec->hw_device_ctx) {
      error_ = "NativeDecoder could not reference AVHWDeviceContext.";
      close();
      return false;
    }

    impl_->videoCodec->opaque = reinterpret_cast<void *>(
      static_cast<intptr_t>(hwPixelFormat)
    );

    impl_->videoCodec->get_format = getHardwareFormat;
  }

  AVDictionary *options = nullptr;

  av_dict_set(&options, "flags2", "+fast", 0);

  if (!useHardware) {
    av_dict_set(&options, "threads", "2", 0);
  }

  ret = avcodec_open2(impl_->videoCodec, impl_->videoDecoder, &options);

  av_dict_free(&options);

  if (ret < 0) {
    error_ = "NativeDecoder could not open video decoder: " + ffmpegError(ret);
    close();
    return false;
  }

  impl_->packet = av_packet_alloc();
  impl_->videoFrame = av_frame_alloc();
  impl_->transferredFrame = av_frame_alloc();
  impl_->convertedYuvFrame = av_frame_alloc();

  if (!impl_->packet || !impl_->videoFrame || !impl_->transferredFrame || !impl_->convertedYuvFrame) {
    error_ = "NativeDecoder could not allocate packet/frame.";
    close();
    return false;
  }

  video_.opened = true;
  video_.codecName = videoStream.codecName;
  video_.decoderName = impl_->videoDecoder->name ? impl_->videoDecoder->name : "unknown";
  video_.streamIndex = videoStream.index;
  video_.width = impl_->videoCodec->width;
  video_.height = impl_->videoCodec->height;
  video_.usingHardware = useHardware;
  video_.hwPixelFormat = useHardware ? pixelFormatName(hwPixelFormat) : "none";

  rebuildSummary();

  return true;
}
#endif

bool NativeDecoder::openAudio(const NativeDemuxer &demuxer) {
#ifndef NSTV_USE_FFMPEG
  error_ = "NativeDecoder requires NSTV_USE_FFMPEG.";
  return false;
#else
  if (!impl_) {
    error_ = "NativeDecoder internal state is unavailable.";
    return false;
  }

  if (!demuxer.isOpen()) {
    error_ = "NativeDecoder requires an open NativeDemuxer.";
    return false;
  }

  if (!demuxer.audio().exists) {
    rebuildSummary();
    return true;
  }

  const NativeStreamInfo &audioStream = demuxer.audio();

  AVFormatContext *format = demuxer.formatContext();

  if (!format) {
    error_ = "NativeDecoder could not access AVFormatContext from NativeDemuxer.";
    return false;
  }

  if (audioStream.index < 0 || audioStream.index >= static_cast<int>(format->nb_streams)) {
    error_ = "NativeDecoder received an invalid audio stream index.";
    return false;
  }

  AVStream *stream = format->streams[audioStream.index];

  if (!stream || !stream->codecpar) {
    error_ = "NativeDecoder could not access audio codec parameters.";
    return false;
  }

  AVCodecParameters *params = stream->codecpar;

  impl_->audioDecoder = avcodec_find_decoder(params->codec_id);

  if (!impl_->audioDecoder) {
    rebuildSummary();
    return true;
  }

  impl_->audioCodec = avcodec_alloc_context3(impl_->audioDecoder);

  if (!impl_->audioCodec) {
    error_ = "NativeDecoder could not allocate audio AVCodecContext.";
    return false;
  }

  int ret = avcodec_parameters_to_context(impl_->audioCodec, params);

  if (ret < 0) {
    error_ = "NativeDecoder could not copy audio parameters: " + ffmpegError(ret);
    close();
    return false;
  }

  ret = avcodec_open2(impl_->audioCodec, impl_->audioDecoder, nullptr);

  if (ret < 0) {
    error_ = "NativeDecoder could not open audio decoder: " + ffmpegError(ret);
    close();
    return false;
  }

  audio_.opened = true;
  audio_.codecName = audioStream.codecName;
  audio_.decoderName = impl_->audioDecoder->name ? impl_->audioDecoder->name : "unknown";
  audio_.streamIndex = audioStream.index;
  audio_.sampleRate = safeAudioSampleRate(impl_->audioCodec);
  audio_.channels = safeAudioChannels(impl_->audioCodec);

  impl_->audioFrame = av_frame_alloc();

  if (!impl_->audioFrame) {
    error_ = "NativeDecoder could not allocate audio frame.";
    close();
    return false;
  }

  AVChannelLayout inLayout = safeInputLayout(impl_->audioCodec);
  AVChannelLayout outLayout = makeDefaultLayout(impl_->audioOutChannels);

  const int inSampleRate = safeAudioSampleRate(impl_->audioCodec);
  const AVSampleFormat inSampleFormat = safeAudioSampleFormat(impl_->audioCodec);

  ret = swr_alloc_set_opts2(
    &impl_->swr,
    &outLayout,
    impl_->audioOutFormat,
    impl_->audioOutSampleRate,
    &inLayout,
    inSampleFormat,
    inSampleRate,
    0,
    nullptr
  );

  av_channel_layout_uninit(&inLayout);
  av_channel_layout_uninit(&outLayout);

  if (ret < 0 || !impl_->swr) {
    std::ostringstream audioError;

    audioError
      << "NativeDecoder could not allocate SwrContext: "
      << ffmpegError(ret)
      << " | codec="
      << audio_.codecName
      << " decoder="
      << audio_.decoderName
      << " inputSampleRate="
      << safeAudioSampleRate(impl_->audioCodec)
      << " inputChannels="
      << safeAudioChannels(impl_->audioCodec)
      << " inputSampleFormat="
      << static_cast<int>(safeAudioSampleFormat(impl_->audioCodec))
      << " outputSampleRate="
      << impl_->audioOutSampleRate
      << " outputChannels="
      << impl_->audioOutChannels;

    error_ = audioError.str();

    close();
    return false;
  }

  ret = swr_init(impl_->swr);

  if (ret < 0) {
    std::ostringstream audioError;

    audioError
      << "NativeDecoder could not initialize SwrContext: "
      << ffmpegError(ret)
      << " | codec="
      << audio_.codecName
      << " decoder="
      << audio_.decoderName
      << " inputSampleRate="
      << safeAudioSampleRate(impl_->audioCodec)
      << " inputChannels="
      << safeAudioChannels(impl_->audioCodec)
      << " inputSampleFormat="
      << static_cast<int>(safeAudioSampleFormat(impl_->audioCodec))
      << " outputSampleRate="
      << impl_->audioOutSampleRate
      << " outputChannels="
      << impl_->audioOutChannels;

    error_ = audioError.str();

    close();
    return false;
  }
  #ifdef NSTV_USE_SDL
  if (impl_->audioDevice == 0) {
    SDL_AudioSpec wanted{};
    SDL_AudioSpec obtained{};

    wanted.freq = impl_->audioOutSampleRate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = static_cast<Uint8>(impl_->audioOutChannels);
    wanted.samples = 2048;
    wanted.callback = nullptr;
    wanted.userdata = nullptr;

    impl_->audioDevice = SDL_OpenAudioDevice(
      nullptr,
      0,
      &wanted,
      &obtained,
      0
    );

    if (impl_->audioDevice == 0) {
      error_ = std::string("SDL_OpenAudioDevice failed: ") + SDL_GetError();
      close();
      return false;
    }

    impl_->audioSpec = obtained;
  }
  #endif

  rebuildSummary();

  return true;
#endif
}

void NativeDecoder::startAudio() {
#ifdef NSTV_USE_SDL
  if (!impl_ || impl_->audioDevice == 0) {
    return;
  }

  SDL_PauseAudioDevice(impl_->audioDevice, 0);
  impl_->audioStarted = true;
#endif
}

void NativeDecoder::stopAudio() {
#ifdef NSTV_USE_SDL
  if (!impl_ || impl_->audioDevice == 0) {
    return;
  }

  SDL_PauseAudioDevice(impl_->audioDevice, 1);
  SDL_ClearQueuedAudio(impl_->audioDevice);
  impl_->audioStarted = false;
#endif
}

int NativeDecoder::audioQueuedBytes() const {
#ifdef NSTV_USE_SDL
  if (!impl_ || impl_->audioDevice == 0) {
    return 0;
  }

  return static_cast<int>(SDL_GetQueuedAudioSize(impl_->audioDevice));
#else
  return 0;
#endif
}

int NativeDecoder::audioQueuedMs() const {
#ifdef NSTV_USE_SDL
  if (!impl_ || impl_->audioDevice == 0) {
    return 0;
  }

  const Uint32 queued = SDL_GetQueuedAudioSize(impl_->audioDevice);
  const Uint32 bytesPerSample = SDL_AUDIO_BITSIZE(impl_->audioSpec.format) / 8u;

  if (impl_->audioSpec.freq <= 0 || impl_->audioSpec.channels <= 0 || bytesPerSample == 0) {
    return 0;
  }

  const Uint32 bytesPerSecond =
    static_cast<Uint32>(impl_->audioSpec.freq) *
    static_cast<Uint32>(impl_->audioSpec.channels) *
    bytesPerSample;

  if (bytesPerSecond == 0) {
    return 0;
  }

  return static_cast<int>((static_cast<unsigned long long>(queued) * 1000ull) / bytesPerSecond);
#else
  return 0;
#endif
}

void NativeDecoder::clearAudioQueue() {
#ifdef NSTV_USE_SDL
  if (!impl_ || impl_->audioDevice == 0) {
    return;
  }

  SDL_ClearQueuedAudio(impl_->audioDevice);
#endif
}

void NativeDecoder::flushForSeek() {
#ifdef NSTV_USE_FFMPEG
  if (!impl_) {
    return;
  }

  releaseLatestHardwareFrame();

  if (impl_->videoCodec) {
    avcodec_flush_buffers(impl_->videoCodec);
  }

  if (impl_->audioCodec) {
    avcodec_flush_buffers(impl_->audioCodec);
  }

  if (impl_->videoFrame) {
    av_frame_unref(impl_->videoFrame);
  }

  if (impl_->transferredFrame) {
    av_frame_unref(impl_->transferredFrame);
  }

  if (impl_->convertedYuvFrame) {
    av_frame_unref(impl_->convertedYuvFrame);
  }

  if (impl_->audioFrame) {
    av_frame_unref(impl_->audioFrame);
  }

  if (impl_->packet) {
    av_packet_unref(impl_->packet);
  }

  if (impl_->swr) {
    swr_close(impl_->swr);
    swr_init(impl_->swr);
  }

  clearAudioQueue();
  firstVideoFrame_ = NativeFrameInfo{};
  latestFrameInfo_ = NativeFrameInfo{};
  firstYuvFrame_ = YuvFrame{};
  latestYuvFrame_ = YuvFrame{};
#endif
}

#ifdef NSTV_USE_FFMPEG
bool NativeDecoder::decodeAudioPacketToSdl(AVPacket *packet) {
  if (!impl_ || !impl_->audioCodec || !impl_->audioFrame || !impl_->swr) {
    return true;
  }

  int ret = avcodec_send_packet(impl_->audioCodec, packet);

  if (ret < 0 && ret != AVERROR(EAGAIN)) {
    error_ = "NativeDecoder could not send audio packet: " + ffmpegError(ret);
    return false;
  }

  while (true) {
    ret = avcodec_receive_frame(impl_->audioCodec, impl_->audioFrame);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }

    if (ret < 0) {
      error_ = "NativeDecoder could not receive audio frame: " + ffmpegError(ret);
      return false;
    }

    const int outSamples = av_rescale_rnd(
      swr_get_delay(impl_->swr, impl_->audioCodec->sample_rate) + impl_->audioFrame->nb_samples,
      impl_->audioOutSampleRate,
      impl_->audioCodec->sample_rate,
      AV_ROUND_UP
    );

    if (outSamples <= 0) {
      av_frame_unref(impl_->audioFrame);
      continue;
    }

    const int bytesPerSample = av_get_bytes_per_sample(impl_->audioOutFormat);
    const int outBufferSize = outSamples * impl_->audioOutChannels * bytesPerSample;

    impl_->audioBuffer.resize(static_cast<std::size_t>(outBufferSize));

    uint8_t *outData[2] = {
      impl_->audioBuffer.data(),
      nullptr
    };

    ret = swr_convert(
      impl_->swr,
      outData,
      outSamples,
      const_cast<const uint8_t **>(impl_->audioFrame->extended_data),
      impl_->audioFrame->nb_samples
    );

    if (ret < 0) {
      error_ = "NativeDecoder swr_convert failed: " + ffmpegError(ret);
      av_frame_unref(impl_->audioFrame);
      return false;
    }

    const int convertedBytes =
      ret * impl_->audioOutChannels * bytesPerSample;

#ifdef NSTV_USE_SDL
    if (impl_->audioDevice != 0 && convertedBytes > 0) {
      const Uint32 queued = SDL_GetQueuedAudioSize(impl_->audioDevice);
      const Uint32 bytesPerSample = SDL_AUDIO_BITSIZE(impl_->audioSpec.format) / 8u;
      const Uint32 bytesPerSecond =
        static_cast<Uint32>(impl_->audioSpec.freq) *
        static_cast<Uint32>(impl_->audioSpec.channels) *
        bytesPerSample;
      /*
        O limite antigo (~2s) permitia que o áudio ficasse muito
        atrasado ou continuasse tocando muito à frente quando o vídeo
        precisava recuperar. Para streaming ao vivo, mantemos uma fila
        curta e descartamos áudio excedente em vez de acumular latência.
      */
      const Uint32 targetMaxQueue = (bytesPerSecond * 450u) / 1000u; // ~450ms
      const Uint32 emergencyMaxQueue = (bytesPerSecond * 800u) / 1000u; // ~800ms

      if (queued > emergencyMaxQueue) {
        SDL_ClearQueuedAudio(impl_->audioDevice);
      }

      const Uint32 refreshedQueued = SDL_GetQueuedAudioSize(impl_->audioDevice);
      const bool queueHasRoom = refreshedQueued < targetMaxQueue;

      if (queueHasRoom) {
        SDL_QueueAudio(
          impl_->audioDevice,
          impl_->audioBuffer.data(),
          static_cast<Uint32>(convertedBytes)
        );
      }
    }
#endif

    av_frame_unref(impl_->audioFrame);
  }

  return true;
}
#endif

bool NativeDecoder::decodeNextVideoFrame(
  NativeDemuxer &demuxer,
  bool outputFrame,
  YuvFrame *outputYuvFrame,
  int maxPackets
) {
#ifndef NSTV_USE_FFMPEG
  error_ = "NativeDecoder requires NSTV_USE_FFMPEG.";
  return false;
#else
  if (!impl_) {
    error_ = "NativeDecoder internal state is unavailable.";
    return false;
  }

  releaseLatestHardwareFrame();

  if (!video_.opened || !impl_->videoCodec || !impl_->packet || !impl_->videoFrame) {
    error_ = "NativeDecoder requires an open video decoder before decoding frames.";
    return false;
  }

  AVFormatContext *format = demuxer.formatContext();

  if (!format) {
    error_ = "NativeDecoder could not access AVFormatContext while decoding.";
    return false;
  }

  if (maxPackets <= 0) {
    maxPackets = 80;
  }

  for (int i = 0; i < maxPackets; ++i) {
    demuxer.beginIoGuard(900);
    int ret = av_read_frame(format, impl_->packet);
    const bool interrupted = demuxer.wasIoInterrupted();
    demuxer.endIoGuard();

    if (ret < 0 || interrupted) {
      error_ = interrupted
        ? "NativeDecoder video read interrupted by IO guard"
        : "NativeDecoder could not read next video frame: " + ffmpegError(ret);
      return false;
    }

    if (audio_.opened && impl_->packet->stream_index == audio_.streamIndex) {
      if (!decodeAudioPacketToSdl(impl_->packet)) {
        av_packet_unref(impl_->packet);
        return false;
      }

      av_packet_unref(impl_->packet);
      continue;
    }

    if (impl_->packet->stream_index != video_.streamIndex) {
      av_packet_unref(impl_->packet);
      continue;
    }

    ret = avcodec_send_packet(impl_->videoCodec, impl_->packet);

    av_packet_unref(impl_->packet);

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
      error_ = "NativeDecoder could not send video packet: " + ffmpegError(ret);
      return false;
    }

    while (true) {
      ret = avcodec_receive_frame(impl_->videoCodec, impl_->videoFrame);

      if (ret == AVERROR(EAGAIN)) {
        break;
      }

      if (ret == AVERROR_EOF) {
        error_ = "NativeDecoder reached EOF while decoding video.";
        return false;
      }

      if (ret < 0) {
        error_ = "NativeDecoder could not receive video frame: " + ffmpegError(ret);
        return false;
      }

      AVStream *videoStream = format->streams[video_.streamIndex];

      AVPixelFormat originalFormat = static_cast<AVPixelFormat>(impl_->videoFrame->format);

      bool hardwareFrame =
        impl_->usingHardware &&
        originalFormat == impl_->selectedHwPixelFormat;

      bool hasHardwareContext =
        hardwareFrame ||
        impl_->videoFrame->hw_frames_ctx != nullptr;

      /*
        Frame info inicial usando o frame original.

        Importante:
        Se outputFrame=false, vamos atualizar o relógio/PTS e descartar
        o frame SEM chamar av_hwframe_transfer_data().
      */
      NativeFrameInfo currentFrameInfo;

      currentFrameInfo.decoded = true;
      currentFrameInfo.streamIndex = video_.streamIndex;
      currentFrameInfo.width = impl_->videoFrame->width;
      currentFrameInfo.height = impl_->videoFrame->height;
      currentFrameInfo.outputWidth = impl_->videoFrame->width;
      currentFrameInfo.outputHeight = impl_->videoFrame->height;
      currentFrameInfo.pixelFormat = pixelFormatName(originalFormat);
      currentFrameInfo.ptsMs = framePtsMs(impl_->videoFrame, videoStream);
      currentFrameInfo.hardwareFrame = hardwareFrame;
      currentFrameInfo.transferredFromHardware = false;

      if (hasHardwareContext) {
        impl_->latestNativeSurfaceInfo = inspectNativeVideoSurface(impl_->videoFrame);
        currentFrameInfo.nativeSurfaceAvailable = impl_->latestNativeSurfaceInfo.valid;
        currentFrameInfo.nativeSurfaceSummary = impl_->latestNativeSurfaceInfo.summary;

        ++impl_->nativeSurfaceLogCount;
        if (impl_->nativeSurfaceLogCount <= 3 || impl_->nativeSurfaceLogCount % 120 == 0) {
          logLinef(
            "[KBORE][NVTEGRA] frame %u: %s",
            impl_->nativeSurfaceLogCount,
            impl_->latestNativeSurfaceInfo.summary.c_str()
          );
        }
      } else {
        impl_->latestNativeSurfaceInfo = NativeVideoSurfaceInfo{};
      }

      latestFrameInfo_ = currentFrameInfo;

      if (!firstVideoFrame_.decoded) {
        firstVideoFrame_ = currentFrameInfo;
      }

      /*
        Modo drop real.

        Se outputFrame=false, este frame só serve para avançar o decoder.
        Não fazemos:
          - av_hwframe_transfer_data()
          - sws_scale()
          - cópia para YuvFrame

        Isso é essencial para HD/FHD.
      */
      if (!outputFrame) {
        av_frame_unref(impl_->videoFrame);
        av_frame_unref(impl_->transferredFrame);

        rebuildSummary();

        return true;
      }

      AVFrame *frameForProcessing = impl_->videoFrame;
      bool transferredFromHardware = false;

      if (hasHardwareContext) {
        AVPixelFormat transferFormat = AV_PIX_FMT_NONE;
        std::string transferPrepError;

        if (!prepareAlignedTransferFrame(
              impl_->videoFrame,
              impl_->transferredFrame,
              impl_->alignedTransferBuffer,
              impl_->alignedTransferBufferSize,
              transferFormat,
              transferPrepError
            )) {
          error_ = transferPrepError;
          av_frame_unref(impl_->videoFrame);
          return false;
        }

        ret = av_hwframe_transfer_data(
          impl_->transferredFrame,
          impl_->videoFrame,
          0
        );

        if (ret < 0) {
          error_ = "NativeDecoder av_hwframe_transfer_data failed: " + ffmpegError(ret);
          av_frame_unref(impl_->videoFrame);
          return false;
        }

        frameForProcessing = impl_->transferredFrame;
        transferredFromHardware = true;
      }

      const int inputWidth = frameForProcessing->width;
      const int inputHeight = frameForProcessing->height;
      const AVPixelFormat inputFormat = static_cast<AVPixelFormat>(frameForProcessing->format);
      int outputWidth = inputWidth;
      int outputHeight = inputHeight;

      if (inputWidth > 1280 || inputHeight > 720) {
        const double scale = std::min(
          1280.0 / static_cast<double>(inputWidth),
          720.0 / static_cast<double>(inputHeight)
        );

        outputWidth = std::max(2, static_cast<int>(inputWidth * scale));
        outputHeight = std::max(2, static_cast<int>(inputHeight * scale));
        outputWidth &= ~1;
        outputHeight &= ~1;
      }

      /*
        Atualiza info depois da transferência.
        Aqui o pixel format normalmente deve virar yuv420p/nv12/etc.
      */
      latestFrameInfo_.width = inputWidth;
      latestFrameInfo_.height = inputHeight;
      latestFrameInfo_.outputWidth = outputWidth;
      latestFrameInfo_.outputHeight = outputHeight;
      latestFrameInfo_.pixelFormat = pixelFormatName(inputFormat);
      latestFrameInfo_.transferredFromHardware = transferredFromHardware;

      currentFrameInfo.decoded = true;
      currentFrameInfo.streamIndex = video_.streamIndex;
      currentFrameInfo.width = inputWidth;
      currentFrameInfo.height = inputHeight;
      currentFrameInfo.outputWidth = outputWidth;
      currentFrameInfo.outputHeight = outputHeight;
      currentFrameInfo.pixelFormat = pixelFormatName(inputFormat);
      currentFrameInfo.ptsMs = framePtsMs(impl_->videoFrame, videoStream);
      currentFrameInfo.hardwareFrame = hardwareFrame;
      currentFrameInfo.transferredFromHardware = transferredFromHardware;
      currentFrameInfo.nativeSurfaceAvailable = latestFrameInfo_.nativeSurfaceAvailable;
      currentFrameInfo.nativeSurfaceSummary = latestFrameInfo_.nativeSurfaceSummary;

      AVFrame *frameForCopy = frameForProcessing;

      const bool mustScaleForOutput =
        outputWidth != inputWidth ||
        outputHeight != inputHeight;

      const bool canCopyDirect =
        !mustScaleForOutput &&
        (inputFormat == AV_PIX_FMT_YUV420P ||
         inputFormat == AV_PIX_FMT_YUVJ420P);

      if (!canCopyDirect) {
        int bufferSize = av_image_get_buffer_size(
          AV_PIX_FMT_YUV420P,
          outputWidth,
          outputHeight,
          1
        );

        if (bufferSize <= 0) {
          error_ = "NativeDecoder could not calculate YUV conversion buffer size.";
          av_frame_unref(impl_->videoFrame);
          av_frame_unref(impl_->transferredFrame);
          return false;
        }

        impl_->convertedYuvBuffer.resize(static_cast<std::size_t>(bufferSize));

        av_image_fill_arrays(
          impl_->convertedYuvFrame->data,
          impl_->convertedYuvFrame->linesize,
          impl_->convertedYuvBuffer.data(),
          AV_PIX_FMT_YUV420P,
          outputWidth,
          outputHeight,
          1
        );

        impl_->convertedYuvFrame->width = outputWidth;
        impl_->convertedYuvFrame->height = outputHeight;
        impl_->convertedYuvFrame->format = AV_PIX_FMT_YUV420P;

        const bool mustRecreateSws =
          !impl_->sws ||
          impl_->swsSrcWidth != inputWidth ||
          impl_->swsSrcHeight != inputHeight ||
          impl_->swsDstWidth != outputWidth ||
          impl_->swsDstHeight != outputHeight ||
          impl_->swsSrcFormat != inputFormat;

        if (mustRecreateSws) {
          if (impl_->sws) {
            sws_freeContext(impl_->sws);
            impl_->sws = nullptr;
          }

          impl_->sws = sws_getContext(
            inputWidth,
            inputHeight,
            inputFormat,
            outputWidth,
            outputHeight,
            AV_PIX_FMT_YUV420P,
            SWS_FAST_BILINEAR,
            nullptr,
            nullptr,
            nullptr
          );

          if (!impl_->sws) {
            error_ = "NativeDecoder could not create conversion context.";
            av_frame_unref(impl_->videoFrame);
            av_frame_unref(impl_->transferredFrame);
            return false;
          }

          impl_->swsSrcWidth = inputWidth;
          impl_->swsSrcHeight = inputHeight;
          impl_->swsDstWidth = outputWidth;
          impl_->swsDstHeight = outputHeight;
          impl_->swsSrcFormat = inputFormat;
        }

        sws_scale(
          impl_->sws,
          frameForProcessing->data,
          frameForProcessing->linesize,
          0,
          inputHeight,
          impl_->convertedYuvFrame->data,
          impl_->convertedYuvFrame->linesize
        );

        frameForCopy = impl_->convertedYuvFrame;

        latestFrameInfo_.outputWidth = outputWidth;
        latestFrameInfo_.outputHeight = outputHeight;
        latestFrameInfo_.pixelFormat = pixelFormatName(static_cast<AVPixelFormat>(frameForCopy->format));
      }

      YuvFrame decodedFrame;
      YuvFrame &targetFrame = outputYuvFrame ? *outputYuvFrame : decodedFrame;
      std::string copyError;

      if (!copyAvFrameToYuvFrame(frameForCopy, targetFrame, copyError)) {
        error_ = copyError;
        av_frame_unref(impl_->videoFrame);
        av_frame_unref(impl_->transferredFrame);
        return false;
      }

      if (!firstYuvFrame_.valid()) {
        firstYuvFrame_ = targetFrame;
      }

      if (!outputYuvFrame) {
        latestYuvFrame_ = std::move(decodedFrame);
      }

      av_frame_unref(impl_->videoFrame);
      av_frame_unref(impl_->transferredFrame);

      rebuildSummary();

      return true;
    }
  }

  error_ = "NativeDecoder reached packet limit before next video frame (maxPackets=" +
    std::to_string(maxPackets) + ").";
  return false;
#endif
}

bool NativeDecoder::decodeFirstVideoFrame(NativeDemuxer &demuxer) {
  if (firstYuvFrame_.valid()) {
    latestYuvFrame_ = firstYuvFrame_;
    latestFrameInfo_ = firstVideoFrame_;
    return true;
  }

  // Streams IPTV/TS muitas vezes iniciam com PAT/PMT, metadata e vários pacotes de
  // áudio antes do primeiro frame de vídeo. No startup precisamos ser mais
  // pacientes; os limites curtos continuam valendo no playback normal.
  return decodeNextVideoFrame(demuxer, true, nullptr, 1200);
}

bool NativeDecoder::dropNextVideoFrame(NativeDemuxer &demuxer) {
  return decodeNextVideoFrame(demuxer, false, nullptr, 80);
}

#ifdef NSTV_USE_FFMPEG
bool NativeDecoder::decodeNextHardwareFrame(NativeDemuxer &demuxer, int maxPackets) {
  if (!impl_) {
    error_ = "NativeDecoder internal state is unavailable.";
    return false;
  }

  releaseLatestHardwareFrame();

  if (!video_.opened || !impl_->videoCodec || !impl_->packet || !impl_->videoFrame) {
    error_ = "NativeDecoder requires an open video decoder before decoding hardware frames.";
    return false;
  }

  AVFormatContext *format = demuxer.formatContext();

  if (!format) {
    error_ = "NativeDecoder could not access AVFormatContext while decoding hardware frame.";
    return false;
  }

  if (maxPackets <= 0) {
    maxPackets = 80;
  }

  for (int i = 0; i < maxPackets; ++i) {
    demuxer.beginIoGuard(900);
    int ret = av_read_frame(format, impl_->packet);
    const bool interrupted = demuxer.wasIoInterrupted();
    demuxer.endIoGuard();

    if (ret < 0 || interrupted) {
      error_ = interrupted
        ? "NativeDecoder hardware read interrupted by IO guard"
        : "NativeDecoder could not read next hardware frame: " + ffmpegError(ret);
      return false;
    }

    if (audio_.opened && impl_->packet->stream_index == audio_.streamIndex) {
      if (!decodeAudioPacketToSdl(impl_->packet)) {
        av_packet_unref(impl_->packet);
        return false;
      }

      av_packet_unref(impl_->packet);
      continue;
    }

    if (impl_->packet->stream_index != video_.streamIndex) {
      av_packet_unref(impl_->packet);
      continue;
    }

    ret = avcodec_send_packet(impl_->videoCodec, impl_->packet);

    av_packet_unref(impl_->packet);

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
      error_ = "NativeDecoder could not send video packet for hardware frame: " + ffmpegError(ret);
      return false;
    }

    while (true) {
      ret = avcodec_receive_frame(impl_->videoCodec, impl_->videoFrame);

      if (ret == AVERROR(EAGAIN)) {
        break;
      }

      if (ret == AVERROR_EOF) {
        error_ = "NativeDecoder reached EOF while decoding hardware frame.";
        return false;
      }

      if (ret < 0) {
        error_ = "NativeDecoder could not receive hardware frame: " + ffmpegError(ret);
        return false;
      }

      AVStream *videoStream = format->streams[video_.streamIndex];
      AVPixelFormat originalFormat = static_cast<AVPixelFormat>(impl_->videoFrame->format);
      const bool hardwareFrame =
        impl_->usingHardware &&
        originalFormat == impl_->selectedHwPixelFormat;
      const bool hasHardwareContext =
        hardwareFrame ||
        impl_->videoFrame->hw_frames_ctx != nullptr;

      NativeFrameInfo currentFrameInfo;
      currentFrameInfo.decoded = true;
      currentFrameInfo.streamIndex = video_.streamIndex;
      currentFrameInfo.width = impl_->videoFrame->width;
      currentFrameInfo.height = impl_->videoFrame->height;
      currentFrameInfo.outputWidth = impl_->videoFrame->width;
      currentFrameInfo.outputHeight = impl_->videoFrame->height;
      currentFrameInfo.pixelFormat = pixelFormatName(originalFormat);
      currentFrameInfo.ptsMs = framePtsMs(impl_->videoFrame, videoStream);
      currentFrameInfo.hardwareFrame = hardwareFrame;
      currentFrameInfo.transferredFromHardware = false;

      if (hasHardwareContext) {
        impl_->latestNativeSurfaceInfo = inspectNativeVideoSurface(impl_->videoFrame);
        currentFrameInfo.nativeSurfaceAvailable = impl_->latestNativeSurfaceInfo.valid;
        currentFrameInfo.nativeSurfaceSummary = impl_->latestNativeSurfaceInfo.summary;
        latestFrameInfo_ = currentFrameInfo;

        if (!firstVideoFrame_.decoded) {
          firstVideoFrame_ = currentFrameInfo;
        }

        impl_->latestHardwareFrameRetained = true;

        ++impl_->retainedHardwareFrameLogCount;
        if (
          impl_->retainedHardwareFrameLogCount <= 3 ||
          impl_->retainedHardwareFrameLogCount % 120 == 0
        ) {
          logLinef(
            "[KBORE][NVTEGRA] retained hardware frame %u without CPU transfer: %s",
            impl_->retainedHardwareFrameLogCount,
            impl_->latestNativeSurfaceInfo.summary.c_str()
          );
        }

        rebuildSummary();
        return true;
      }

      latestFrameInfo_ = currentFrameInfo;
      impl_->latestNativeSurfaceInfo = NativeVideoSurfaceInfo{};
      av_frame_unref(impl_->videoFrame);
      rebuildSummary();

      error_ = "NativeDecoder received a software frame while hardware frame was requested.";
      return false;
    }
  }

  error_ = "NativeDecoder reached packet limit before next hardware frame (maxPackets=" +
    std::to_string(maxPackets) + ").";
  return false;
}

const AVFrame *NativeDecoder::latestHardwareFrame() const {
  if (!impl_ || !impl_->latestHardwareFrameRetained) {
    return nullptr;
  }

  return impl_->videoFrame;
}

void NativeDecoder::releaseLatestHardwareFrame() {
  if (!impl_ || !impl_->latestHardwareFrameRetained) {
    return;
  }

  av_frame_unref(impl_->videoFrame);
  impl_->latestHardwareFrameRetained = false;
}
#endif

void NativeDecoder::close() {
#ifdef NSTV_USE_FFMPEG
  if (impl_) {
    releaseLatestHardwareFrame();

    #ifdef NSTV_USE_SDL
        if (impl_->audioDevice != 0) {
          SDL_PauseAudioDevice(impl_->audioDevice, 1);
          SDL_ClearQueuedAudio(impl_->audioDevice);
          SDL_CloseAudioDevice(impl_->audioDevice);
          impl_->audioDevice = 0;
          impl_->audioStarted = false;
        }
    #endif

        if (impl_->swr) {
          swr_free(&impl_->swr);
          impl_->swr = nullptr;
        }

        if (impl_->audioFrame) {
          av_frame_free(&impl_->audioFrame);
          impl_->audioFrame = nullptr;
        }
    if (impl_->packet) {
      av_packet_free(&impl_->packet);
      impl_->packet = nullptr;
    }

    if (impl_->videoFrame) {
      av_frame_free(&impl_->videoFrame);
      impl_->videoFrame = nullptr;
    }

    if (impl_->transferredFrame) {
      av_frame_free(&impl_->transferredFrame);
      impl_->transferredFrame = nullptr;
    }

    if (impl_->convertedYuvFrame) {
      av_frame_free(&impl_->convertedYuvFrame);
      impl_->convertedYuvFrame = nullptr;
    }

    if (impl_->sws) {
      sws_freeContext(impl_->sws);
      impl_->sws = nullptr;
    }

    if (impl_->alignedTransferBuffer) {
      std::free(impl_->alignedTransferBuffer);
      impl_->alignedTransferBuffer = nullptr;
      impl_->alignedTransferBufferSize = 0;
    }

    if (impl_->videoCodec) {
      avcodec_free_context(&impl_->videoCodec);
      impl_->videoCodec = nullptr;
    }

    if (impl_->audioCodec) {
      avcodec_free_context(&impl_->audioCodec);
      impl_->audioCodec = nullptr;
    }

    impl_->videoDecoder = nullptr;
    impl_->audioDecoder = nullptr;
    impl_->convertedYuvBuffer.clear();
    impl_->audioBuffer.clear();

    impl_->swsSrcWidth = 0;
    impl_->swsSrcHeight = 0;
    impl_->swsDstWidth = 0;
    impl_->swsDstHeight = 0;
    impl_->swsSrcFormat = AV_PIX_FMT_NONE;

    impl_->usingHardware = false;
    impl_->selectedHwPixelFormat = AV_PIX_FMT_NONE;
  }
#endif
}

} // namespace nstv
