#ifdef __SWITCH__
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include "nstv/image_cache.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <curl/curl.h>
#include <zlib.h>
#ifdef NSTV_USE_SDL_IMAGE
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#endif

namespace nstv {
namespace {

static size_t writeBytes(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *data = static_cast<std::vector<uint8_t> *>(userdata);
  std::size_t total = size * nmemb;

  // Avoid downloading huge images by mistake.
  if (data->size() + total > 2 * 1024 * 1024) {
    return 0;
  }

  data->insert(data->end(), reinterpret_cast<uint8_t *>(ptr), reinterpret_cast<uint8_t *>(ptr) + total);
  return total;
}

uint32_t readBE32(const std::vector<uint8_t> &data, std::size_t offset) {
  if (offset + 4 > data.size()) return 0;
  return (uint32_t(data[offset]) << 24) |
         (uint32_t(data[offset + 1]) << 16) |
         (uint32_t(data[offset + 2]) << 8) |
         uint32_t(data[offset + 3]);
}

int paeth(int a, int b, int c) {
  int p = a + b - c;
  int pa = std::abs(p - a);
  int pb = std::abs(p - b);
  int pc = std::abs(p - c);

  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

} // namespace


ImageCache::~ImageCache() {
  stop_ = true;
  cv_.notify_all();

  if (worker_.joinable()) {
    worker_.join();
  }
}

void ImageCache::ensureWorkerLocked() {
  if (!worker_.joinable()) {
    stop_ = false;
    worker_ = std::thread(&ImageCache::workerLoop, this);
  }
}

void ImageCache::workerLoop() {
  while (true) {
    std::string url;

    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() {
        return stop_ || !queue_.empty();
      });

      if (stop_ && queue_.empty()) {
        break;
      }

      url = queue_.front();
      queue_.erase(queue_.begin());
      queued_.erase(url);
    }

    Entry entry;
    std::vector<uint8_t> bytes;

    if (download(url, bytes) &&
        (decodeSdlImage(bytes, entry.bitmap) || decodePng(bytes, entry.bitmap) || decodePpm(bytes, entry.bitmap))) {
      entry.status = Status::Loaded;
    } else {
      entry.status = Status::Failed;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      entries_[url] = std::move(entry);
    }
  }
}

const Bitmap *ImageCache::peek(const std::string &url) const {
  if (url.empty()) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto found = entries_.find(url);

  if (found == entries_.end() || found->second.status != Status::Loaded) {
    return nullptr;
  }

  return &found->second.bitmap;
}

bool ImageCache::isPending(const std::string &url) const {
  if (url.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto found = entries_.find(url);
  return found != entries_.end() && found->second.status == Status::Loading;
}

void ImageCache::request(const std::string &url) {
  if (url.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(url);

    if (found != entries_.end() && found->second.status != Status::Missing) {
      return;
    }

    if (queued_.count(url)) {
      return;
    }

    entries_[url].status = Status::Loading;
    queue_.push_back(url);
    queued_.insert(url);
    ensureWorkerLocked();
  }

  cv_.notify_one();
}

const Bitmap *ImageCache::get(const std::string &url) {
  if (url.empty()) {
    return nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(url);

    if (found != entries_.end()) {
      return found->second.status == Status::Loaded ? &found->second.bitmap : nullptr;
    }
  }

  Entry entry;
  std::vector<uint8_t> bytes;

  if (download(url, bytes) &&
      (decodeSdlImage(bytes, entry.bitmap) || decodePng(bytes, entry.bitmap) || decodePpm(bytes, entry.bitmap))) {
    entry.status = Status::Loaded;
  } else {
    entry.status = Status::Failed;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto [it, _] = entries_.emplace(url, std::move(entry));

  return it->second.status == Status::Loaded ? &it->second.bitmap : nullptr;
}

void ImageCache::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  queue_.clear();
  queued_.clear();
}

bool ImageCache::download(const std::string &url, std::vector<uint8_t> &data) {
  CURL *curl = curl_easy_init();

  if (!curl) {
    return false;
  }

  data.clear();

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Accept: image/png,image/*,*/*");
  headers = curl_slist_append(headers, "User-Agent: NSTV-Switch/0.2");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBytes);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);

  CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return code == CURLE_OK && status >= 200 && status < 300 && !data.empty();
}



