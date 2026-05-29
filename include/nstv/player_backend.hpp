#pragma once

#include "graphics.hpp"
#include <string>

namespace nstv {

class IPlayerBackend {
public:
  virtual ~IPlayerBackend() = default;

  virtual bool open(const std::string &url) = 0;
  virtual void close() = 0;
  virtual bool update() = 0;
  virtual void togglePause() = 0;

  virtual bool isOpen() const = 0;
  virtual bool isPaused() const = 0;
  virtual bool hasFrame() const = 0;

  virtual const Bitmap &frame() const = 0;
  virtual const YuvFrame &yuvFrame() const = 0;

  virtual const std::string &error() const = 0;
  virtual const std::string &url() const = 0;

  virtual const char *name() const = 0;
};

} // namespace nstv