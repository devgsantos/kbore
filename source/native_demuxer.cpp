#include "nstv/native_demuxer.hpp"
#include "nstv/log.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/version.h>
}
#endif

namespace nstv {

#ifdef NSTV_USE_FFMPEG
struct NativeDemuxer::Impl {
  AVFormatContext *format = nullptr;
  long long ioDeadlineMs = 0;
  bool ioInterrupted = false;
};
#endif

namespace {

#ifdef NSTV_USE_FFMPEG

long long steadyNowMs() {
  using clock = std::chrono::steady_clock;

  return std::chrono::duration_cast<std::chrono::milliseconds>(
    clock::now().time_since_epoch()
  ).count();
}

int demuxInterruptCallback(void *opaque) {
  auto *impl = static_cast<NativeDemuxer::Impl *>(opaque);

  if (!impl || impl->ioDeadlineMs <= 0) {
    return 0;
  }

  if (steadyNowMs() >= impl->ioDeadlineMs) {
    impl->ioInterrupted = true;
    return 1;
  }

  return 0;
}

void installInterruptCallback(NativeDemuxer::Impl *impl) {
  if (!impl || !impl->format) {
    return;
  }

  impl->format->interrupt_callback.callback = demuxInterruptCallback;
  impl->format->interrupt_callback.opaque = impl;
}

std::string ffmpegError(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

bool startsWithHttps(const std::string &url) {
  return url.rfind("https://", 0) == 0 || url.rfind("HTTPS://", 0) == 0;
}

std::string httpFallbackUrl(const std::string &url) {
  if (url.rfind("https://", 0) == 0) {
    return "http://" + url.substr(8);
  }

  if (url.rfind("HTTPS://", 0) == 0) {
    return "http://" + url.substr(8);
  }

  return url;
}

std::string httpFallbackUrlWithoutExplicitPort(const std::string &url) {
  if (!startsWithHttps(url)) {
    return url;
  }

  const std::string rest = url.substr(8);
  const std::size_t slash = rest.find('/');
  const std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
  const std::string path = slash == std::string::npos ? std::string() : rest.substr(slash);

  std::string host = authority;

  if (!host.empty() && host.front() == '[') {
    const std::size_t close = host.find(']');
    if (close != std::string::npos && close + 1 < host.size() && host[close + 1] == ':') {
      host = host.substr(0, close + 1);
    }
  } else {
    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) {
      host = host.substr(0, colon);
    }
  }

  if (host.empty()) {
    return httpFallbackUrl(url);
  }

  return "http://" + host + path;
}


char lowerAscii(char value) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

std::string toLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), lowerAscii);
  return value;
}

void replaceCaseInsensitiveFrom(
  std::string &value,
  std::size_t start,
  const std::string &needle,
  const std::string &replacement
) {
  if (needle.empty() || start >= value.size()) {
    return;
  }

  std::string lowerValue = toLowerAscii(value);
  const std::string lowerNeedle = toLowerAscii(needle);

  std::size_t pos = lowerValue.find(lowerNeedle, start);
  while (pos != std::string::npos) {
    value.replace(pos, needle.size(), replacement);
    lowerValue.replace(pos, needle.size(), toLowerAscii(replacement));
    pos = lowerValue.find(lowerNeedle, pos + replacement.size());
  }
}

std::string rewriteNestedHttpsToHttp(const std::string &url) {
  std::string rewritten = url;
  const std::size_t query = rewritten.find('?');

  if (query == std::string::npos || query + 1 >= rewritten.size()) {
    return rewritten;
  }

  // Some HLS provider URLs are HTTP-compatible after the main scheme fallback,
  // but still carry nested HTTPS upstreams inside query parameters, e.g.
  //   ...m3u8?yo.up=https://cdn.example/...
  // FFmpeg without TLS can still fail when the provider follows that nested URL.
  // Rewriting only the query keeps the real URL scheme untouched.
  replaceCaseInsensitiveFrom(rewritten, query + 1, "https://", "http://");
  replaceCaseInsensitiveFrom(rewritten, query + 1, "https%3A%2F%2F", "http%3A%2F%2F");
  return rewritten;
}

void addUniqueFallback(std::vector<std::string> &fallbacks, const std::string &url) {
  if (url.empty()) {
    return;
  }

  if (std::find(fallbacks.begin(), fallbacks.end(), url) == fallbacks.end()) {
    fallbacks.push_back(url);
  }
}