bool ImageCache::decodeSdlImage(const std::vector<uint8_t> &data, Bitmap &bitmap) {
#ifndef NSTV_USE_SDL_IMAGE
  (void)data;
  (void)bitmap;
  return false;
#else
  if (data.empty()) return false;

  static bool initialized = false;
  if (!initialized) {
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP);
    initialized = true;
  }

  SDL_RWops *rw = SDL_RWFromConstMem(data.data(), static_cast<int>(data.size()));
  if (!rw) return false;

  SDL_Surface *loaded = IMG_Load_RW(rw, 1);
  if (!loaded) return false;

  SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(loaded);
  if (!rgba) return false;

  bitmap.width = rgba->w;
  bitmap.height = rgba->h;
  bitmap.rgba.resize(static_cast<std::size_t>(bitmap.width * bitmap.height * 4));

  const uint8_t *src = static_cast<const uint8_t *>(rgba->pixels);
  for (int y = 0; y < bitmap.height; ++y) {
    std::memcpy(
      bitmap.rgba.data() + static_cast<std::size_t>(y * bitmap.width * 4),
      src + static_cast<std::size_t>(y * rgba->pitch),
      static_cast<std::size_t>(bitmap.width * 4)
    );
  }

  SDL_FreeSurface(rgba);
  return true;
#endif
}

bool ImageCache::decodePpm(const std::vector<uint8_t> &data, Bitmap &bitmap) {
  if (data.size() < 3 || data[0] != 'P' || data[1] != '6') {
    return false;
  }

  std::size_t i = 2;

  auto skipSpaceAndComments = [&]() {
    while (i < data.size()) {
      if (data[i] == '#') {
        while (i < data.size() && data[i] != '\n') ++i;
        continue;
      }

      if (data[i] == ' ' || data[i] == '\n' || data[i] == '\r' || data[i] == '\t') {
        ++i;
        continue;
      }

      break;
    }
  };

  auto readInt = [&]() -> int {
    skipSpaceAndComments();

    int value = 0;

    while (i < data.size() && data[i] >= '0' && data[i] <= '9') {
      value = value * 10 + (data[i] - '0');
      ++i;
    }

    return value;
  };

  int width = readInt();
  int height = readInt();
  int maxValue = readInt();

  if (i < data.size() && (data[i] == '\n' || data[i] == '\r' || data[i] == ' ' || data[i] == '\t')) {
    ++i;
  }

  if (width <= 0 || height <= 0 || maxValue <= 0 || maxValue > 255) {
    return false;
  }

  std::size_t expected = static_cast<std::size_t>(width * height * 3);

  if (i + expected > data.size()) {
    return false;
  }

  bitmap.width = width;
  bitmap.height = height;
  bitmap.rgba.resize(static_cast<std::size_t>(width * height * 4));

  for (int p = 0; p < width * height; ++p) {
    bitmap.rgba[p * 4 + 0] = data[i + p * 3 + 0];
    bitmap.rgba[p * 4 + 1] = data[i + p * 3 + 1];
    bitmap.rgba[p * 4 + 2] = data[i + p * 3 + 2];
    bitmap.rgba[p * 4 + 3] = 255;
  }

  return true;
}

