#pragma once

#include "nstv/player_backend.hpp"
#include "nstv/native_demuxer.hpp"
#include "nstv/native_decoder.hpp"
#include "nstv/native_hw_device.hpp"

#include <cstdint>
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
  bool hasFrame() const override { return yuvFrame_.valid(); }

  const Bitmap &frame() const override { return emptyFrame_; }
  const YuvFrame &yuvFrame() const override { return yuvFrame_; }

  const std::string &error() const override { return error_; }
  const std::string &url() const override { return url_; }

  const char *name() const override { return "Native NVTEGRA HW Player"; }

private:
  bool open_ = false;
  bool paused_ = false;

  std::string url_;
  std::string error_;

  Bitmap emptyFrame_;
  YuvFrame yuvFrame_;

  NativeDemuxer demuxer_;
  NativeDecoder decoder_;
  NativeHwDeviceProbe hwProbe_;

  bool clockStarted_ = false;

  int64_t firstPtsMs_ = -1;
  int64_t lastPtsMs_ = -1;

  long long playbackStartMs_ = 0;
  long long lastFrameWallMs_ = 0;

  int fallbackFrameIntervalMs_ = 33;

  void resetClock();
  void syncClockFromLatestFrame();
  bool shouldDecodeNow(long long now) const;
  bool isVideoLate(long long now) const;
};

} // namespace nstv