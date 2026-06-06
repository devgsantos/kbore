#pragma once

#include "graphics.hpp"
#include <cstdint>
#include <string>

namespace nstv {

class IPlayerBackend {
public:
  virtual ~IPlayerBackend() = default;

  virtual bool open(const std::string &url) = 0;
  virtual void close() = 0;
  virtual bool update() = 0;
  virtual void togglePause() = 0;

  virtual bool canSeek() const { return false; }
  virtual int64_t durationMs() const { return 0; }
  virtual int64_t positionMs() const { return 0; }
  virtual bool seekToMs(int64_t positionMs) { (void)positionMs; return false; }
  virtual bool seekByMs(int64_t deltaMs) {
    if (!canSeek()) {
      return false;
    }

    int64_t target = positionMs() + deltaMs;
    const int64_t duration = durationMs();

    if (target < 0) {
      target = 0;
    }

    if (duration > 0 && target > duration) {
      target = duration;
    }

    return seekToMs(target);
  }

  virtual bool isOpen() const = 0;
  virtual bool isPaused() const = 0;
  virtual bool hasFrame() const = 0;

  virtual const Bitmap &frame() const = 0;
  virtual const YuvFrame &yuvFrame() const = 0;
  virtual bool nativeVideoActive() const { return false; }
  virtual bool isAudioOnly() const { return false; }
  virtual void setNativeVideoAllowed(bool allowed) { (void)allowed; }
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

  virtual const std::string &error() const = 0;
  virtual const std::string &url() const = 0;

  virtual const char *name() const = 0;
};

} // namespace nstv
