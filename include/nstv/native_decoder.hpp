#pragma once

#include "nstv/graphics.hpp"
#include "nstv/native_demuxer.hpp"

#include <string>

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
};

struct NativeFrameInfo {
  bool decoded = false;

  int streamIndex = -1;

  int width = 0;
  int height = 0;

  std::string pixelFormat;
};

class NativeDecoder {
public:
  NativeDecoder();
  ~NativeDecoder();

  bool openVideo(const NativeDemuxer &demuxer);
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

  YuvFrame firstYuvFrame_;
  YuvFrame latestYuvFrame_;

  std::string error_;
  std::string summary_;

  void resetState();
  void rebuildSummary();
};

} // namespace nstv