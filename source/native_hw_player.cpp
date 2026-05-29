#include "nstv/native_hw_player.hpp"

#include <chrono>

namespace nstv {

namespace {

long long nowMs() {
  using clock = std::chrono::steady_clock;

  return std::chrono::duration_cast<std::chrono::milliseconds>(
    clock::now().time_since_epoch()
  ).count();
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
}

void NativeHwPlayerBackend::syncClockFromLatestFrame() {
  const NativeFrameInfo &info = decoder_.latestFrameInfo();

  const long long now = nowMs();

  if (!clockStarted_) {
    clockStarted_ = true;
    playbackStartMs_ = now;
    lastFrameWallMs_ = now;

    if (info.ptsMs >= 0) {
      firstPtsMs_ = info.ptsMs;
      lastPtsMs_ = info.ptsMs;
    } else {
      firstPtsMs_ = 0;
      lastPtsMs_ = 0;
    }

    return;
  }

  if (info.ptsMs >= 0) {
    lastPtsMs_ = info.ptsMs;
  } else {
    lastPtsMs_ += fallbackFrameIntervalMs_;
  }

  lastFrameWallMs_ = now;
}

bool NativeHwPlayerBackend::shouldDecodeNow(long long now) const {
  if (!clockStarted_) {
    return true;
  }

  if (now - lastFrameWallMs_ < fallbackFrameIntervalMs_) {
    return false;
  }

  return true;
}

bool NativeHwPlayerBackend::isVideoLate(long long now) const {
  if (!clockStarted_) {
    return false;
  }

  if (firstPtsMs_ < 0 || lastPtsMs_ < 0) {
    return false;
  }

  const long long playbackElapsed = now - playbackStartMs_;
  const long long videoElapsed = lastPtsMs_ - firstPtsMs_;

  return videoElapsed + 140 < playbackElapsed;
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
    "Build with NSTV_ENABLE_NATIVE_HW_PLAYER to run the native hw device probe.";

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
    " | result: current FFmpeg exposes no usable hardware device config. "
    "Next step: build custom FFmpeg with Switch/Tegra hwaccel.";

  open_ = false;
  return false;
}

if (!hwProbe_.createBestDevice()) {
  error_ =
    hwProbe_.summary() +
    " | result: FFmpeg exposes a hardware config, but AVHWDeviceContext creation failed. "
    "Next step: check whether this hwaccel backend is supported on Horizon/libnx.";

  open_ = false;
  return false;
}

/*
  Sucesso nesta etapa:

  O FFmpeg expôs AVCodecHWConfig e conseguimos criar AVHWDeviceContext.

  Ainda não estamos decodificando via hardware.
  A próxima etapa será ligar este device no AVCodecContext.
*/

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

  int framesDecoded = 0;
  const int maxFramesPerUpdate = 4;

  do {
    if (!decoder_.decodeNextVideoFrame(demuxer_)) {
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
    ++framesDecoded;

  } while (
    framesDecoded < maxFramesPerUpdate &&
    isVideoLate(nowMs())
  );

  return true;
#endif
}

void NativeHwPlayerBackend::togglePause() {
  if (!open_) {
    return;
  }

  paused_ = !paused_;

  if (!paused_) {
    playbackStartMs_ = nowMs();

    if (lastPtsMs_ >= 0 && firstPtsMs_ >= 0) {
      playbackStartMs_ -= (lastPtsMs_ - firstPtsMs_);
    }

    lastFrameWallMs_ = nowMs();
  }
}

} // namespace nstv