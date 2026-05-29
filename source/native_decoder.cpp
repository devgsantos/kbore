#include "nstv/native_decoder.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libavutil/version.h>
#include <libswscale/swscale.h>
}
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

  SwsContext *sws = nullptr;

  std::vector<uint8_t> convertedYuvBuffer;

  int swsSrcWidth = 0;
  int swsSrcHeight = 0;
  int swsDstWidth = 0;
  int swsDstHeight = 0;
  AVPixelFormat swsSrcFormat = AV_PIX_FMT_NONE;

  bool usingHardware = false;
  AVPixelFormat selectedHwPixelFormat = AV_PIX_FMT_NONE;
};
#endif

namespace {

#ifdef NSTV_USE_FFMPEG

std::string ffmpegError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

int audioChannels(const AVCodecContext *ctx) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
  return ctx->ch_layout.nb_channels;
#else
  return ctx->channels;
#endif
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

  if (pixelFormat != AV_PIX_FMT_YUV420P && pixelFormat != AV_PIX_FMT_YUVJ420P) {
    error =
      "copyAvFrameToYuvFrame expected yuv420p/yuvj420p but received: " +
      pixelFormatName(pixelFormat);

    return false;
  }

  out.width = width;
  out.height = height;

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
  audio_.sampleRate = impl_->audioCodec->sample_rate;
  audio_.channels = audioChannels(impl_->audioCodec);

  rebuildSummary();

  return true;
#endif
}

bool NativeDecoder::decodeNextVideoFrame(NativeDemuxer &demuxer) {
#ifndef NSTV_USE_FFMPEG
  error_ = "NativeDecoder requires NSTV_USE_FFMPEG.";
  return false;
#else
  if (!impl_) {
    error_ = "NativeDecoder internal state is unavailable.";
    return false;
  }

  if (!video_.opened || !impl_->videoCodec || !impl_->packet || !impl_->videoFrame) {
    error_ = "NativeDecoder requires an open video decoder before decoding frames.";
    return false;
  }

  AVFormatContext *format = demuxer.formatContext();

  if (!format) {
    error_ = "NativeDecoder could not access AVFormatContext while decoding.";
    return false;
  }

  const int maxPackets = 80;

  for (int i = 0; i < maxPackets; ++i) {
    int ret = av_read_frame(format, impl_->packet);

    if (ret < 0) {
      error_ = "NativeDecoder could not read next video frame: " + ffmpegError(ret);
      return false;
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

      AVFrame *frameForProcessing = impl_->videoFrame;
      AVPixelFormat originalFormat = static_cast<AVPixelFormat>(impl_->videoFrame->format);

      bool hardwareFrame = impl_->usingHardware && originalFormat == impl_->selectedHwPixelFormat;
      bool transferredFromHardware = false;

      if (hardwareFrame || impl_->videoFrame->hw_frames_ctx) {
        av_frame_unref(impl_->transferredFrame);

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

      NativeFrameInfo currentFrameInfo;

      currentFrameInfo.decoded = true;
      currentFrameInfo.streamIndex = video_.streamIndex;
      currentFrameInfo.width = inputWidth;
      currentFrameInfo.height = inputHeight;
      currentFrameInfo.outputWidth = inputWidth;
      currentFrameInfo.outputHeight = inputHeight;
      currentFrameInfo.pixelFormat = pixelFormatName(inputFormat);
      currentFrameInfo.ptsMs = framePtsMs(impl_->videoFrame, videoStream);
      currentFrameInfo.hardwareFrame = hardwareFrame;
      currentFrameInfo.transferredFromHardware = transferredFromHardware;

      latestFrameInfo_ = currentFrameInfo;

      if (!firstVideoFrame_.decoded) {
        firstVideoFrame_ = currentFrameInfo;
      }

      AVFrame *frameForCopy = frameForProcessing;

      const bool canCopyDirect =
        inputFormat == AV_PIX_FMT_YUV420P ||
        inputFormat == AV_PIX_FMT_YUVJ420P;

      if (!canCopyDirect) {
        int bufferSize = av_image_get_buffer_size(
          AV_PIX_FMT_YUV420P,
          inputWidth,
          inputHeight,
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
          inputWidth,
          inputHeight,
          1
        );

        impl_->convertedYuvFrame->width = inputWidth;
        impl_->convertedYuvFrame->height = inputHeight;
        impl_->convertedYuvFrame->format = AV_PIX_FMT_YUV420P;

        const bool mustRecreateSws =
          !impl_->sws ||
          impl_->swsSrcWidth != inputWidth ||
          impl_->swsSrcHeight != inputHeight ||
          impl_->swsDstWidth != inputWidth ||
          impl_->swsDstHeight != inputHeight ||
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
            inputWidth,
            inputHeight,
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
          impl_->swsDstWidth = inputWidth;
          impl_->swsDstHeight = inputHeight;
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

        latestFrameInfo_.outputWidth = inputWidth;
        latestFrameInfo_.outputHeight = inputHeight;
        latestFrameInfo_.pixelFormat = pixelFormatName(static_cast<AVPixelFormat>(frameForCopy->format));
      }

      YuvFrame decodedFrame;
      std::string copyError;

      if (!copyAvFrameToYuvFrame(frameForCopy, decodedFrame, copyError)) {
        error_ = copyError;
        av_frame_unref(impl_->videoFrame);
        av_frame_unref(impl_->transferredFrame);
        return false;
      }

      latestYuvFrame_ = std::move(decodedFrame);

      if (!firstYuvFrame_.valid()) {
        firstYuvFrame_ = latestYuvFrame_;
      }

      av_frame_unref(impl_->videoFrame);
      av_frame_unref(impl_->transferredFrame);

      rebuildSummary();

      return true;
    }
  }

  error_ = "NativeDecoder reached packet limit before next video frame.";
  return false;
#endif
}

bool NativeDecoder::decodeFirstVideoFrame(NativeDemuxer &demuxer) {
  if (firstYuvFrame_.valid()) {
    latestYuvFrame_ = firstYuvFrame_;
    latestFrameInfo_ = firstVideoFrame_;
    return true;
  }

  return decodeNextVideoFrame(demuxer);
}

void NativeDecoder::close() {
#ifdef NSTV_USE_FFMPEG
  if (impl_) {
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