#pragma once

#include "nstv/models.hpp"

#include <string>
#include <map>
#include <vector>

namespace nstv {

enum class PlaybackSleepBehavior {
  SystemDefault,
  DockedOnly,
  AlwaysPrevent
};

enum class ParentalRule {
  None,
  Hidden,
  Locked
};

std::string toString(ParentalRule rule);
ParentalRule parentalRuleFromString(const std::string &value);

std::string toString(PlaybackSleepBehavior behavior);
PlaybackSleepBehavior playbackSleepBehaviorFromString(const std::string &value);

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

  // Manual EPG time correction for providers whose EPG data is shifted.
  // Applied in minutes to programme start/stop timestamps before matching/display.
  // Example: if programmes appear 7 hours late, use -420.
  int epgOffsetMinutes = 0;

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

  // Playback sleep policy. Dashboard always follows the system.
  // DockedOnly is the recommended default: prevent sleep during docked playback,
  // but respect system sleep in handheld/battery mode.
  PlaybackSleepBehavior playbackSleepBehavior = PlaybackSleepBehavior::DockedOnly;
  int dockedSleepTimerMinutes = 0;      // 0 = Off. Optional timer for TV/docked playback.
  int batterySleepTimeoutMinutes = 10;  // App-side estimate for warning overlay only.
  int sleepWarningSeconds = 60;         // Warning lead time for battery/timer overlays.

  int manifestRefreshHours = 24;        // 0 = always use cache until manual reload.
  int epgRefreshHours = 12;             // 0 = cache-only after first successful fetch.
  std::string uiLanguage = "en";

  std::string parentalPin = "0000";
  std::map<std::string, ParentalRule> parentalRules;

  const PlaylistConfig *activePlaylist() const;
};

Config loadConfig();
bool saveConfig(const Config &config);
std::string configPath();

} // namespace nstv
