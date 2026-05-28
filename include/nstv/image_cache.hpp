#pragma once

#include "nstv/graphics.hpp"
#include <map>
#include <string>

namespace nstv {

class ImageCache {
public:
  const Bitmap *get(const std::string &url);
  void clear();

private:
  enum class Status { Missing, Loaded, Failed };

  struct Entry {
    Status status = Status::Missing;
    Bitmap bitmap;
  };

  std::map<std::string, Entry> entries_;

  bool download(const std::string &url, std::vector<uint8_t> &data);
  bool decodePng(const std::vector<uint8_t> &data, Bitmap &bitmap);
  bool decodePpm(const std::vector<uint8_t> &data, Bitmap &bitmap);
};

} // namespace nstv
