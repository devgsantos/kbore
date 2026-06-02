#include "nstv/native_hw_player.hpp"
#include "nstv/log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

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
  lastPresentedVideoWallMs_ = 0;
  nextFrameDueMs_ = 0;

  fallbackFrameIntervalMs_ = 40;
  currentFrameIntervalMs_ = 40;
  cpuFrameCostAvgMs_ = 0;

  decodedFrames_ = 0;
  droppedFrames_ = 0;
  lastDropLogWallMs_ = 0;
}

void NativeHwPlayerBackend::resetClockToCurrentFrame(long long now) {
  const NativeFrameInfo &info = decoder_.latestFrameInfo();

  playbackStartMs_ = now;
  lastFrameWallMs_ = now;
  lastPresentedVideoWallMs_ = now;
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
  return playbackDelayMs(now) > dropDelayThresholdMs();
}

int NativeHwPlayerBackend::cpuPresentationIntervalMs() const {
  int width = decoder_.video().width;
  int height = decoder_.video().height;

  if (yuvFrame_.valid()) {
    width = std::max(width, yuvFrame_.width);
    height = std::max(height, yuvFrame_.height);
  }

  const int pixels = width * height;

  if (width >= 1700 || height >= 950 || pixels >= 1800 * 900) {
    if (cpuFrameCostAvgMs_ <= 0) {
      return 50;
    }

    if (cpuFrameCostAvgMs_ <= 28) {
      return 33;
    }

    if (cpuFrameCostAvgMs_ <= 42) {
      return 40;
    }

    if (cpuFrameCostAvgMs_ <= 54) {
      return 50;
    }

    return 67;
  }

  if (width >= 1100 || height >= 650 || pixels >= 1000 * 600) {
    if (cpuFrameCostAvgMs_ <= 0) {
      return 33;
    }

    if (cpuFrameCostAvgMs_ <= 22) {
      return 25;
    }

    return 33;
  }

  return 0;
}

int NativeHwPlayerBackend::maxDropsPerUpdate() const {
  if (nativeRendererReady_ && nativeRenderer_) {
    return 1;
  }

  const int interval = cpuPresentationIntervalMs();

  if (interval >= 100) {
    return 18;
  }

  if (interval >= 67) {
    return 14;
  }

  if (interval >= 50) {
    return 10;
  }

  return 6;
}

int NativeHwPlayerBackend::dropDelayThresholdMs() const {
  if (nativeRendererReady_ && nativeRenderer_) {
    return 500;
  }

  const int interval = cpuPresentationIntervalMs();

  if (interval >= 100) {
    return 45;
  }

  if (interval >= 67) {
    return 55;
  }

  if (interval >= 50) {
    return 70;
  }

  if (interval >= 40) {
    return 90;
  }

  return 120;
}

void NativeHwPlayerBackend::updateCpuFrameCost(long long elapsedMs) {
  const int clamped = std::max(1, std::min(250, static_cast<int>(elapsedMs)));

  if (cpuFrameCostAvgMs_ <= 0) {
    cpuFrameCostAvgMs_ = clamped;
    return;
  }

  cpuFrameCostAvgMs_ = (cpuFrameCostAvgMs_ * 3 + clamped) / 4;
}

