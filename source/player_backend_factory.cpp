#include "nstv/player_backend_factory.hpp"

#include "nstv/native_hw_player.hpp"
#include "nstv/video_player.hpp"

namespace nstv {

std::unique_ptr<IPlayerBackend> createPlayerBackend() {
#if defined(__SWITCH__) && defined(NSTV_ENABLE_NATIVE_HW_PLAYER)
  return std::make_unique<NativeHwPlayerBackend>();
#else
  return std::make_unique<VideoPlayer>();
#endif
}

} // namespace nstv