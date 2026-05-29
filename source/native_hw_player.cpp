#include "nstv/native_hw_player.hpp"

namespace nstv {

NativeHwPlayerBackend::NativeHwPlayerBackend() = default;

NativeHwPlayerBackend::~NativeHwPlayerBackend() {
  close();
}

bool NativeHwPlayerBackend::open(const std::string &url) {
  close();

  url_ = url;
  paused_ = false;

#ifndef NSTV_ENABLE_NATIVE_HW_PLAYER
  error_ =
    "Native hardware player is not enabled. "
    "Build with NSTV_ENABLE_NATIVE_HW_PLAYER after the native backend is implemented.";

  open_ = false;
  return false;
#else
  /*
    Próxima etapa real:

    Aqui entra o pipeline nativo:

    1. Abrir URL
    2. Inicializar FFmpeg custom / demuxer
    3. Inicializar decoder hardware
    4. Inicializar deko3d renderer
    5. Inicializar audio sink nativo ou SDL fallback
    6. Iniciar playback

    Por enquanto este backend existe apenas como ponto seguro
    de integração, sem quebrar o player atual.
  */

  error_ =
    "Native hardware player backend is enabled, but the implementation is not connected yet.";

  open_ = false;
  return false;
#endif
}

void NativeHwPlayerBackend::close() {
#ifdef NSTV_ENABLE_NATIVE_HW_PLAYER
  /*
    Futuro cleanup:

    - parar thread de demux/decode
    - liberar decoder hardware
    - liberar texturas/surfaces deko3d
    - parar audio sink
    - fechar input
  */
#endif

  open_ = false;
  paused_ = false;
  url_.clear();
}

bool NativeHwPlayerBackend::update() {
#ifdef NSTV_ENABLE_NATIVE_HW_PLAYER
  /*
    Futuro update:

    - bombear eventos do player
    - atualizar estado
    - processar fim de stream
    - processar erros assíncronos
  */
#endif

  return false;
}

void NativeHwPlayerBackend::togglePause() {
  if (!open_) {
    return;
  }

  paused_ = !paused_;

#ifdef NSTV_ENABLE_NATIVE_HW_PLAYER
  /*
    Futuro pause/resume:

    - pausar decoder/demux
    - pausar audio sink
    - manter último frame renderizado
  */
#endif
}

} // namespace nstv