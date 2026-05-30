#include "nstv/deko3d_video_renderer.hpp"

#include <cstdio>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/pixfmt.h>
}
#endif

#ifdef __SWITCH__
#include <deko3d.hpp>
#include <switch.h>
#endif

namespace nstv {

#ifdef __SWITCH__
struct Deko3dVideoRenderer::SwitchState {
  dk::Device device;
  dk::Queue queue;
};
#endif

Deko3dVideoRenderer::~Deko3dVideoRenderer() {
  shutdown();
}

bool Deko3dVideoRenderer::initialize() {
  if (initialized_) {
    return true;
  }

  error_.clear();

#ifndef __SWITCH__
  error_ = "Deko3D renderer is only available on Switch.";
  return false;
#else
  switchState_ = new SwitchState();

  switchState_->device = dk::DeviceMaker{}.create();

  if (!switchState_->device) {
    error_ = "dk::DeviceMaker failed.";
    delete switchState_;
    switchState_ = nullptr;
    std::printf("[KBORE][DEKO3D] initialize failed: %s\n", error_.c_str());
    return false;
  }

  switchState_->queue = dk::QueueMaker{switchState_->device}
    .setFlags(DkQueueFlags_Graphics)
    .create();

  if (!switchState_->queue) {
    error_ = "dk::QueueMaker failed.";
    shutdown();
    std::printf("[KBORE][DEKO3D] initialize failed: %s\n", error_.c_str());
    return false;
  }

  initialized_ = true;

  std::printf("[KBORE][DEKO3D] initialized probe renderer\n");

  return true;
#endif
}

void Deko3dVideoRenderer::shutdown() {
#ifdef __SWITCH__
  if (switchState_) {
    if (switchState_->queue) {
      switchState_->queue.waitIdle();
      switchState_->queue.destroy();
    }

    if (switchState_->device) {
      switchState_->device.destroy();
    }

    delete switchState_;
    switchState_ = nullptr;
  }
#endif

  initialized_ = false;
}

#ifdef NSTV_USE_FFMPEG
bool Deko3dVideoRenderer::canRender(const AVFrame *frame) const {
  if (!initialized_ || !frame) {
    return false;
  }

  return frame->format == AV_PIX_FMT_NVTEGRA;
}

bool Deko3dVideoRenderer::renderFrame(const AVFrame *frame) {
  (void)frame;
  error_ = "Deko3D frame rendering is not implemented yet.";
  return false;
}
#endif

#ifdef __SWITCH__
std::unique_ptr<INativeVideoRenderer> createDeko3dVideoRenderer() {
  return std::unique_ptr<INativeVideoRenderer>(new Deko3dVideoRenderer());
}
#endif

} // namespace nstv
