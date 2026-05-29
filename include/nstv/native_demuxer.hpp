#pragma once

#include <string>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

namespace nstv {

struct NativeStreamInfo {
  bool exists = false;
  int index = -1;

  std::string type;
  std::string codecName;

  int width = 0;
  int height = 0;

  int sampleRate = 0;
  int channels = 0;
};

class NativeDemuxer {
public:
  NativeDemuxer();
  ~NativeDemuxer();

  bool open(const std::string &url);
  void close();

  bool isOpen() const { return open_; }

  const std::string &url() const { return url_; }
  const std::string &error() const { return error_; }
  const std::string &summary() const { return summary_; }

  const NativeStreamInfo &video() const { return video_; }
  const NativeStreamInfo &audio() const { return audio_; }

#ifdef NSTV_USE_FFMPEG
  AVFormatContext *formatContext() const;
#endif

private:
  struct Impl;
  Impl *impl_ = nullptr;

  bool open_ = false;

  std::string url_;
  std::string error_;
  std::string summary_;

  NativeStreamInfo video_;
  NativeStreamInfo audio_;

  void resetState();
};

} // namespace nstv