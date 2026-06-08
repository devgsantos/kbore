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
  lastQualityLogWallMs_ = 0;
  currentPositionMs_ = 0;
  seekBasePositionMs_ = 0;
  seekBaseValid_ = false;
  seeking_ = false;
  requestedSeekPositionMs_ = 0;
  seekStartedWallMs_ = 0;
  seekDecodeFailures_ = 0;
}

void NativeHwPlayerBackend::resetClockToCurrentFrame(long long now) {
  const NativeFrameInfo &info = decoder_.latestFrameInfo();

  /*
    Rebase para streaming ao vivo. Se a rede/decoder travou, não faz sentido
    tentar "pagar" segundos antigos: isso só cria engasgos e A/V drift.
    Mantemos uma latência curta como alvo e seguimos do frame mais recente.
  */
  playbackStartMs_ = now - targetPlaybackDelayMs();
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
  int64_t ptsDelta = -1;

  if (info.ptsMs >= 0 && lastPtsMs_ >= 0) {
    ptsDelta = info.ptsMs - lastPtsMs_;

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

  if (info.ptsMs >= 0) {
    const int64_t startTimeMs = demuxer_.startTimeMs();

    if (startTimeMs > 0) {
      currentPositionMs_ = std::max<int64_t>(0, info.ptsMs - startTimeMs);
    } else if (seekBaseValid_) {
      currentPositionMs_ = std::max<int64_t>(0, seekBasePositionMs_ + (lastPtsMs_ - firstPtsMs_));
    } else {
      currentPositionMs_ = std::max<int64_t>(0, lastPtsMs_ - firstPtsMs_);
    }

    const int64_t duration = durationMs();
    if (duration > 0 && currentPositionMs_ > duration) {
      currentPositionMs_ = duration;
    }
  }

  if (nativeRendererReady_ && nativeRenderer_) {
    const long long delay = playbackDelayMs(now);

    const int audioQueuedMs = decoder_.audioQueuedMs();
    const int effectiveTargetMs = effectiveTargetPlaybackDelayMs(audioQueuedMs);

    if (
      delay > effectiveTargetMs ||
      delay < -targetPlaybackDelayMs() ||
      decodedFrames_ <= 3 ||
      decodedFrames_ % 120 == 0
    ) {
      logLinef(
        "[KBORE][PLAYBACK] sync decoded=%u interval=%d delay=%lld target=%d audioQ=%dms ptsDelta=%lld firstPtsMs=%lld lastPtsMs=%lld",
        decodedFrames_,
        interval,
        delay,
        effectiveTargetMs,
        audioQueuedMs,
        ptsDelta,
        firstPtsMs_,
        lastPtsMs_
      );
    }

    /*
      Controlador inteligente de clock:
      - recuperação usa o alvo base, não o alvo efetivo de A/V.
        Isso mantém a folga contra o dropThreshold e evita que a
        compensação visual de áudio vire atraso acumulado.
    */
    const int recoveryTargetMs = targetPlaybackDelayMs();

    if (delay > recoveryTargetMs) {
      const int reduction = std::min(
        std::max(1, interval - 8),
        std::max(2, static_cast<int>((delay - recoveryTargetMs) / 30))
      );
      interval = std::max(8, interval - reduction);
    } else if (delay < -targetPlaybackDelayMs()) {
      const int expansion = std::min(
        30,
        std::max(1, static_cast<int>((-delay - targetPlaybackDelayMs()) / 20))
      );
      interval = clampFrameInterval(interval + expansion);
    }
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

  const long long delay = playbackDelayMs(now);
  const int audioQueuedMs = decoder_.audioQueuedMs();
  const int effectiveTargetMs = effectiveTargetPlaybackDelayMs(audioQueuedMs);

  /*
    Recuperação de atraso continua usando o alvo base. O alvo efetivo serve
    apenas para micro-hold visual, não para decidir que o player pode esperar
    mais antes de decodificar. Isso evita o retorno dos engasgos.
  */
  if (delay > targetPlaybackDelayMs()) {
    return true;
  }

  /*
    Ajuste fino A/V: quando o áudio tem fila suficiente e o vídeo está um
    pouco cedo em relação ao alvo efetivo, seguramos só alguns milissegundos.
    O cap baixo mantém a fluidez e evita acumular delay até o frame drop.
  */
  if (
    nativeRendererReady_ &&
    nativeRenderer_ &&
    decodedFrames_ > 12 &&
    audioQueuedMs >= 250 &&
    delay + 8 < effectiveTargetMs
  ) {
    const long long holdMs = std::min(14LL, static_cast<long long>(effectiveTargetMs - delay) / 3);
    return now >= nextFrameDueMs_ + holdMs;
  }

  /*
    Se o vídeo ficou adiantado em relação ao relógio, seguramos um pouco
    mais antes de decodificar o próximo frame.
  */
  if (delay < -targetPlaybackDelayMs()) {
    const long long holdMs = std::min(80LL, (-delay) / 2);
    return now >= nextFrameDueMs_ + holdMs;
  }

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

int NativeHwPlayerBackend::maxDropsPerUpdate(long long currentDelayMs) const {
  if (nativeRendererReady_ && nativeRenderer_) {
    /*
      Render nativo consegue descartar frames sem transferência para CPU.
      O limite antigo de 1 drop por update deixava o atraso preso perto
      de 800ms por muitos segundos. Aqui a recuperação é proporcional.
    */
    if (currentDelayMs > 5000) {
      return 24;
    }

    if (currentDelayMs > 2500) {
      return 16;
    }

    if (currentDelayMs > 1500) {
      return 10;
    }

    if (currentDelayMs > 900) {
      return 6;
    }

    if (currentDelayMs > 500) {
      return 4;
    }

    if (currentDelayMs > dropDelayThresholdMs()) {
      return 2;
    }

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
    // Warmup curto: tolera buffer inicial, mas não permite drift longo.
    if (decodedFrames_ < 12) {
      return 600;
    }

    if (decodedFrames_ < 60) {
      return 350;
    }

    return 220;
  }

  const int interval = cpuPresentationIntervalMs();

  if (interval >= 100) {
    return 60;
  }

  if (interval >= 67) {
    return 75;
  }

  if (interval >= 50) {
    return 95;
  }

  if (interval >= 40) {
    return 115;
  }

  return 140;
}

int NativeHwPlayerBackend::targetPlaybackDelayMs() const {
  /*
    Alvo base do relógio de live IPTV. Mantemos 90ms no render nativo porque
    foi o ponto que eliminou engasgos e drift em testes longos. O ajuste fino
    de A/V não deve aumentar este alvo base, senão o player espera demais,
    chega perto do threshold de drop e volta a produzir microtravadas.
  */
  return nativeRendererReady_ && nativeRenderer_ ? 90 : 120;
}

int NativeHwPlayerBackend::audioLeadCompensationMs(int audioQueuedMs) const {
  if (!nativeRendererReady_ || !nativeRenderer_) {
    return 0;
  }

  /*
    Compensação visual pequena para a latência de saída do SDL. Ela é usada
    apenas como margem de apresentação, não como novo alvo de recuperação.
    Assim o vídeo pode esperar alguns ms pelo áudio sem reintroduzir drops.
  */
  if (audioQueuedMs >= 450) {
    return 24;
  }

  if (audioQueuedMs >= 350) {
    return 18;
  }

  if (audioQueuedMs >= 250) {
    return 12;
  }

  return 0;
}

int NativeHwPlayerBackend::effectiveTargetPlaybackDelayMs(int audioQueuedMs) const {
  return targetPlaybackDelayMs() + audioLeadCompensationMs(audioQueuedMs);
}

int NativeHwPlayerBackend::streamPanicDelayMs() const {
  /*
    Acima disso não é mais caso de descartar alguns frames. O log de travadas
    mostrou o delay saltando de ~700ms para vários segundos enquanto av_read_frame
    bloqueava. Para live IPTV, a recuperação correta é rebase de clock.
  */
  return nativeRendererReady_ && nativeRenderer_ ? 1200 : 900;
}

int NativeHwPlayerBackend::decodeStallBudgetMs() const {
  /*
    Um único decode/drop não deve prender o update por muito tempo. Se prendeu,
    a fonte atrasou e precisamos abandonar a compensação acumulada.
  */
  return nativeRendererReady_ && nativeRenderer_ ? 650 : 450;
}

int NativeHwPlayerBackend::dropLoopBudgetMs() const {
  /*
    Mesmo com drop barato, o loop de recuperação não pode consumir o frame loop.
  */
  return nativeRendererReady_ && nativeRenderer_ ? 180 : 120;
}

bool NativeHwPlayerBackend::recoverIfPlaybackPanic(long long now, const char *reason) {
  if (!clockStarted_) {
    return false;
  }

  const long long delayBefore = playbackDelayMs(now);
  const long long sincePresentedMs =
    lastPresentedVideoWallMs_ > 0 ? now - lastPresentedVideoWallMs_ : 0;

  const bool delayPanic = delayBefore > streamPanicDelayMs();
  const bool frameStarved = sincePresentedMs > 2000 && delayBefore > dropDelayThresholdMs();

  if (!delayPanic && !frameStarved) {
    return false;
  }

  const int audioQueuedMs = decoder_.audioQueuedMs();

  if (audioQueuedMs > 0) {
    decoder_.clearAudioQueue();
  }

  resetClockToCurrentFrame(now);

  logLinef(
    "[KBORE][PLAYBACK][RECOVER] live resync reason=%s delayBefore=%lldms delayAfter=%lldms panic=%d threshold=%d sinceFrame=%lldms audioQ=%dms decoded=%d dropped=%d",
    reason ? reason : "unknown",
    delayBefore,
    playbackDelayMs(now),
    delayPanic ? 1 : 0,
    streamPanicDelayMs(),
    sincePresentedMs,
    audioQueuedMs,
    decodedFrames_,
    droppedFrames_
  );

  return true;
}

bool NativeHwPlayerBackend::finishSeekAfterFrame(long long now) {
  if (!seeking_) {
    return false;
  }

  seeking_ = false;
  seekDecodeFailures_ = 0;
  seekBasePositionMs_ = requestedSeekPositionMs_;
  seekBaseValid_ = true;
  currentPositionMs_ = requestedSeekPositionMs_;

  // Rebase from the first decoded frame after seek. This avoids inheriting
  // pre-seek PTS/wall-clock state and prevents the recovery loop from trying
  // to drop through a VOD seek jump.
  resetClockToCurrentFrame(now);
  currentPositionMs_ = requestedSeekPositionMs_;
  seekBasePositionMs_ = requestedSeekPositionMs_;
  seekBaseValid_ = true;

  if (!paused_) {
    decoder_.startAudio();
  }

  logLinef(
    "[KBORE][PLAYBACK][VOD] seek ready position=%lld duration=%lld",
    requestedSeekPositionMs_,
    durationMs()
  );

  return true;
}

void NativeHwPlayerBackend::cancelSeekRecovery(long long now, const char *reason) {
  if (!seeking_) {
    return;
  }

  seeking_ = false;
  seekDecodeFailures_ = 0;
  resetClockToCurrentFrame(now);

  if (!paused_) {
    decoder_.startAudio();
  }

  logLinef(
    "[KBORE][PLAYBACK][VOD] seek recovery reason=%s position=%lld duration=%lld",
    reason ? reason : "unknown",
    requestedSeekPositionMs_,
    durationMs()
  );
}

void NativeHwPlayerBackend::rebalanceAudioQueue(long long now) {
  const int audioQueuedMs = decoder_.audioQueuedMs();

  if (audioQueuedMs <= 0) {
    return;
  }

  const long long delay = playbackDelayMs(now);

  /*
    Se o áudio acumulou muita fila, ele passa a tocar tarde.
    Limpamos somente em caso alto/seguro, para evitar o cenário inverso
    relatado: vídeo adiantado e áudio atrasado.
  */
  if (audioQueuedMs > 800 || (audioQueuedMs > 600 && delay < -targetPlaybackDelayMs())) {
    decoder_.clearAudioQueue();

    logLinef(
      "[KBORE][PLAYBACK][QUALITY] cleared audio queue audioQ=%dms delay=%lldms target=%d",
      audioQueuedMs,
      delay,
      effectiveTargetPlaybackDelayMs(audioQueuedMs)
    );

    return;
  }

  logStreamQuality(now, delay, audioQueuedMs);
}

void NativeHwPlayerBackend::logStreamQuality(long long now, long long delayMs, int audioQueuedMs) {
  const bool unstable =
    delayMs > dropDelayThresholdMs() ||
    delayMs < -dropDelayThresholdMs() ||
    audioQueuedMs > 600;

  if (!unstable && now - lastQualityLogWallMs_ < 5000) {
    return;
  }

  if (now - lastQualityLogWallMs_ < 1000) {
    return;
  }

  logLinef(
    "[KBORE][PLAYBACK][QUALITY] delay=%lldms target=%d dropThreshold=%d audioQ=%dms decoded=%d dropped=%d",
    delayMs,
    effectiveTargetPlaybackDelayMs(audioQueuedMs),
    dropDelayThresholdMs(),
    audioQueuedMs,
    decodedFrames_,
    droppedFrames_
  );

  lastQualityLogWallMs_ = now;
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
  audioOnlyMode_ = false;
  resetClock();

  auto failOpen = [this](const std::string &message) -> bool {
    const std::string preserved = message;
    close();
    error_ = preserved;
    open_ = false;
    return false;
  };

#ifndef NSTV_ENABLE_NATIVE_HW_PLAYER
  return failOpen(
    "Native hardware player is not enabled. "
    "Build with NSTV_ENABLE_NATIVE_HW_PLAYER to run the native hardware player."
  );
#else
  if (!demuxer_.open(url)) {
    return failOpen(demuxer_.error());
  }

  if (!demuxer_.video().exists && demuxer_.audio().exists) {
    audioOnlyMode_ = true;

    if (!decoder_.openAudio(demuxer_)) {
      return failOpen(decoder_.error());
    }

    decoder_.startAudio();
    error_.clear();
    open_ = true;

    logLinef(
      "[KBORE][PLAYBACK][AUDIO_ONLY] playing audio with static radio background url=%s decoder=%s",
      url.c_str(),
      decoder_.summary().c_str()
    );

    return true;
  }

  if (!hwProbe_.probeVideo(demuxer_)) {
    return failOpen(hwProbe_.error());
  }

  if (!hwProbe_.hasUsableDeviceConfig()) {
    return failOpen(
      hwProbe_.summary() +
      " | result: current FFmpeg exposes no usable hardware device config."
    );
  }

  if (!hwProbe_.createBestDevice()) {
    return failOpen(
      hwProbe_.summary() +
      " | result: FFmpeg exposes a hardware config, but AVHWDeviceContext creation failed."
    );
  }

  if (!decoder_.openVideoHardware(
        demuxer_,
        hwProbe_.deviceContext(),
        hwProbe_.selectedHwPixelFormat()
      )) {
    return failOpen(
      "Native HW device created, but decoder hardware open failed: " +
      decoder_.error() +
      " | hwProbe=" +
      hwProbe_.summary()
    );
  }

  if (!decoder_.openAudio(demuxer_)) {
    return failOpen(decoder_.error());
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
      nativeRenderer_->setOverlayProgress(
        nativeOverlayProgressPositionMs_,
        nativeOverlayProgressDurationMs_,
        nativeOverlayProgressVisible_
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
    if (decoder_.decodeNextHardwareFrame(demuxer_, 1200)) {
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

    if (!decoder_.decodeNextVideoFrame(demuxer_, true, &yuvFrame_, 1200)) {
      return failOpen(
        "Native HW decoder opened, but first frame decode failed: " +
        decoder_.error() +
        " | decoder=" +
        decoder_.summary() +
        " | hwProbe=" +
        hwProbe_.summary()
      );
    }

    updateCpuFrameCost(nowMs() - firstDecodeStartMs);

    if (!yuvFrame_.valid()) {
      return failOpen(
        "Native HW first frame decoded, but YuvFrame is invalid: " +
        decoder_.summary()
      );
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
  decoder_.stopAudio();

  if (nativeRenderer_) {
    nativeRenderer_->shutdown();
    nativeRenderer_.reset();
  }

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
  audioOnlyMode_ = false;
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
      nativeRenderer_->setOverlayProgress(
        nativeOverlayProgressPositionMs_,
        nativeOverlayProgressDurationMs_,
        nativeOverlayProgressVisible_
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

void NativeHwPlayerBackend::setOverlayProgress(int64_t positionMs, int64_t durationMs, bool visible) {
  nativeOverlayProgressPositionMs_ = positionMs;
  nativeOverlayProgressDurationMs_ = durationMs;
  nativeOverlayProgressVisible_ = visible;

  if (nativeRenderer_) {
    nativeRenderer_->setOverlayProgress(positionMs, durationMs, visible);
  }
}

bool NativeHwPlayerBackend::update() {
#ifndef NSTV_ENABLE_NATIVE_HW_PLAYER
  return false;
#else
  if (!open_ || paused_) {
    return hasFrame();
  }

  if (audioOnlyMode_) {
    // Keep audio-only streams fed without touching video timing or frame pacing.
    if (decoder_.audioQueuedMs() < 420) {
#ifdef NSTV_USE_FFMPEG
      if (!decoder_.decodeAudioOnly(demuxer_, 128)) {
        error_ = decoder_.error();
        return false;
      }
#endif
    }

    error_.clear();
    return true;
  }

  const long long now = nowMs();

  if (seeking_) {
    // During VOD seek, never run the normal live recovery/drop pipeline.
    // It can fight the demuxer while HLS/HTTP is repositioning and make the
    // UI appear frozen. Decode one guarded frame and resume from there.
    if (seekStartedWallMs_ > 0 && now - seekStartedWallMs_ > 5000) {
      cancelSeekRecovery(now, "timeout");
      return hasFrame();
    }
  } else {
    rebalanceAudioQueue(now);

    if (recoverIfPlaybackPanic(now, "pre-decode")) {
      return hasFrame();
    }

    if (!shouldDecodeNow(now)) {
      return hasFrame();
    }
  }

  /*
    Se estamos atrasados, pulamos frames proporcionalmente SEM transferir para CPU.

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
  const int maxDrops = seeking_ ? 0 : maxDropsPerUpdate(dropStartDelayMs);
  const long long dropLoopStartMs = nowMs();

  while (dropped < maxDrops && shouldDropFrames(nowMs())) {
    const long long singleDropStartMs = nowMs();

    if (!decoder_.dropNextVideoFrame(demuxer_)) {
      error_ = decoder_.error();
      break;
    }

    const long long singleDropEndMs = nowMs();

    syncClockFromLatestFrame();
    ++dropped;
    ++droppedFrames_;

    if (singleDropEndMs - singleDropStartMs > decodeStallBudgetMs()) {
      recoverIfPlaybackPanic(singleDropEndMs, "drop-read-stall");
      break;
    }

    if (singleDropEndMs - dropLoopStartMs > dropLoopBudgetMs()) {
      logLinef(
        "[KBORE][PLAYBACK][RECOVER] capped drop loop elapsed=%lldms dropped=%d delay=%lldms budget=%d",
        singleDropEndMs - dropLoopStartMs,
        dropped,
        playbackDelayMs(singleDropEndMs),
        dropLoopBudgetMs()
      );
      break;
    }
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
    !seeking_ &&
    cpuInterval > 0 &&
    yuvFrame_.valid() &&
    lastPresentedVideoWallMs_ > 0 &&
    nowMs() - lastPresentedVideoWallMs_ < cpuInterval
  ) {
    const long long skipStartMs = nowMs();

    if (decoder_.dropNextVideoFrame(demuxer_)) {
      const long long skipEndMs = nowMs();
      syncClockFromLatestFrame();

      if (skipEndMs - skipStartMs > decodeStallBudgetMs()) {
        recoverIfPlaybackPanic(skipEndMs, "cpu-rate-skip-stall");
      }

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
    const long long decodeStartMs = nowMs();

    if (decoder_.decodeNextHardwareFrame(demuxer_, seeking_ ? 600 : 80)) {
      const long long decodeEndMs = nowMs();
      const AVFrame *frame = decoder_.latestHardwareFrame();

      if (nativeRenderer_->canRender(frame) && nativeRenderer_->renderFrame(frame)) {
        nativeFramePresented_ = true;
        syncClockFromLatestFrame();
        lastPresentedVideoWallMs_ = nowMs();

        const bool wasSeeking = seeking_;
        finishSeekAfterFrame(lastPresentedVideoWallMs_);

        if (!wasSeeking && decodeEndMs - decodeStartMs > decodeStallBudgetMs()) {
          recoverIfPlaybackPanic(lastPresentedVideoWallMs_, "hardware-decode-stall");
        } else if (!wasSeeking) {
          recoverIfPlaybackPanic(lastPresentedVideoWallMs_, "hardware-delay-panic");
        }

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

      if (seeking_) {
        ++seekDecodeFailures_;

        if (seekDecodeFailures_ >= 5) {
          cancelSeekRecovery(nowMs(), error_.c_str());
        }
      }

      return hasFrame();
    }
#endif
  }

  const long long decodeStartMs = nowMs();

  if (!decoder_.decodeNextVideoFrame(demuxer_, true, &yuvFrame_, seeking_ ? 600 : 80)) {
    error_ = decoder_.error();

    if (seeking_) {
      ++seekDecodeFailures_;

      if (seekDecodeFailures_ >= 5) {
        cancelSeekRecovery(nowMs(), error_.c_str());
      }
    }

    return hasFrame();
  }

  const long long decodeEndMs = nowMs();

  updateCpuFrameCost(decodeEndMs - decodeStartMs);

  if (!yuvFrame_.valid()) {
    error_ = "Native decoded frame is invalid: " + decoder_.summary();
    return hasFrame();
  }

  syncClockFromLatestFrame();
  lastPresentedVideoWallMs_ = nowMs();

  const bool wasSeeking = seeking_;
  finishSeekAfterFrame(lastPresentedVideoWallMs_);

  if (!wasSeeking && decodeEndMs - decodeStartMs > decodeStallBudgetMs()) {
    recoverIfPlaybackPanic(lastPresentedVideoWallMs_, "cpu-decode-stall");
  } else if (!wasSeeking) {
    recoverIfPlaybackPanic(lastPresentedVideoWallMs_, "cpu-delay-panic");
  }

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

bool NativeHwPlayerBackend::canSeek() const {
  return open_ && !audioOnlyMode_ && demuxer_.canSeek() && durationMs() > 0;
}

int64_t NativeHwPlayerBackend::durationMs() const {
  return demuxer_.durationMs();
}

int64_t NativeHwPlayerBackend::positionMs() const {
  const int64_t duration = durationMs();

  if (duration > 0) {
    return std::max<int64_t>(0, std::min<int64_t>(currentPositionMs_, duration));
  }

  return std::max<int64_t>(0, currentPositionMs_);
}

bool NativeHwPlayerBackend::seekToMs(int64_t positionMs) {
  if (!canSeek() || seeking_) {
    return false;
  }

  const int64_t duration = durationMs();

  if (positionMs < 0) {
    positionMs = 0;
  }

  if (duration > 0 && positionMs > duration) {
    positionMs = duration;
  }

  const long long startWallMs = nowMs();

  // A VOD seek must be a controlled transition. Stop/clear audio first and
  // keep it paused until the first post-seek video frame is rendered; otherwise
  // the audio queue can restart against stale video state.
  decoder_.stopAudio();
  decoder_.clearAudioQueue();

  seeking_ = true;
  requestedSeekPositionMs_ = positionMs;
  seekStartedWallMs_ = startWallMs;
  seekDecodeFailures_ = 0;

  if (!demuxer_.seekToMs(positionMs)) {
    error_ = demuxer_.error();
    seeking_ = false;

    if (!paused_) {
      decoder_.startAudio();
    }

    logLinef(
      "[KBORE][PLAYBACK][VOD] seek failed position=%lld duration=%lld error=%s",
      positionMs,
      duration,
      error_.c_str()
    );

    return false;
  }

  decoder_.flushForSeek();
  yuvFrame_ = YuvFrame{};
  nativeFramePresented_ = false;

  currentPositionMs_ = positionMs;
  seekBasePositionMs_ = positionMs;
  seekBaseValid_ = true;

  clockStarted_ = false;
  firstPtsMs_ = -1;
  lastPtsMs_ = -1;
  playbackStartMs_ = 0;
  lastFrameWallMs_ = 0;
  lastPresentedVideoWallMs_ = startWallMs;
  nextFrameDueMs_ = 0;
  currentFrameIntervalMs_ = fallbackFrameIntervalMs_;
  lastDropLogWallMs_ = 0;

  logLinef(
    "[KBORE][PLAYBACK][VOD] seek requested position=%lld duration=%lld elapsed=%lld",
    positionMs,
    duration,
    nowMs() - startWallMs
  );

  return true;
}

} // namespace nstv
