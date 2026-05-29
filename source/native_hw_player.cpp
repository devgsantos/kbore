#include "nstv/native_hw_player.hpp"

#include <algorithm>
#include <chrono>

namespace nstv {

namespace {

long long nowMs() {
  using clock = std::chrono::steady_clock;

  return std::chrono::duration_cast<std::chrono::milliseconds>(
    clock::now().time_since_epoch()
  ).count();
}

int clampFrameInterval(int value) {
  /*
    Limites seguros:
    16ms = ~60 FPS
    80ms = ~12.5 FPS

    IPTV normalmente fica entre 25, 29.97, 30, 50 ou 60 FPS.
    Se o PTS vier quebrado ou com saltos estranhos, não deixamos
    o player acelerar demais nem ficar parado demais.
  */
  return std::max(16, std::min(80, value));
}

} // namespace

NativeHwPlayerBackend::NativeHwPlayerBackend() = default;

NativeHwPlayerBackend::~NativeHwPlayerBackend() {
  close();
}

void NativeHwPlayerBackend::resetClock() {
  clockStarted_ = false;

  firstPtsMs_ = -1;
  lastPtsMs_ = -1;

  playbackStartMs_ = 0;
  lastFrameWallMs_ = 0;
  nextFrameDueMs_ = 0;

  fallbackFrameIntervalMs_ = 40;
  currentFrameIntervalMs_ = 40;

  decodedFrames_ = 0;
}

void NativeHwPlayerBackend::resetClockToCurrentFrame(long long now) {
  const NativeFrameInfo &info = decoder_.latestFrameInfo();

  playbackStartMs_ = now;
  lastFrameWallMs_ = now;
  nextFrameDueMs_ = now + currentFrameIntervalMs_;

  if (info.ptsMs >= 0) {
    firstPtsMs_ = info.ptsMs;
    lastPtsMs_ = info.ptsMs;
  } else {
    firstPtsMs_ = 0;
    lastPtsMs_ = 0;
  }
}

void NativeHwPlayerBackend::syncClockFromLatestFrame() {
  const NativeFrameInfo &info = decoder_.latestFrameInfo();
  const long long now = nowMs();

  if (!clockStarted_) {
    clockStarted_ = true;

    playbackStartMs_ = now;
    lastFrameWallMs_ = now;
    nextFrameDueMs_ = now + currentFrameIntervalMs_;

    if (info.ptsMs >= 0) {
      firstPtsMs_ = info.ptsMs;
      lastPtsMs_ = info.ptsMs;
    } else {
      firstPtsMs_ = 0;
      lastPtsMs_ = 0;
    }

    ++decodedFrames_;
    return;
  }

  int interval = fallbackFrameIntervalMs_;

  if (info.ptsMs >= 0 && lastPtsMs_ >= 0) {
    const int64_t ptsDelta = info.ptsMs - lastPtsMs_;

    /*
      PTS normal:
        25 FPS  -> 40ms
        30 FPS  -> 33ms
        50 FPS  -> 20ms
        60 FPS  -> 16/17ms

      Se vier delta negativo, zero ou salto enorme, consideramos
      descontinuidade/buffer e resetamos a base do relógio para evitar 2x.
    */
    if (ptsDelta >= 15 && ptsDelta <= 120) {
      interval = clampFrameInterval(static_cast<int>(ptsDelta));
      lastPtsMs_ = info.ptsMs;
    } else if (ptsDelta > 120 && ptsDelta <= 1000) {
      /*
        Pequeno buraco de transmissão. Não tentamos reproduzir tudo rápido.
        Mantemos ritmo estável.
      */
      interval = fallbackFrameIntervalMs_;
      lastPtsMs_ = info.ptsMs;
    } else {
      /*
        Descontinuidade real: PTS voltou, pulou demais ou está inválido.
        Resetar evita que o player tente compensar decodificando rápido.
      */
      resetClockToCurrentFrame(now);
      ++decodedFrames_;
      return;
    }
  } else {
    lastPtsMs_ += fallbackFrameIntervalMs_;
  }

  currentFrameIntervalMs_ = interval;
  lastFrameWallMs_ = now;
  nextFrameDueMs_ = now + currentFrameIntervalMs_;

  ++decodedFrames_;
}

bool NativeHwPlayerBackend::shouldDecodeNow(long long now) const {
  if (!clockStarted_) {
    return true;
  }

  /*
    Essa é a correção principal do SD em 2x.

    Antes o update podia consumir frames do buffer sempre que a UI rodava.
    Agora só decodificamos o próximo frame quando o relógio permitir.
  */
  return now >= nextFrameDueMs_;
}

long long NativeHwPlayerBackend::playbackDelayMs(long long now) const {
  if (!clockStarted_) {
    return 0;
  }

  if (firstPtsMs_ < 0 || lastPtsMs_ < 0) {
    return 0;
  }

  const long long playbackElapsed = now - playbackStartMs_;
  const long long videoElapsed = lastPtsMs_ - firstPtsMs_;

  return playbackElapsed - videoElapsed;
}

bool NativeHwPlayerBackend::shouldDropFrames(long long now) const {
  /*
    Só fazemos frame dropping quando o vídeo está claramente atrasado.

    Isso evita o problema anterior do SD acelerando para 2x.
    Em SD estável, o delay fica baixo e nenhum frame é pulado.
  */
  return playbackDelayMs(now) > 120;
}

bool NativeHwPlayerBackend::open(const std::string &url) {
  close();

  url_ = url;
  paused_ = false;
  error_.clear();
  yuvFrame_ = YuvFrame{};
  resetClock();

#ifndef NSTV_ENABLE_NATIVE_HW_PLAYER
  error_ =
    "Native hardware player is not enabled. "
    "Build with NSTV_ENABLE_NATIVE_HW_PLAYER to run the native hardware player.";

  open_ = false;
  return false;
#else
  if (!demuxer_.open(url)) {
    error_ = demuxer_.error();
    open_ = false;
    return false;
  }

  if (!hwProbe_.probeVideo(demuxer_)) {
    error_ = hwProbe_.error();
    open_ = false;
    return false;
  }

  if (!hwProbe_.hasUsableDeviceConfig()) {
    error_ =
      hwProbe_.summary() +
      " | result: current FFmpeg exposes no usable hardware device config.";

    open_ = false;
    return false;
  }

  if (!hwProbe_.createBestDevice()) {
    error_ =
      hwProbe_.summary() +
      " | result: FFmpeg exposes a hardware config, but AVHWDeviceContext creation failed.";

    open_ = false;
    return false;
  }

  if (!decoder_.openVideoHardware(
        demuxer_,
        hwProbe_.deviceContext(),
        hwProbe_.selectedHwPixelFormat()
      )) {
    error_ =
      "Native HW device created, but decoder hardware open failed: " +
      decoder_.error() +
      " | hwProbe=" +
      hwProbe_.summary();

    open_ = false;
    return false;
  }

  if (!decoder_.openAudio(demuxer_)) {
    error_ = decoder_.error();
    open_ = false;
    return false;
  }

  if (!decoder_.decodeFirstVideoFrame(demuxer_)) {
    error_ =
      "Native HW decoder opened, but first frame decode failed: " +
      decoder_.error() +
      " | decoder=" +
      decoder_.summary() +
      " | hwProbe=" +
      hwProbe_.summary();

    open_ = false;
    return false;
  }

  yuvFrame_ = decoder_.latestYuvFrame();

  if (!yuvFrame_.valid()) {
    error_ =
      "Native HW first frame decoded, but YuvFrame is invalid: " +
      decoder_.summary();

    open_ = false;
    return false;
  }

  syncClockFromLatestFrame();

  error_.clear();
  open_ = true;

  return true;
#endif
}

void NativeHwPlayerBackend::close() {
#ifdef NSTV_ENABLE_NATIVE_HW_PLAYER
  decoder_.close();
  demuxer_.close();
  hwProbe_.closeDevice();
#endif

  open_ = false;
  paused_ = false;
  url_.clear();
  error_.clear();
  yuvFrame_ = YuvFrame{};
  resetClock();
}

bool NativeHwPlayerBackend::update() {
#ifndef NSTV_ENABLE_NATIVE_HW_PLAYER
  return false;
#else
  if (!open_ || paused_) {
    return hasFrame();
  }

  const long long now = nowMs();

  if (!shouldDecodeNow(now)) {
    return hasFrame();
  }

  /*
    Se estamos atrasados, pulamos até 2 frames SEM transferir para CPU.

    Isso reduz bastante o custo em HD/FHD, porque evita:
      - av_hwframe_transfer_data()
      - sws_scale()
      - cópia para YuvFrame

    Depois dos drops, decodificamos 1 frame real para exibição.
  */
  int dropped = 0;

  /*
    Agora o drop é barato, porque dropNextVideoFrame()
    não transfere frame para CPU.

    Podemos permitir mais drops por update para HD/FHD.
  */
  const int maxDropsPerUpdate = 6;

  while (dropped < maxDropsPerUpdate && shouldDropFrames(nowMs())) {
    if (!decoder_.dropNextVideoFrame(demuxer_)) {
      error_ = decoder_.error();
      break;
    }

    syncClockFromLatestFrame();
    ++dropped;
  }

  if (!decoder_.decodeNextVideoFrame(demuxer_, true)) {
    error_ = decoder_.error();
    return hasFrame();
  }

  yuvFrame_ = decoder_.latestYuvFrame();

  if (!yuvFrame_.valid()) {
    error_ = "Native decoded frame is invalid: " + decoder_.summary();
    return hasFrame();
  }

  syncClockFromLatestFrame();

  error_.clear();

  return true;
#endif
}

void NativeHwPlayerBackend::togglePause() {
  if (!open_) {
    return;
  }

  paused_ = !paused_;

  /*
    Ao retomar, reinicia a base de tempo no frame atual.
    Isso evita que o player tente compensar o tempo parado.
  */
  if (!paused_) {
    resetClockToCurrentFrame(nowMs());
  }
}

} // namespace nstv