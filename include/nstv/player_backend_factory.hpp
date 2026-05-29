#pragma once

#include "player_backend.hpp"
#include <memory>

namespace nstv {

std::unique_ptr<IPlayerBackend> createPlayerBackend();

} // namespace nstv