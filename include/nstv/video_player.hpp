#pragma once

#include "nstv/graphics.hpp"
#include "nstv/player_backend.hpp"
#include <string>

namespace nstv {

class VideoPlayer : public IPlayerBackend {
public:
  VideoPlayer();
  ~VideoPlayer() override;

  bool open(const std::string &url) override;
  void close() override;

  bool update() override;

  void togglePause() override;

  bool isPaused() const override { return paused_; }
  bool isOpen() const override { return open_; }

  bool hasFrame() const override {
    return yuvFrame_.valid() || frame_.valid();
  }

  const Bitmap &frame() const override { return frame_; }
  const YuvFrame &yuvFrame() const override { return yuvFrame_; }

  const std::string &error() const override { return error_; }
  const std::string &url() const override { return url_; }

  const char *name() const override { return "Software YUV Player"; }

private:
  bool open_ = false;
  bool paused_ = false;
  std::string url_;
  std::string error_;
  Bitmap frame_;
  YuvFrame yuvFrame_;

#ifdef NSTV_USE_FFMPEG
  struct Impl;
  Impl *impl_ = nullptr;
#endif
};

} // namespace nstv