bool ImageCache::decodePng(const std::vector<uint8_t> &data, Bitmap &bitmap) {
  static const uint8_t signature[8] = {137,80,78,71,13,10,26,10};

  if (data.size() < 24 || std::memcmp(data.data(), signature, 8) != 0) {
    return false;
  }

  int width = 0;
  int height = 0;
  int bitDepth = 0;
  int colorType = 0;
  int interlace = 0;
  std::vector<uint8_t> idat;

  std::size_t offset = 8;

  while (offset + 8 <= data.size()) {
    uint32_t length = readBE32(data, offset);
    offset += 4;

    if (offset + 4 > data.size()) return false;

    std::string type(reinterpret_cast<const char *>(&data[offset]), 4);
    offset += 4;

    if (offset + length + 4 > data.size()) return false;

    if (type == "IHDR") {
      width = static_cast<int>(readBE32(data, offset));
      height = static_cast<int>(readBE32(data, offset + 4));
      bitDepth = data[offset + 8];
      colorType = data[offset + 9];
      interlace = data[offset + 12];
    } else if (type == "IDAT") {
      idat.insert(idat.end(), data.begin() + static_cast<long>(offset), data.begin() + static_cast<long>(offset + length));
    } else if (type == "IEND") {
      break;
    }

    offset += length + 4; // data + CRC
  }

  if (width <= 0 || height <= 0 || bitDepth != 8 || interlace != 0 || idat.empty()) {
    return false;
  }

  int channels = 0;

  if (colorType == 6) channels = 4;      // RGBA
  else if (colorType == 2) channels = 3; // RGB
  else if (colorType == 0) channels = 1; // Grayscale
  else return false;                     // Indexed/grayscale-alpha not supported in the lightweight decoder.

  const std::size_t stride = static_cast<std::size_t>(width * channels);
  const std::size_t expected = static_cast<std::size_t>((stride + 1) * height);
  std::vector<uint8_t> inflated(expected);

  uLongf outSize = static_cast<uLongf>(inflated.size());
  int zres = uncompress(inflated.data(), &outSize, idat.data(), static_cast<uLong>(idat.size()));

  if (zres != Z_OK || outSize < expected) {
    return false;
  }

  std::vector<uint8_t> raw(static_cast<std::size_t>(height) * stride);
  std::vector<uint8_t> previous(stride, 0);

  std::size_t inOffset = 0;

  for (int y = 0; y < height; ++y) {
    uint8_t filter = inflated[inOffset++];

    for (std::size_t x = 0; x < stride; ++x) {
      uint8_t value = inflated[inOffset++];
      int left = x >= static_cast<std::size_t>(channels) ? raw[static_cast<std::size_t>(y) * stride + x - channels] : 0;
      int up = previous[x];
      int upLeft = x >= static_cast<std::size_t>(channels) ? previous[x - channels] : 0;

      int recon = 0;

      switch (filter) {
        case 0: recon = value; break;
        case 1: recon = value + left; break;
        case 2: recon = value + up; break;
        case 3: recon = value + ((left + up) / 2); break;
        case 4: recon = value + paeth(left, up, upLeft); break;
        default: return false;
      }

      raw[static_cast<std::size_t>(y) * stride + x] = static_cast<uint8_t>(recon & 0xff);
    }

    std::copy(
      raw.begin() + static_cast<long>(static_cast<std::size_t>(y) * stride),
      raw.begin() + static_cast<long>((static_cast<std::size_t>(y) + 1) * stride),
      previous.begin()
    );
  }

  bitmap.width = width;
  bitmap.height = height;
  bitmap.rgba.resize(static_cast<std::size_t>(width * height * 4));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      std::size_t src = static_cast<std::size_t>(y * width + x) * channels;
      std::size_t dst = static_cast<std::size_t>(y * width + x) * 4;

      if (colorType == 6) {
        bitmap.rgba[dst + 0] = raw[src + 0];
        bitmap.rgba[dst + 1] = raw[src + 1];
        bitmap.rgba[dst + 2] = raw[src + 2];
        bitmap.rgba[dst + 3] = raw[src + 3];
      } else if (colorType == 2) {
        bitmap.rgba[dst + 0] = raw[src + 0];
        bitmap.rgba[dst + 1] = raw[src + 1];
        bitmap.rgba[dst + 2] = raw[src + 2];
        bitmap.rgba[dst + 3] = 255;
      } else {
        uint8_t gray = raw[src];
        bitmap.rgba[dst + 0] = gray;
        bitmap.rgba[dst + 1] = gray;
        bitmap.rgba[dst + 2] = gray;
        bitmap.rgba[dst + 3] = 255;
      }
    }
  }

  return true;
}

} // namespace nstv
