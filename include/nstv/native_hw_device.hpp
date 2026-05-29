#pragma once

#include "nstv/native_demuxer.hpp"

#include <string>
#include <vector>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace nstv {

struct NativeHwConfigInfo {
  bool exists = false;

  int index = -1;

  std::string pixelFormat;
  std::string deviceType;
  std::string methods;

  bool hasHwDeviceCtx = false;
  bool hasHwFramesCtx = false;
  bool hasInternal = false;
  bool hasAdHoc = false;

  int rawPixelFormat = -1;
  int rawDeviceType = 0;
};

class NativeHwDeviceProbe {
public:
  NativeHwDeviceProbe();
  ~NativeHwDeviceProbe();

  bool probeVideo(const NativeDemuxer &demuxer);
  bool createBestDevice();
  void closeDevice();

  bool hasAnyConfig() const { return !configs_.empty(); }
  bool hasUsableDeviceConfig() const { return hasUsableDeviceConfig_; }
  bool hasCreatedDevice() const { return deviceCreated_; }

  const std::vector<NativeHwConfigInfo> &configs() const { return configs_; }

  const std::string &codecName() const { return codecName_; }
  const std::string &decoderName() const { return decoderName_; }
  const std::string &selectedDeviceType() const { return selectedDeviceType_; }
  const std::string &selectedPixelFormat() const { return selectedPixelFormat_; }

  const std::string &error() const { return error_; }
  const std::string &summary() const { return summary_; }

#ifdef NSTV_USE_FFMPEG
  AVBufferRef *deviceContext() const;
  AVPixelFormat selectedHwPixelFormat() const;
  AVHWDeviceType selectedHwDeviceType() const;
#endif

private:
  struct Impl;
  Impl *impl_ = nullptr;

  std::vector<NativeHwConfigInfo> configs_;

  bool hasUsableDeviceConfig_ = false;
  bool deviceCreated_ = false;

  int selectedConfigIndex_ = -1;

  std::string codecName_;
  std::string decoderName_;
  std::string selectedDeviceType_;
  std::string selectedPixelFormat_;
  std::string error_;
  std::string summary_;

  void reset();
  void rebuildSummary();
};

} // namespace nstv