std::string urlOrigin(const std::string &url) {
  const std::size_t scheme = url.find("://");
  if (scheme == std::string::npos) {
    return std::string();
  }

  const std::size_t authorityStart = scheme + 3;
  const std::size_t slash = url.find('/', authorityStart);
  if (slash == std::string::npos) {
    return url + "/";
  }

  return url.substr(0, slash + 1);
}

std::string urlScheme(const std::string &url) {
  const std::size_t colon = url.find(':');
  if (colon == std::string::npos || colon == 0) {
    return std::string();
  }

  for (std::size_t i = 0; i < colon; ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (!std::isalpha(c) && !std::isdigit(c) && url[i] != '+' && url[i] != '-' && url[i] != '.') {
      return std::string();
    }
  }

  return toLowerAscii(url.substr(0, colon));
}

bool isProtocolNotFound(int code) {
  return ffmpegError(code).find("Protocol not found") != std::string::npos;
}

bool isHttp4xxError(int code) {
  const std::string error = ffmpegError(code);
  return error.find("Server returned 4") != std::string::npos ||
    error.find("4XX Client Error") != std::string::npos;
}

bool isNetworkIoError(int code) {
  return ffmpegError(code).find("I/O error") != std::string::npos;
}

bool shouldRetryWithBrowserHeaders(int code) {
  return isHttp4xxError(code) || isNetworkIoError(code);
}

std::string unsupportedProtocolMessage(const std::string &url) {
  const std::string scheme = urlScheme(url);

  if (scheme.empty() || scheme == "http" || scheme == "https") {
    return std::string();
  }

  return " | Unsupported stream protocol '" + scheme +
    "' in this FFmpeg build. Kboré currently treats HTTP/HLS directly and HTTPS only through HTTP-compatible fallback.";
}

void setLiveInputOptions(
  AVDictionary **options,
  const std::string &url,
  const char *userAgent,
  bool browserHeaders
) {
  av_dict_set(options, "user_agent", userAgent, 0);

  if (browserHeaders) {
    const std::string origin = urlOrigin(url);
    av_dict_set(options, "headers", "Accept: */*\r\nConnection: keep-alive\r\n", 0);
    if (!origin.empty()) {
      av_dict_set(options, "referer", origin.c_str(), 0);
    }
  }

  av_dict_set(options, "reconnect", "1", 0);
  av_dict_set(options, "reconnect_streamed", "1", 0);
  av_dict_set(options, "reconnect_delay_max", "2", 0);
  /*
    IPTV ao vivo não pode deixar av_read_frame bloquear por muitos segundos.
    O log de travadas mostrou o clock acumulando dezenas de segundos enquanto
    o demuxer esperava dados. Timeouts menores fazem o player congelar menos
    e permitem o resync de live latency agir.
  */
  av_dict_set(options, "timeout", "3000000", 0);
  av_dict_set(options, "rw_timeout", "2000000", 0);
  av_dict_set(options, "fflags", "nobuffer", 0);
  av_dict_set(options, "flags", "low_delay", 0);
  av_dict_set(options, "max_delay", "500000", 0);
  av_dict_set(options, "analyzeduration", "1000000", 0);
  av_dict_set(options, "probesize", "65536", 0);
}

int openLiveInput(
  AVFormatContext **format,
  const std::string &url,
  const char *userAgent,
  bool browserHeaders = false
) {
  AVDictionary *options = nullptr;
  setLiveInputOptions(&options, url, userAgent, browserHeaders);

  int ret = avformat_open_input(
    format,
    url.c_str(),
    nullptr,
    &options
  );

  av_dict_free(&options);
  return ret;
}

int openLiveInputWithHttpCompatibilityRetries(
  NativeDemuxer::Impl *impl,
  const std::string &url,
  const char *normalUserAgent,
  const char *browserUserAgent
) {
  int ret = openLiveInput(&impl->format, url, normalUserAgent, false);

  if (ret < 0 && shouldRetryWithBrowserHeaders(ret)) {
    logLinef(
      "[KBORE][NETWORK][HTTP] retrying stream with browser-compatible headers url=%s reason=%s",
      url.c_str(),
      ffmpegError(ret).c_str()
    );

    if (impl->format) {
      avformat_close_input(&impl->format);
      impl->format = nullptr;
    }

    const int browserRet = openLiveInput(&impl->format, url, browserUserAgent, true);
    if (browserRet >= 0) {
      logLine("[KBORE][NETWORK][HTTP] browser-compatible headers opened stream successfully");
      return browserRet;
    }

    logLinef(
      "[KBORE][NETWORK][HTTP] browser-compatible headers failed error=%s",
      ffmpegError(browserRet).c_str()
    );
    ret = browserRet;
  }

  return ret;
}

