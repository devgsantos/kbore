#pragma once

#include "nstv/native_video_renderer.hpp"

namespace nstv {

class Deko3dVideoRenderer : public INativeVideoRenderer {
public:
  ~Deko3dVideoRenderer() override;

  bool initialize() override;
  void shutdown() override;
  void setOverlayVisible(bool visible) override { overlayVisible_ = visible; }
  void setOverlayInfo(
    const std::string &title,
    const std::string &subtitle,
    const std::string &status,
    const std::string &controls
  ) override;
  void setOverlayProgress(int64_t positionMs, int64_t durationMs, bool visible) override;

#ifdef NSTV_USE_FFMPEG
  bool canRender(const AVFrame *frame) const override;
  bool renderFrame(const AVFrame *frame) override;
#endif

  const std::string &error() const override { return error_; }
  const char *name() const override { return "deko3d"; }

private:
  bool initialized_ = false;
  bool overlayVisible_ = false;
  bool overlayDirty_ = true;
  bool loggedFrameImport_ = false;
  bool loggedFirstPresent_ = false;
  unsigned int presentedFrames_ = 0;
  std::string overlayTitle_;
  std::string overlaySubtitle_;
  std::string overlayStatus_;
  std::string overlayControls_;
  int64_t overlayProgressPositionMs_ = 0;
  int64_t overlayProgressDurationMs_ = 0;
  bool overlayProgressVisible_ = false;
  std::string error_;

#ifdef __SWITCH__
  struct SwitchState;
  SwitchState *switchState_ = nullptr;
#endif
};

} // namespace nstv
