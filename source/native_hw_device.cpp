#include "nstv/native_hw_device.hpp"

#include <sstream>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}
#endif

namespace nstv {

#ifdef NSTV_USE_FFMPEG
struct NativeHwDeviceProbe::Impl {
  AVBufferRef *deviceContext = nullptr;

  AVHWDeviceType selectedDeviceType = AV_HWDEVICE_TYPE_NONE;
  AVPixelFormat selectedPixelFormat = AV_PIX_FMT_NONE;
};
#endif

namespace {

#ifdef NSTV_USE_FFMPEG

std::string ffmpegError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

std::string codecNameFromId(AVCodecID codecId) {
  const char *name = avcodec_get_name(codecId);

  if (name && name[0] != '\0') {
    return name;
  }

  const AVCodecDescriptor *descriptor = avcodec_descriptor_get(codecId);

  if (descriptor && descriptor->name) {
    return descriptor->name;
  }

  return "unknown";
}

std::string pixelFormatName(AVPixelFormat format) {
  const char *name = av_get_pix_fmt_name(format);

  if (name && name[0] != '\0') {
    return name;
  }

  return "unknown";
}

std::string deviceTypeName(AVHWDeviceType type) {
  const char *name = av_hwdevice_get_type_name(type);

  if (name && name[0] != '\0') {
    return name;
  }

  return "none";
}

std::string methodsToString(int methods) {
  std::ostringstream out;
  bool first = true;

#ifdef AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX
  if (methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
    if (!first) {
      out << ",";
    }

    out << "HW_DEVICE_CTX";
    first = false;
  }
#endif

#ifdef AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX
  if (methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) {
    if (!first) {
      out << ",";
    }

    out << "HW_FRAMES_CTX";
    first = false;
  }
#endif

#ifdef AV_CODEC_HW_CONFIG_METHOD_INTERNAL
  if (methods & AV_CODEC_HW_CONFIG_METHOD_INTERNAL) {
    if (!first) {
      out << ",";
    }

    out << "INTERNAL";
    first = false;
  }
#endif

#ifdef AV_CODEC_HW_CONFIG_METHOD_AD_HOC
  if (methods & AV_CODEC_HW_CONFIG_METHOD_AD_HOC) {
    if (!first) {
      out << ",";
    }

    out << "AD_HOC";
    first = false;
  }
#endif

  if (first) {
    return "none";
  }

  return out.str();
}

bool hasHwDeviceCtx(int methods) {
#ifdef AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX
  return (methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0;
#else
  (void)methods;
  return false;
#endif
}

bool hasHwFramesCtx(int methods) {
#ifdef AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX
  return (methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) != 0;
#else
  (void)methods;
  return false;
#endif
}

bool hasInternal(int methods) {
#ifdef AV_CODEC_HW_CONFIG_METHOD_INTERNAL
  return (methods & AV_CODEC_HW_CONFIG_METHOD_INTERNAL) != 0;
#else
  (void)methods;
  return false;
#endif
}

bool hasAdHoc(int methods) {
#ifdef AV_CODEC_HW_CONFIG_METHOD_AD_HOC
  return (methods & AV_CODEC_HW_CONFIG_METHOD_AD_HOC) != 0;
#else
  (void)methods;
  return false;
#endif
}

#endif

} // namespace

NativeHwDeviceProbe::NativeHwDeviceProbe() {
#ifdef NSTV_USE_FFMPEG
  impl_ = new Impl();
#endif
}

NativeHwDeviceProbe::~NativeHwDeviceProbe() {
  closeDevice();

#ifdef NSTV_USE_FFMPEG
  delete impl_;
  impl_ = nullptr;
#endif
}

void NativeHwDeviceProbe::reset() {
  closeDevice();

  configs_.clear();

  hasUsableDeviceConfig_ = false;
  deviceCreated_ = false;

  selectedConfigIndex_ = -1;

  codecName_.clear();
  decoderName_.clear();
  selectedDeviceType_.clear();
  selectedPixelFormat_.clear();
  error_.clear();
  summary_.clear();

#ifdef NSTV_USE_FFMPEG
  if (impl_) {
    impl_->selectedDeviceType = AV_HWDEVICE_TYPE_NONE;
    impl_->selectedPixelFormat = AV_PIX_FMT_NONE;
  }
#endif
}

void NativeHwDeviceProbe::rebuildSummary() {
  std::ostringstream out;

  out
    << "Native HW probe: codec="
    << (codecName_.empty() ? "unknown" : codecName_)
    << " decoder="
    << (decoderName_.empty() ? "unknown" : decoderName_)
    << " configs="
    << configs_.size();

  if (configs_.empty()) {
    out << " | no AVCodecHWConfig exposed by current FFmpeg build";
    summary_ = out.str();
    return;
  }

  for (const NativeHwConfigInfo &config : configs_) {
    out
      << " | #"
      << config.index
      << " pix_fmt="
      << config.pixelFormat
      << " device="
      << config.deviceType
      << " methods="
      << config.methods;
  }

  out
    << " | usableDeviceConfig="
    << (hasUsableDeviceConfig_ ? "yes" : "no");

  if (selectedConfigIndex_ >= 0) {
    out
      << " | selected=#"
      << selectedConfigIndex_
      << " device="
      << (selectedDeviceType_.empty() ? "none" : selectedDeviceType_)
      << " pix_fmt="
      << (selectedPixelFormat_.empty() ? "none" : selectedPixelFormat_);
  }

  out
    << " | createdDevice="
    << (deviceCreated_ ? "yes" : "no");

  if (!error_.empty()) {
    out << " | error=" << error_;
  }

  summary_ = out.str();
}

bool NativeHwDeviceProbe::probeVideo(const NativeDemuxer &demuxer) {
  reset();

#ifndef NSTV_USE_FFMPEG
  error_ = "NativeHwDeviceProbe requires NSTV_USE_FFMPEG.";
  rebuildSummary();
  return false;
#else
  if (!demuxer.isOpen()) {
    error_ = "NativeHwDeviceProbe requires an open NativeDemuxer.";
    rebuildSummary();
    return false;
  }

  if (!demuxer.video().exists) {
    error_ = "NativeHwDeviceProbe did not receive a valid video stream.";
    rebuildSummary();
    return false;
  }

  AVFormatContext *format = demuxer.formatContext();

  if (!format) {
    error_ = "NativeHwDeviceProbe could not access AVFormatContext.";
    rebuildSummary();
    return false;
  }

  const int videoIndex = demuxer.video().index;

  if (videoIndex < 0 || videoIndex >= static_cast<int>(format->nb_streams)) {
    error_ = "NativeHwDeviceProbe received invalid video stream index.";
    rebuildSummary();
    return false;
  }

  AVStream *stream = format->streams[videoIndex];

  if (!stream || !stream->codecpar) {
    error_ = "NativeHwDeviceProbe could not access video codec parameters.";
    rebuildSummary();
    return false;
  }

  AVCodecParameters *params = stream->codecpar;

  codecName_ = codecNameFromId(params->codec_id);

  const AVCodec *decoder = avcodec_find_decoder(params->codec_id);

  if (!decoder) {
    error_ = "NativeHwDeviceProbe could not find decoder for codec: " + codecName_;
    rebuildSummary();
    return false;
  }

  decoderName_ = decoder->name ? decoder->name : "unknown";

  for (int i = 0;; ++i) {
    const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);

    if (!config) {
      break;
    }

    NativeHwConfigInfo info;

    info.exists = true;
    info.index = i;
    info.pixelFormat = pixelFormatName(config->pix_fmt);
    info.deviceType = deviceTypeName(config->device_type);
    info.methods = methodsToString(config->methods);

    info.hasHwDeviceCtx = hasHwDeviceCtx(config->methods);
    info.hasHwFramesCtx = hasHwFramesCtx(config->methods);
    info.hasInternal = hasInternal(config->methods);
    info.hasAdHoc = hasAdHoc(config->methods);

    info.rawPixelFormat = static_cast<int>(config->pix_fmt);
    info.rawDeviceType = static_cast<int>(config->device_type);

  const bool isNvtegraConfig =
    info.deviceType == "nvtegra" ||
    info.pixelFormat == "nvtegra";

  const bool hasStandardMethod =
    info.hasHwDeviceCtx ||
    info.hasHwFramesCtx ||
    info.hasInternal ||
    info.hasAdHoc;

  /*
    O backend nvtegra pode aparecer com methods=none, mas ainda assim
    expor device=nvtegra e pix_fmt=nvtegra.

    Então não podemos descartar apenas porque methods=none.
  */
  if (
    config->device_type != AV_HWDEVICE_TYPE_NONE &&
    (hasStandardMethod || isNvtegraConfig)
  ) {
    hasUsableDeviceConfig_ = true;
  }

    configs_.push_back(info);
  }

