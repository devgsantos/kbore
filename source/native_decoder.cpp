#include "nstv/native_decoder.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
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
  AVFrame *convertedYuvFrame = nullptr;

  SwsContext *sws = nullptr;

  std::vector<uint8_t> convertedYuvBuffer;
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
      << " pix_fmt="
      << firstVideoFrame_.pixelFormat
      << " stream="
      << firstVideoFrame_.streamIndex
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
      << video_.streamIndex;
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

  close();
  resetState();

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

  impl_->videoCodec->thread_count = 2;
  impl_->videoCodec->thread_type = FF_THREAD_FRAME;
  impl_->videoCodec->flags2 |= AV_CODEC_FLAG2_FAST;

  AVDictionary *options = nullptr;

  av_dict_set(&options, "threads", "2", 0);
  av_dict_set(&options, "flags2", "+fast", 0);

  ret = avcodec_open2(impl_->videoCodec, impl_->videoDecoder, &options);

  av_dict_free(&options);

  if (ret < 0) {
    error_ = "NativeDecoder could not open video decoder: " + ffmpegError(ret);
    close();
    return false;
  }

  impl_->packet = av_packet_alloc();
  impl_->videoFrame = av_frame_alloc();
  impl_->convertedYuvFrame = av_frame_alloc();

  if (!impl_->packet || !impl_->videoFrame || !impl_->convertedYuvFrame) {
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

  rebuildSummary();

  return true;
#endif
}

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

      firstVideoFrame_.decoded = true;
      firstVideoFrame_.streamIndex = video_.streamIndex;
      firstVideoFrame_.width = impl_->videoFrame->width;
      firstVideoFrame_.height = impl_->videoFrame->height;
      firstVideoFrame_.pixelFormat = pixelFormatName(
        static_cast<AVPixelFormat>(impl_->videoFrame->format)
      );

      AVFrame *frameForCopy = impl_->videoFrame;
      const AVPixelFormat frameFormat = static_cast<AVPixelFormat>(impl_->videoFrame->format);

      if (frameFormat != AV_PIX_FMT_YUV420P && frameFormat != AV_PIX_FMT_YUVJ420P) {
        const int width = impl_->videoFrame->width;
        const int height = impl_->videoFrame->height;

        int bufferSize = av_image_get_buffer_size(
          AV_PIX_FMT_YUV420P,
          width,
          height,
          1
        );

        if (bufferSize <= 0) {
          error_ = "NativeDecoder could not calculate YUV conversion buffer size.";
          av_frame_unref(impl_->videoFrame);
          return false;
        }

        impl_->convertedYuvBuffer.resize(static_cast<std::size_t>(bufferSize));

        av_image_fill_arrays(
          impl_->convertedYuvFrame->data,
          impl_->convertedYuvFrame->linesize,
          impl_->convertedYuvBuffer.data(),
          AV_PIX_FMT_YUV420P,
          width,
          height,
          1
        );

        impl_->convertedYuvFrame->width = width;
        impl_->convertedYuvFrame->height = height;
        impl_->convertedYuvFrame->format = AV_PIX_FMT_YUV420P;

        if (impl_->sws) {
          sws_freeContext(impl_->sws);
          impl_->sws = nullptr;
        }

        impl_->sws = sws_getContext(
          width,
          height,
          frameFormat,
          width,
          height,
          AV_PIX_FMT_YUV420P,
          SWS_FAST_BILINEAR,
          nullptr,
          nullptr,
          nullptr
        );

        if (!impl_->sws) {
          error_ = "NativeDecoder could not create conversion context.";
          av_frame_unref(impl_->videoFrame);
          return false;
        }

        sws_scale(
          impl_->sws,
          impl_->videoFrame->data,
          impl_->videoFrame->linesize,
          0,
          height,
          impl_->convertedYuvFrame->data,
          impl_->convertedYuvFrame->linesize
        );

        frameForCopy = impl_->convertedYuvFrame;
      }

      YuvFrame decodedFrame;
      std::string copyError;

      if (!copyAvFrameToYuvFrame(frameForCopy, decodedFrame, copyError)) {
        error_ = copyError;
        av_frame_unref(impl_->videoFrame);
        return false;
      }

      latestYuvFrame_ = std::move(decodedFrame);

      if (!firstYuvFrame_.valid()) {
        firstYuvFrame_ = latestYuvFrame_;
      }

      av_frame_unref(impl_->videoFrame);

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
  }
#endif
}

} // namespace nstv