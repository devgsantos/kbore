#pragma once

#include <cstdint>
#include <string>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace nstv {

struct NativeVideoSurfaceInfo {
  bool valid = false;

  int width = 0;
  int height = 0;

  int format = -1;
  std::string formatName;

  bool hasHwFramesCtx = false;
  bool hasBuf0 = false;
  bool hasBuf1 = false;
  bool hasBuf2 = false;

  uintptr_t data0 = 0;
  uintptr_t data1 = 0;
  uintptr_t data2 = 0;

  int linesize0 = 0;
  int linesize1 = 0;
  int linesize2 = 0;

  std::string summary;
};

#ifdef NSTV_USE_FFMPEG
NativeVideoSurfaceInfo inspectNativeVideoSurface(const AVFrame *frame);
#endif

} // namespace nstv
