#include "nstv/config.hpp"
#include "nstv/json.hpp"
#include <fstream>
#include <sstream>

namespace nstv {

std::string configPath() {
#ifdef __SWITCH__
  return "sdmc:/switch/nstv/config.json";
#else
  return "./config.json";
#endif
}

static std::string readFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

static std::string trimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') value.pop_back();
  return value;
}

Config loadConfig() {
  Config cfg;
  std::string text = readFile(configPath());
  if (!text.empty()) {
    try {
      Json json = Json::parse(text);
      cfg.parserApiBaseUrl = trimTrailingSlash(json["parserApiBaseUrl"].asString(json["baseUrl"].asString("")));
      cfg.apiKey = json["apiKey"].asString("");
      cfg.defaultPlaylistUrl = json["defaultPlaylistUrl"].asString("");
      cfg.defaultXtreamUrl = json["defaultXtreamUrl"].asString("");
      cfg.pageSize = json["pageSize"].asInt(20);
      cfg.preloadThreshold = json["preloadThreshold"].asInt(8);
      cfg.useUnicodeIcons = json["useUnicodeIcons"].asBool(false);
    } catch (...) {
      // Fall through to defaults.
    }
  }

  // Empty defaults keep the app functional and show a clear UI message.
  if (cfg.pageSize < 1) cfg.pageSize = 20;
  if (cfg.pageSize > 1000) cfg.pageSize = 1000;
  if (cfg.preloadThreshold < 1) cfg.preloadThreshold = 1;
  if (cfg.preloadThreshold > 50) cfg.preloadThreshold = 50;
  return cfg;
}

} // namespace nstv
