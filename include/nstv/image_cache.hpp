#pragma once

#include "nstv/graphics.hpp"
#include <atomic>
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

private:
  enum class Status { Missing, Loading, Loaded, Failed };

  struct Entry {
    Status status = Status::Missing;
    Bitmap bitmap;
  };

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::map<std::string, Entry> entries_;
  std::vector<std::string> queue_;
  std::set<std::string> queued_;
  std::thread worker_;
  std::atomic<bool> stop_{false};

  void ensureWorkerLocked();
  void workerLoop();

  bool download(const std::string &url, std::vector<uint8_t> &data);
  bool decodeSdlImage(const std::vector<uint8_t> &data, Bitmap &bitmap);
  bool decodePng(const std::vector<uint8_t> &data, Bitmap &bitmap);
  bool decodePpm(const std::vector<uint8_t> &data, Bitmap &bitmap);
};

} // namespace nstv
