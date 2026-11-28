#pragma once

#include "nstv/native_video_surface.hpp"

#include <cstdint>
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

  virtual void setOverlayProgress(int64_t positionMs, int64_t durationMs, bool visible) {
    (void)positionMs;
    (void)durationMs;
    (void)visible;
  }

#ifdef NSTV_USE_FFMPEG
  // Waits for the GPU to finish with every decoder frame it imported and drops those imports.
  // Must be called before the decoder flushes or frees its frame pool (e.g. on seek): imported
  // memory blocks map the decoder's buffers directly, and a mapping left pointing at freed
  // decoder memory makes nvservices abort, taking the whole system down.
  virtual void releaseImportedFrames() {}

  virtual bool canRender(const AVFrame *frame) const = 0;
  virtual bool renderFrame(const AVFrame *frame) = 0;
#endif

  virtual const std::string &error() const = 0;
  virtual const char *name() const = 0;
};

std::unique_ptr<INativeVideoRenderer> createDeko3dVideoRenderer();

} // namespace nstv