  if (configs_.empty()) {
    error_ =
      "Current FFmpeg decoder '" +
      decoderName_ +
      "' exposes no AVCodecHWConfig for codec '" +
      codecName_ +
      "'.";
  }

  rebuildSummary();

  return true;
#endif
}

bool NativeHwDeviceProbe::createBestDevice() {
#ifndef NSTV_USE_FFMPEG
  error_ = "NativeHwDeviceProbe requires NSTV_USE_FFMPEG.";
  rebuildSummary();
  return false;
#else
  closeDevice();

  if (!impl_) {
    error_ = "NativeHwDeviceProbe internal state is unavailable.";
    rebuildSummary();
    return false;
  }

  if (configs_.empty()) {
    error_ = "Cannot create hardware device because no AVCodecHWConfig exists.";
    rebuildSummary();
    return false;
  }

  const NativeHwConfigInfo *chosen = nullptr;

  /*
    Prioridade 1:
    Config explicitamente nvtegra, mesmo com methods=none.
  */
  for (const NativeHwConfigInfo &config : configs_) {
    const bool isNvtegraConfig =
      config.deviceType == "nvtegra" ||
      config.pixelFormat == "nvtegra";

    if (
      config.rawDeviceType != static_cast<int>(AV_HWDEVICE_TYPE_NONE) &&
      isNvtegraConfig
    ) {
      chosen = &config;
      break;
    }
  }

  /*
    Prioridade 2:
    Config padrão FFmpeg com HW_DEVICE_CTX/HW_FRAMES_CTX.
  */
  if (!chosen) {
    for (const NativeHwConfigInfo &config : configs_) {
      if (
        config.rawDeviceType != static_cast<int>(AV_HWDEVICE_TYPE_NONE) &&
        (config.hasHwDeviceCtx || config.hasHwFramesCtx)
      ) {
        chosen = &config;
        break;
      }
    }
  }

  /*
    Prioridade 3:
    Qualquer config com device_type válido.
  */
  if (!chosen) {
    for (const NativeHwConfigInfo &config : configs_) {
      if (config.rawDeviceType != static_cast<int>(AV_HWDEVICE_TYPE_NONE)) {
        chosen = &config;
        break;
      }
    }
  }

  if (!chosen) {
    error_ = "No hardware config with a valid AVHWDeviceType was found.";
    rebuildSummary();
    return false;
  }

  selectedConfigIndex_ = chosen->index;
  selectedDeviceType_ = chosen->deviceType;
  selectedPixelFormat_ = chosen->pixelFormat;

  impl_->selectedDeviceType = static_cast<AVHWDeviceType>(chosen->rawDeviceType);
  impl_->selectedPixelFormat = static_cast<AVPixelFormat>(chosen->rawPixelFormat);

  AVBufferRef *device = nullptr;

  int ret = av_hwdevice_ctx_create(
    &device,
    impl_->selectedDeviceType,
    nullptr,
    nullptr,
    0
  );

  if (ret < 0) {
    error_ =
      "av_hwdevice_ctx_create failed for device '" +
      selectedDeviceType_ +
      "': " +
      ffmpegError(ret);

    if (device) {
      av_buffer_unref(&device);
    }

    deviceCreated_ = false;
    rebuildSummary();
    return false;
  }

  impl_->deviceContext = device;
  deviceCreated_ = true;
  error_.clear();

  rebuildSummary();

  return true;
#endif
}

void NativeHwDeviceProbe::closeDevice() {
#ifdef NSTV_USE_FFMPEG
  if (impl_ && impl_->deviceContext) {
    av_buffer_unref(&impl_->deviceContext);
    impl_->deviceContext = nullptr;
  }
#endif

  deviceCreated_ = false;
}

#ifdef NSTV_USE_FFMPEG
AVBufferRef *NativeHwDeviceProbe::deviceContext() const {
  return impl_ ? impl_->deviceContext : nullptr;
}

AVPixelFormat NativeHwDeviceProbe::selectedHwPixelFormat() const {
  return impl_ ? impl_->selectedPixelFormat : AV_PIX_FMT_NONE;
}

AVHWDeviceType NativeHwDeviceProbe::selectedHwDeviceType() const {
  return impl_ ? impl_->selectedDeviceType : AV_HWDEVICE_TYPE_NONE;
}
#endif

} // namespace nstv