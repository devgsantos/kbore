#include "nstv/native_video_renderer.hpp"

namespace nstv {

#ifndef __SWITCH__
std::unique_ptr<INativeVideoRenderer> createDeko3dVideoRenderer() {
  return nullptr;
}
#endif

} // namespace nstv