int64_t durationFromFormatMs(AVFormatContext *format) {
  if (!format) {
    return 0;
  }

  if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
    return av_rescale(format->duration, 1000, AV_TIME_BASE);
  }

  int64_t bestMs = 0;

  for (unsigned int i = 0; i < format->nb_streams; ++i) {
    AVStream *stream = format->streams[i];

    if (!stream || stream->duration == AV_NOPTS_VALUE || stream->duration <= 0) {
      continue;
    }

    AVRational milliseconds{1, 1000};
    const int64_t streamMs = av_rescale_q(stream->duration, stream->time_base, milliseconds);

    if (streamMs > bestMs) {
      bestMs = streamMs;
    }
  }

  return bestMs;
}

int64_t startTimeFromFormatMs(AVFormatContext *format) {
  if (!format) {
    return 0;
  }

  if (format->start_time != AV_NOPTS_VALUE && format->start_time > 0) {
    return av_rescale(format->start_time, 1000, AV_TIME_BASE);
  }

  int64_t bestMs = -1;

  for (unsigned int i = 0; i < format->nb_streams; ++i) {
    AVStream *stream = format->streams[i];

    if (!stream || stream->start_time == AV_NOPTS_VALUE || stream->start_time < 0) {
      continue;
    }

    AVRational milliseconds{1, 1000};
    const int64_t streamMs = av_rescale_q(stream->start_time, stream->time_base, milliseconds);

    if (bestMs < 0 || streamMs < bestMs) {
      bestMs = streamMs;
    }
  }

  return bestMs > 0 ? bestMs : 0;
}

