#pragma once

#include <string>

namespace nstv {

struct Config {
  std::string parserApiBaseUrl;
  std::string apiKey;
  std::string defaultPlaylistUrl;
  std::string defaultXtreamUrl;
  int pageSize = 20;
  int preloadThreshold = 8;
  bool useUnicodeIcons = false;
};

Config loadConfig();
std::string configPath();

} // namespace nstv
