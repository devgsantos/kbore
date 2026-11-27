#pragma once

#include "nstv/graphics.hpp"
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace nstv {

class ImageCache {
public:
  ImageCache() = default;
  ~ImageCache();

  // Synchronous fetch kept for callers that explicitly want blocking behavior.
  const Bitmap *get(const std::string &url);

  // Non-blocking lazy loading helpers. request() queues the URL and returns
  // immediately; peek() returns the decoded bitmap only when it is already
  // available in memory.
  void request(const std::string &url);
  const Bitmap *peek(const std::string &url) const;
  bool isPending(const std::string &url) const;

  void clear();

  // Drops the least recently drawn decoded images above maxEntries. Call from the UI thread only
  // (e.g. once per frame before drawing): pointers returned by peek() stay valid until then.
  void trim(std::size_t maxEntries);

  // While paused, workers finish their current download but start no new one. Used while a stream
  // connects so logo downloads do not compete with it for the Switch's few network sessions.
  void setPaused(bool paused);

private:
  enum class Status { Missing, Loading, Loaded, Failed };

  struct Entry {
    Status status = Status::Missing;
    Bitmap bitmap;
    mutable uint64_t lastAccess = 0;
  };

  static constexpr int kWorkerCount = 2;
  // Pending downloads beyond this are dropped (oldest first): they belong to rows already
  // scrolled away and will be requested again if they come back on screen.
  static constexpr std::size_t kMaxQueued = 48;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::map<std::string, Entry> entries_;
  std::vector<std::string> queue_;
  std::set<std::string> queued_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stop_{false};
  bool paused_ = false;  // guarded by mutex_
  mutable uint64_t accessCounter_ = 0;

  void ensureWorkerLocked();
  void workerLoop();
  static void finalizeBitmap(Bitmap &bitmap);

  bool download(const std::string &url, std::vector<uint8_t> &data);
  bool decodeSdlImage(const std::vector<uint8_t> &data, Bitmap &bitmap);
  bool decodePng(const std::vector<uint8_t> &data, Bitmap &bitmap);
  bool decodePpm(const std::vector<uint8_t> &data, Bitmap &bitmap);
};

} // namespace nstv
