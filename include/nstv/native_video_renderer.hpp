#pragma once

#include "nstv/native_video_surface.hpp"

#include <memory>
#include <string>

namespace nstv {

class INativeVideoRenderer {
public:
  virtual ~INativeVideoRenderer() = default;

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;
  virtual void setOverlayVisible(bool visible) { (void)visible; }
  virtual void setOverlayInfo(
    const std::string &title,
    const std::string &subtitle,
    const std::string &status,
    const std::string &controls
  ) {
    (void)title;
    (void)subtitle;
    (void)status;
    (void)controls;
  }

#ifdef NSTV_USE_FFMPEG
  virtual bool canRender(const AVFrame *frame) const = 0;
  virtual bool renderFrame(const AVFrame *frame) = 0;
#endif

  virtual const std::string &error() const = 0;
  virtual const char *name() const = 0;
};

std::unique_ptr<INativeVideoRenderer> createDeko3dVideoRenderer();

} // namespace nstv
