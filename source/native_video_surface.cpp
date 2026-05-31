#include "nstv/native_video_surface.hpp"

#include <sstream>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/pixdesc.h>
}
#endif

namespace nstv {

#ifdef NSTV_USE_FFMPEG
NativeVideoSurfaceInfo inspectNativeVideoSurface(const AVFrame *frame) {
  NativeVideoSurfaceInfo info;

  if (!frame) {
    info.summary = "NVTEGRA frame: invalid null frame";
    return info;
  }

  const char *formatName = av_get_pix_fmt_name(
    static_cast<AVPixelFormat>(frame->format)
  );

  info.valid = frame->width > 0 && frame->height > 0;
  info.width = frame->width;
  info.height = frame->height;
  info.format = frame->format;
  info.formatName = (formatName && formatName[0] != '\0') ? formatName : "unknown";
  info.hasHwFramesCtx = frame->hw_frames_ctx != nullptr;
  info.hasBuf0 = frame->buf[0] != nullptr;
  info.hasBuf1 = frame->buf[1] != nullptr;
  info.hasBuf2 = frame->buf[2] != nullptr;
  info.data0 = reinterpret_cast<uintptr_t>(frame->data[0]);
  info.data1 = reinterpret_cast<uintptr_t>(frame->data[1]);
  info.data2 = reinterpret_cast<uintptr_t>(frame->data[2]);
  info.linesize0 = frame->linesize[0];
  info.linesize1 = frame->linesize[1];
  info.linesize2 = frame->linesize[2];

  std::ostringstream out;
  out
    << "NVTEGRA frame: format="
    << info.formatName
    << " width="
    << info.width
    << " height="
    << info.height
    << " hw_frames_ctx="
    << (info.hasHwFramesCtx ? "yes" : "no")
    << " buf0="
    << (info.hasBuf0 ? "yes" : "no")
    << " buf1="
    << (info.hasBuf1 ? "yes" : "no")
    << " buf2="
    << (info.hasBuf2 ? "yes" : "no")
    << " data0=0x"
    << std::hex
    << info.data0
    << " data1=0x"
    << info.data1
    << " data2=0x"
    << info.data2
    << std::dec
    << " linesize0="
    << info.linesize0
    << " linesize1="
    << info.linesize1
    << " linesize2="
    << info.linesize2;

  info.summary = out.str();

  return info;
}
#endif

} // namespace nstv
