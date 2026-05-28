#pragma once

#include "nstv/graphics.hpp"
#include <string>

namespace nstv {

class VideoPlayer {
public:
  VideoPlayer();
  ~VideoPlayer();

  bool open(const std::string &url);
  void close();

  bool update();

  void togglePause();
  bool isPaused() const { return paused_; }
  bool isOpen() const { return open_; }

  const Bitmap &frame() const { return frame_; }
  const std::string &error() const { return error_; }
  const std::string &url() const { return url_; }

private:
  bool open_ = false;
  bool paused_ = false;
  std::string url_;
  std::string error_;
  Bitmap frame_;

#ifdef NSTV_USE_FFMPEG
  struct Impl;
  Impl *impl_ = nullptr;
#endif
};

} // namespace nstv
