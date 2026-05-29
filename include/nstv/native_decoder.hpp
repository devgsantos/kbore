#pragma once

#include "nstv/graphics.hpp"
#include "nstv/native_demuxer.hpp"

#include <cstdint>
#include <string>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace nstv {

struct NativeDecoderInfo {
  bool opened = false;

  std::string codecName;
  std::string decoderName;

  int streamIndex = -1;

  int width = 0;
  int height = 0;

  int sampleRate = 0;
  int channels = 0;

  bool usingHardware = false;
  std::string hwPixelFormat;
};

struct NativeFrameInfo {
  bool decoded = false;

  int streamIndex = -1;

  int width = 0;
  int height = 0;

  int outputWidth = 0;
  int outputHeight = 0;

  std::string pixelFormat;

  bool hardwareFrame = false;
  bool transferredFromHardware = false;

  int64_t ptsMs = -1;
};

class NativeDecoder {
public:
  NativeDecoder();
  ~NativeDecoder();

  bool openVideo(const NativeDemuxer &demuxer);

#ifdef NSTV_USE_FFMPEG
  bool openVideoHardware(
    const NativeDemuxer &demuxer,
    AVBufferRef *deviceContext,
    AVPixelFormat hwPixelFormat
  );
#endif

  bool openAudio(const NativeDemuxer &demuxer);

  bool decodeFirstVideoFrame(NativeDemuxer &demuxer);
  bool decodeNextVideoFrame(NativeDemuxer &demuxer);

  void close();

  bool hasVideoDecoder() const { return video_.opened; }
  bool hasAudioDecoder() const { return audio_.opened; }
  bool hasFirstVideoFrame() const { return firstVideoFrame_.decoded; }
  bool hasLatestYuvFrame() const { return latestYuvFrame_.valid(); }

  const NativeDecoderInfo &video() const { return video_; }
  const NativeDecoderInfo &audio() const { return audio_; }

  const NativeFrameInfo &firstVideoFrame() const { return firstVideoFrame_; }
  const NativeFrameInfo &latestFrameInfo() const { return latestFrameInfo_; }

  const YuvFrame &firstYuvFrame() const { return firstYuvFrame_; }
  const YuvFrame &latestYuvFrame() const { return latestYuvFrame_; }

  const std::string &error() const { return error_; }
  const std::string &summary() const { return summary_; }

private:
  struct Impl;
  Impl *impl_ = nullptr;

  NativeDecoderInfo video_;
  NativeDecoderInfo audio_;

  NativeFrameInfo firstVideoFrame_;
  NativeFrameInfo latestFrameInfo_;

  YuvFrame firstYuvFrame_;
  YuvFrame latestYuvFrame_;

  std::string error_;
  std::string summary_;

  void resetState();
  void rebuildSummary();

#ifdef NSTV_USE_FFMPEG
  bool openVideoInternal(
    const NativeDemuxer &demuxer,
    AVBufferRef *deviceContext,
    AVPixelFormat hwPixelFormat,
    bool useHardware
  );
#endif
};

} // namespace nstv