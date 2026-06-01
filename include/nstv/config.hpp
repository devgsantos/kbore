#pragma once

#include "nstv/models.hpp"

#include <string>
#include <vector>

namespace nstv {

struct PlaylistConfig {
  std::string id;
  std::string name;
  Provider provider = Provider::Local;

  // M3U source.
  std::string m3uUrl;
  std::string epgUrl;

  // Xtream source.
  std::string serverUrl;
  std::string username;
  std::string password;

  std::string sourceUrl() const;
};

struct Config {
  std::string parserApiBaseUrl;
  std::string apiKey;

  // Legacy fields. They are still read for compatibility, but new playlists
  // should be saved in playlists/activePlaylistId.
  std::string defaultPlaylistUrl;
  std::string defaultXtreamUrl;

  std::string activePlaylistId;
  std::vector<PlaylistConfig> playlists;

  int pageSize = 20;
  int preloadThreshold = 8;
  bool useUnicodeIcons = false;

  const PlaylistConfig *activePlaylist() const;
};

Config loadConfig();
bool saveConfig(const Config &config);
std::string configPath();

} // namespace nstv