bool formatCanSeek(AVFormatContext *format) {
  if (!format) {
    return false;
  }

  if (durationFromFormatMs(format) <= 0) {
    return false;
  }

  if (format->pb && (format->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
    return true;
  }

  // HLS VOD and some HTTP sources do not always expose AVIO_SEEKABLE_NORMAL,
  // but av_seek_frame can still seek using the demuxer. Treat finite duration
  // as seekable and let seekToMs() report precise failure if unsupported.
  return true;
}

std::string codecName(AVCodecID codecId) {
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

int audioChannels(const AVCodecParameters *params) {
#if LIBAVUTIL_VERSION_MAJOR >= 57
  return params->ch_layout.nb_channels;
#else
  return params->channels;
#endif
}

NativeStreamInfo buildVideoInfo(int index, const AVCodecParameters *params) {
  NativeStreamInfo info;

  info.exists = true;
  info.index = index;
  info.type = "video";
  info.codecName = codecName(params->codec_id);
  info.width = params->width;
  info.height = params->height;

  return info;
}

NativeStreamInfo buildAudioInfo(int index, const AVCodecParameters *params) {
  NativeStreamInfo info;

  info.exists = true;
  info.index = index;
  info.type = "audio";
  info.codecName = codecName(params->codec_id);
  info.sampleRate = params->sample_rate;
  info.channels = audioChannels(params);

  return info;
}

#endif

} // namespace

NativeDemuxer::NativeDemuxer() {
#ifdef NSTV_USE_FFMPEG
  impl_ = new Impl();
  avformat_network_init();
#endif
}

NativeDemuxer::~NativeDemuxer() {
  close();

#ifdef NSTV_USE_FFMPEG
  avformat_network_deinit();

  delete impl_;
  impl_ = nullptr;
#endif
}

void NativeDemuxer::resetState() {
  open_ = false;

  url_.clear();
  error_.clear();
  summary_.clear();

  video_ = NativeStreamInfo{};
  audio_ = NativeStreamInfo{};
}

bool NativeDemuxer::open(const std::string &url) {
  close();

  resetState();

  url_ = url;

#ifndef NSTV_USE_FFMPEG
  error_ = "NativeDemuxer requires NSTV_USE_FFMPEG.";
  return false;
#else
  if (!impl_) {
    error_ = "NativeDemuxer internal state is unavailable.";
    return false;
  }

  int ret = openLiveInputWithHttpCompatibilityRetries(
    impl_,
    url,
    "NSTV-NativeDemuxer/0.1",
    "Mozilla/5.0 (Nintendo Switch; Kbore/1.0) AppleWebKit/605.1.15"
  );

  const bool httpsProtocolMissing = ret < 0 && startsWithHttps(url) && isProtocolNotFound(ret);

  if (httpsProtocolMissing) {
    std::vector<std::string> fallbacks;
    const std::string withPort = httpFallbackUrl(url);
    const std::string withoutPort = httpFallbackUrlWithoutExplicitPort(url);
    addUniqueFallback(fallbacks, withPort);
    addUniqueFallback(fallbacks, rewriteNestedHttpsToHttp(withPort));
    addUniqueFallback(fallbacks, withoutPort);
    addUniqueFallback(fallbacks, rewriteNestedHttpsToHttp(withoutPort));

    int lastFallbackRet = ret;
    std::string lastTriedFallback;

    for (const std::string &fallback : fallbacks) {
      if (fallback.empty() || fallback == url || fallback == lastTriedFallback) {
        continue;
      }

      lastTriedFallback = fallback;

      if (fallback != fallbacks.front()) {
        logLinef(
          "[KBORE][NETWORK][HTTPS] retrying HTTP fallback without explicit HTTPS port url=%s",
          fallback.c_str()
        );
      } else {
        logLinef(
          "[KBORE][NETWORK][HTTPS] FFmpeg HTTPS protocol unavailable; retrying HTTP fallback url=%s",
          fallback.c_str()
        );
      }

      if (impl_->format) {
        avformat_close_input(&impl_->format);
        impl_->format = nullptr;
      }

      const int fallbackRet = openLiveInputWithHttpCompatibilityRetries(
        impl_,
        fallback,
        "NSTV-NativeDemuxer/0.1",
        "Mozilla/5.0 (Nintendo Switch; Kbore/1.0) AppleWebKit/605.1.15"
      );
      lastFallbackRet = fallbackRet;

      if (fallbackRet >= 0) {
        url_ = fallback;
        ret = fallbackRet;
        logLine("[KBORE][NETWORK][HTTPS] HTTP fallback opened stream successfully");
        break;
      }

      logLinef(
        "[KBORE][NETWORK][HTTPS] HTTP fallback failed error=%s",
        ffmpegError(fallbackRet).c_str()
      );
    }

    if (ret < 0) {
      ret = lastFallbackRet;
    }
  }

  if (ret < 0) {
    error_ = "NativeDemuxer could not open input: " + ffmpegError(ret);

    if (httpsProtocolMissing) {
      error_ +=
        " | The stream URL uses HTTPS, but this FFmpeg build does not expose the https/tls protocol. "
        "Rebuild FFmpeg for Switch with TLS/HTTPS support or use an HTTP-compatible playlist/URL.";
    }

    if (isProtocolNotFound(ret)) {
      error_ += unsupportedProtocolMessage(url);
    }

    close();
    return false;
  }

  installInterruptCallback(impl_);

  beginIoGuard(2500);
  ret = avformat_find_stream_info(impl_->format, nullptr);
  const bool streamInfoInterrupted = wasIoInterrupted();
  endIoGuard();

  if (ret < 0) {
    error_ = "NativeDemuxer could not read stream info: " + ffmpegError(ret);
    if (streamInfoInterrupted) {
      error_ += " | interrupted by IO guard";
    }
    close();
    return false;
  }

  for (unsigned int i = 0; i < impl_->format->nb_streams; ++i) {
    AVStream *stream = impl_->format->streams[i];

    if (!stream || !stream->codecpar) {
      continue;
    }

    AVCodecParameters *params = stream->codecpar;

    if (params->codec_type == AVMEDIA_TYPE_VIDEO && !video_.exists) {
      video_ = buildVideoInfo(static_cast<int>(i), params);
      continue;
    }

    if (params->codec_type == AVMEDIA_TYPE_AUDIO && !audio_.exists) {
      audio_ = buildAudioInfo(static_cast<int>(i), params);
      continue;
    }
  }

  if (!video_.exists && !audio_.exists) {
    error_ = "NativeDemuxer did not find video or audio streams.";
    close();
    return false;
  }

  if (!video_.exists && audio_.exists) {
    logLinef(
      "[KBORE][PLAYBACK][AUDIO_ONLY] demuxer opened audio-only stream codec=%s stream=%d url=%s",
      audio_.codecName.c_str(),
      audio_.index,
      url_.c_str()
    );
  }

  std::ostringstream out;

  if (video_.exists) {
    out
      << "video="
      << video_.codecName
      << " "
      << video_.width
      << "x"
      << video_.height
      << " stream="
      << video_.index;
  } else {
    out << "video=none";
  }

  if (audio_.exists) {
    out
      << " | audio="
      << audio_.codecName
      << " "
      << audio_.channels
      << "ch "
      << audio_.sampleRate
      << "Hz"
      << " stream="
      << audio_.index;
  } else {
    out << " | audio=none";
  }

  out << " | streams=" << impl_->format->nb_streams;

  summary_ = out.str();
  open_ = true;

  return true;
#endif
}

void NativeDemuxer::close() {
#ifdef NSTV_USE_FFMPEG
  endIoGuard();

  if (impl_ && impl_->format) {
    avformat_close_input(&impl_->format);
    impl_->format = nullptr;
  }
#endif

  open_ = false;
}

int64_t NativeDemuxer::durationMs() const {
#ifndef NSTV_USE_FFMPEG
  return 0;
#else
  return impl_ ? durationFromFormatMs(impl_->format) : 0;
#endif
}

int64_t NativeDemuxer::startTimeMs() const {
#ifndef NSTV_USE_FFMPEG
  return 0;
#else
  return impl_ ? startTimeFromFormatMs(impl_->format) : 0;
#endif
}

bool NativeDemuxer::canSeek() const {
#ifndef NSTV_USE_FFMPEG
  return false;
#else
  return impl_ ? formatCanSeek(impl_->format) : false;
#endif
}

void NativeDemuxer::beginIoGuard(int timeoutMs) {
#ifdef NSTV_USE_FFMPEG
  if (!impl_) {
    return;
  }

  impl_->ioInterrupted = false;

  if (timeoutMs <= 0) {
    impl_->ioDeadlineMs = 0;
    return;
  }

  impl_->ioDeadlineMs = steadyNowMs() + std::max(50, timeoutMs);

  if (impl_->format) {
    installInterruptCallback(impl_);
  }
#else
  (void)timeoutMs;
#endif
}

void NativeDemuxer::endIoGuard() {
#ifdef NSTV_USE_FFMPEG
  if (!impl_) {
    return;
  }

  impl_->ioDeadlineMs = 0;
#endif
}

bool NativeDemuxer::wasIoInterrupted() const {
#ifdef NSTV_USE_FFMPEG
  return impl_ && impl_->ioInterrupted;
#else
  return false;
#endif
}

bool NativeDemuxer::seekToMs(int64_t positionMs) {
#ifndef NSTV_USE_FFMPEG
  error_ = "NativeDemuxer seek requires NSTV_USE_FFMPEG.";
  return false;
#else
  if (!impl_ || !impl_->format || !open_) {
    error_ = "NativeDemuxer seek requires an open input.";
    return false;
  }

  if (positionMs < 0) {
    positionMs = 0;
  }

  const int64_t duration = durationMs();

  if (duration > 0 && positionMs > duration) {
    positionMs = duration;
  }

  const int64_t targetUs = av_rescale(positionMs + startTimeMs(), AV_TIME_BASE, 1000);

  beginIoGuard(1800);
  int ret = av_seek_frame(impl_->format, -1, targetUs, AVSEEK_FLAG_BACKWARD);

  if (ret < 0 && !wasIoInterrupted() && video_.exists) {
    AVStream *stream = impl_->format->streams[video_.index];

    if (stream) {
      AVRational microseconds{1, AV_TIME_BASE};
      const int64_t streamTs = av_rescale_q(targetUs, microseconds, stream->time_base);
      ret = av_seek_frame(impl_->format, video_.index, streamTs, AVSEEK_FLAG_BACKWARD);
    }
  }

  const bool interrupted = wasIoInterrupted();
  endIoGuard();

  if (ret < 0 || interrupted) {
    error_ = "NativeDemuxer could not seek: " + (interrupted ? std::string("interrupted by IO guard") : ffmpegError(ret));
    return false;
  }

  return true;
#endif
}

#ifdef NSTV_USE_FFMPEG
AVFormatContext *NativeDemuxer::formatContext() const {
  return impl_ ? impl_->format : nullptr;
}
#endif

} // namespace nstv