bool NativeHwPlayerBackend::open(const std::string &url) {
  close();

  url_ = url;
  paused_ = false;
  error_.clear();
  yuvFrame_ = YuvFrame{};
  nativeRenderer_.reset();
  nativeRendererReady_ = false;
  nativeRendererFailed_ = false;
  nativeFramePresented_ = false;
  nativeRendererStatus_.clear();
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

  if (nativeVideoAllowed_ && preferNativeRenderer_) {
    nativeRenderer_ = createDeko3dVideoRenderer();

    if (nativeRenderer_ && nativeRenderer_->initialize()) {
      nativeRenderer_->setOverlayVisible(nativeOverlayVisible_);
      nativeRenderer_->setOverlayInfo(
        nativeOverlayTitle_,
        nativeOverlaySubtitle_,
        nativeOverlayStatus_,
        nativeOverlayControls_
      );
      nativeRendererReady_ = true;
      nativeRendererStatus_ = std::string(nativeRenderer_->name()) + " initialized";
      logLinef("[KBORE][DEKO3D] %s", nativeRendererStatus_.c_str());
    } else {
      nativeRendererReady_ = false;
      nativeRendererFailed_ = true;
      nativeRendererStatus_ =
        nativeRenderer_
          ? nativeRenderer_->error()
          : "Deko3D renderer is unavailable for this build.";

      logLinef(
        "[KBORE][DEKO3D] unavailable: %s",
        nativeRendererStatus_.c_str()
      );
      logLine("[KBORE][DEKO3D] fallback to SDL/YUV");

      if (nativeRenderer_) {
        nativeRenderer_->shutdown();
        nativeRenderer_.reset();
      }
    }
  }

  if (nativeVideoAllowed_ && nativeRendererReady_ && nativeRenderer_) {
#ifdef NSTV_USE_FFMPEG
    if (decoder_.decodeNextHardwareFrame(demuxer_)) {
      const AVFrame *frame = decoder_.latestHardwareFrame();

      if (nativeRenderer_->canRender(frame) && nativeRenderer_->renderFrame(frame)) {
        nativeFramePresented_ = true;
      } else {
        nativeRendererFailed_ = true;
        nativeRendererReady_ = false;
        nativeRendererStatus_ = "Deko3D first-frame render failed: " + nativeRenderer_->error();

        logLinef("[KBORE][DEKO3D] first frame failed: %s", nativeRendererStatus_.c_str());
        logLine("[KBORE][DEKO3D] fallback to SDL/YUV");

        decoder_.releaseLatestHardwareFrame();
        nativeRenderer_->shutdown();
        nativeRenderer_.reset();
      }
    } else {
      nativeRendererFailed_ = true;
      nativeRendererReady_ = false;
      nativeRendererStatus_ = "Deko3D first hardware decode failed: " + decoder_.error();

      logLinef("[KBORE][DEKO3D] first hardware frame failed: %s", nativeRendererStatus_.c_str());
      logLine("[KBORE][DEKO3D] fallback to SDL/YUV");

      nativeRenderer_->shutdown();
      nativeRenderer_.reset();
    }
#endif
  }

  if (!nativeFramePresented_) {
    const long long firstDecodeStartMs = nowMs();

    if (!decoder_.decodeNextVideoFrame(demuxer_, true, &yuvFrame_)) {
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

    updateCpuFrameCost(nowMs() - firstDecodeStartMs);

    if (!yuvFrame_.valid()) {
      error_ =
        "Native HW first frame decoded, but YuvFrame is invalid: " +
        decoder_.summary();

      open_ = false;
      return false;
    }
  }

  syncClockFromLatestFrame();
  lastPresentedVideoWallMs_ = nowMs();

  decoder_.startAudio();

  error_.clear();
  open_ = true;

  return true;
#endif
}

void NativeHwPlayerBackend::close() {
#ifdef NSTV_ENABLE_NATIVE_HW_PLAYER
  if (nativeRenderer_) {
    nativeRenderer_->shutdown();
    nativeRenderer_.reset();
  }

  decoder_.stopAudio();
  decoder_.close();
  demuxer_.close();
  hwProbe_.closeDevice();
#endif

  open_ = false;
  paused_ = false;
  url_.clear();
  error_.clear();
  yuvFrame_ = YuvFrame{};
  nativeRendererReady_ = false;
  nativeRendererFailed_ = false;
  nativeFramePresented_ = false;
  nativeRendererStatus_.clear();
  resetClock();
}

void NativeHwPlayerBackend::setNativeVideoAllowed(bool allowed) {
  nativeVideoAllowed_ = allowed;

  if (!allowed) {
    if (nativeRenderer_) {
      nativeRenderer_->shutdown();
      nativeRenderer_.reset();
    }

    nativeRendererReady_ = false;
    nativeFramePresented_ = false;
    return;
  }

#ifdef NSTV_ENABLE_NATIVE_HW_PLAYER
  if (
    open_ &&
    preferNativeRenderer_ &&
    !nativeRendererReady_ &&
    !nativeRendererFailed_
  ) {
    nativeRenderer_ = createDeko3dVideoRenderer();

    if (nativeRenderer_ && nativeRenderer_->initialize()) {
      nativeRenderer_->setOverlayVisible(nativeOverlayVisible_);
      nativeRenderer_->setOverlayInfo(
        nativeOverlayTitle_,
        nativeOverlaySubtitle_,
        nativeOverlayStatus_,
        nativeOverlayControls_
      );
      nativeRendererReady_ = true;
      nativeRendererStatus_ = std::string(nativeRenderer_->name()) + " initialized";
      logLinef("[KBORE][DEKO3D] %s", nativeRendererStatus_.c_str());
    } else {
      nativeRendererReady_ = false;
      nativeRendererFailed_ = true;
      nativeRendererStatus_ =
        nativeRenderer_
          ? nativeRenderer_->error()
          : "Deko3D renderer is unavailable for this build.";

      logLinef("[KBORE][DEKO3D] unavailable: %s", nativeRendererStatus_.c_str());

      if (nativeRenderer_) {
        nativeRenderer_->shutdown();
        nativeRenderer_.reset();
      }
    }
  }
#endif
}

void NativeHwPlayerBackend::setOverlayVisible(bool visible) {
  nativeOverlayVisible_ = visible;

  if (nativeRenderer_) {
    nativeRenderer_->setOverlayVisible(visible);
  }
}

void NativeHwPlayerBackend::setOverlayInfo(
  const std::string &title,
  const std::string &subtitle,
  const std::string &status,
  const std::string &controls
) {
  nativeOverlayTitle_ = title;
  nativeOverlaySubtitle_ = subtitle;
  nativeOverlayStatus_ = status;
  nativeOverlayControls_ = controls;

  if (nativeRenderer_) {
    nativeRenderer_->setOverlayInfo(title, subtitle, status, controls);
  }
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
  const long long dropStartDelayMs = playbackDelayMs(nowMs());

  /*
    Agora o drop é barato, porque dropNextVideoFrame()
    não transfere frame para CPU.

    Podemos permitir mais drops por update para HD/FHD.
  */
  const int maxDrops = maxDropsPerUpdate();

  while (dropped < maxDrops && shouldDropFrames(nowMs())) {
    if (!decoder_.dropNextVideoFrame(demuxer_)) {
      error_ = decoder_.error();
      break;
    }

    syncClockFromLatestFrame();
    ++dropped;
    ++droppedFrames_;
  }

  if (dropped > 0) {
    const long long dropLogNow = nowMs();

    if (dropLogNow - lastDropLogWallMs_ >= 1000) {
      logLinef(
        "[KBORE][PLAYBACK] dropped %d frame(s) this update total=%d delayBefore=%lldms delayAfter=%lldms threshold=%d maxDrops=%d native=%d",
        dropped,
        droppedFrames_,
        dropStartDelayMs,
        playbackDelayMs(dropLogNow),
        dropDelayThresholdMs(),
        maxDrops,
        nativeRendererReady_ && nativeRenderer_ ? 1 : 0
      );
      lastDropLogWallMs_ = dropLogNow;
    }
  }

  const int cpuInterval = cpuPresentationIntervalMs();

  if (
    cpuInterval > 0 &&
    yuvFrame_.valid() &&
    lastPresentedVideoWallMs_ > 0 &&
    nowMs() - lastPresentedVideoWallMs_ < cpuInterval
  ) {
    if (decoder_.dropNextVideoFrame(demuxer_)) {
      syncClockFromLatestFrame();
      error_.clear();
    } else {
      error_ = decoder_.error();
    }

    return hasFrame();
  }

  if (
    nativeVideoAllowed_ &&
    preferNativeRenderer_ &&
    !nativeRendererReady_ &&
    !nativeRendererFailed_
  ) {
    setNativeVideoAllowed(true);
  }

  if (nativeVideoAllowed_ && nativeRendererReady_ && nativeRenderer_) {
#ifdef NSTV_USE_FFMPEG
    if (decoder_.decodeNextHardwareFrame(demuxer_)) {
      const AVFrame *frame = decoder_.latestHardwareFrame();

      if (nativeRenderer_->canRender(frame) && nativeRenderer_->renderFrame(frame)) {
        nativeFramePresented_ = true;
        syncClockFromLatestFrame();
        error_.clear();
        return true;
      }

      nativeRendererFailed_ = true;
      nativeRendererReady_ = false;
      nativeFramePresented_ = false;
      nativeRendererStatus_ = "Deko3D render failed: " + nativeRenderer_->error();

      logLinef("[KBORE][DEKO3D] render failed: %s", nativeRendererStatus_.c_str());
      logLine("[KBORE][DEKO3D] fallback to SDL/YUV");

      decoder_.releaseLatestHardwareFrame();
      nativeRenderer_->shutdown();
      nativeRenderer_.reset();
    } else {
      error_ = decoder_.error();
      return hasFrame();
    }
#endif
  }

  const long long decodeStartMs = nowMs();

  if (!decoder_.decodeNextVideoFrame(demuxer_, true, &yuvFrame_)) {
    error_ = decoder_.error();
    return hasFrame();
  }

  updateCpuFrameCost(nowMs() - decodeStartMs);

  if (!yuvFrame_.valid()) {
    error_ = "Native decoded frame is invalid: " + decoder_.summary();
    return hasFrame();
  }

  syncClockFromLatestFrame();
  lastPresentedVideoWallMs_ = nowMs();

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
  if (paused_) {
    decoder_.stopAudio();
  } else {
    resetClockToCurrentFrame(nowMs());
    decoder_.startAudio();
  }
}

} // namespace nstv
