#pragma once

#include "nstv/player_backend.hpp"
#include <string>

namespace nstv {

class NativeHwPlayerBackend : public IPlayerBackend {
public:
  NativeHwPlayerBackend();
  ~NativeHwPlayerBackend() override;

  bool open(const std::string &url) override;
  void close() override;
  bool update() override;
  void togglePause() override;

  bool isOpen() const override { return open_; }
  bool isPaused() const override { return paused_; }
  bool hasFrame() const override { return false; }

  const Bitmap &frame() const override { return emptyFrame_; }
  const YuvFrame &yuvFrame() const override { return emptyYuvFrame_; }

  const std::string &error() const override { return error_; }
  const std::string &url() const override { return url_; }

  const char *name() const override { return "Native HW Player"; }

private:
  bool open_ = false;
  bool paused_ = false;

  std::string url_;
  std::string error_;

  Bitmap emptyFrame_;
  YuvFrame emptyYuvFrame_;
};

} // namespace nstv