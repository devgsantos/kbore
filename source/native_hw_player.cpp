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
  error_.clear();
  yuvFrame_ = YuvFrame{};

#ifndef NSTV_ENABLE_NATIVE_HW_PLAYER
  error_ =
    "Native hardware player is not enabled. "
    "Build with NSTV_ENABLE_NATIVE_HW_PLAYER to run the native video loop.";

  open_ = false;
  return false;
#else
  if (!demuxer_.open(url)) {
    error_ = demuxer_.error();
    open_ = false;
    return false;
  }

  if (!decoder_.openVideo(demuxer_)) {
    error_ = decoder_.error();
    open_ = false;
    return false;
  }

  if (!decoder_.openAudio(demuxer_)) {
    error_ = decoder_.error();
    open_ = false;
    return false;
  }

  if (!decoder_.decodeFirstVideoFrame(demuxer_)) {
    error_ = decoder_.error();
    open_ = false;
    return false;
  }

  yuvFrame_ = decoder_.latestYuvFrame();

  if (!yuvFrame_.valid()) {
    error_ = "Native first frame was decoded, but YuvFrame is invalid: " + decoder_.summary();
    open_ = false;
    return false;
  }

  error_.clear();
  open_ = true;

  return true;
#endif
}

void NativeHwPlayerBackend::close() {
#ifdef NSTV_ENABLE_NATIVE_HW_PLAYER
  decoder_.close();
  demuxer_.close();
#endif

  open_ = false;
  paused_ = false;
  url_.clear();
  error_.clear();
  yuvFrame_ = YuvFrame{};
}

bool NativeHwPlayerBackend::update() {
#ifndef NSTV_ENABLE_NATIVE_HW_PLAYER
  return false;
#else
  if (!open_ || paused_) {
    return hasFrame();
  }

  if (decoder_.decodeNextVideoFrame(demuxer_)) {
    yuvFrame_ = decoder_.latestYuvFrame();
    error_.clear();
    return true;
  }

  /*
    Não derruba o player imediatamente se falhar um frame.
    IPTV pode ter pequenos buracos de leitura.
    Mantemos o último frame na tela.
  */

  error_ = decoder_.error();

  return hasFrame();
#endif
}

void NativeHwPlayerBackend::togglePause() {
  if (!open_) {
    return;
  }

  paused_ = !paused_;
}

} // namespace nstv