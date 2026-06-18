#include "nstv/config.hpp"
#include "nstv/json.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace nstv {

std::string toString(PlaybackSleepBehavior behavior) {
  switch (behavior) {
    case PlaybackSleepBehavior::SystemDefault: return "system_default";
    case PlaybackSleepBehavior::DockedOnly: return "docked_only";
    case PlaybackSleepBehavior::AlwaysPrevent: return "always_prevent";
  }

  return "docked_only";
}

PlaybackSleepBehavior playbackSleepBehaviorFromString(const std::string &value) {
  if (value == "system_default" || value == "system" || value == "default") {
    return PlaybackSleepBehavior::SystemDefault;
  }

  if (value == "always_prevent" || value == "always" || value == "prevent_always") {
    return PlaybackSleepBehavior::AlwaysPrevent;
  }

  return PlaybackSleepBehavior::DockedOnly;
}

namespace {

static std::string trimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') value.pop_back();
  return value;
}

#ifndef KBOR_PARSER_API_BASE_URL
#define KBOR_PARSER_API_BASE_URL "https://iptv-parser-kappa.vercel.app"
#endif

#ifndef KBOR_PARSER_API_KEY
#define KBOR_PARSER_API_KEY "19ede57ab7225fd3ceedcc93fa6c4460"
#endif

static std::string bundledParserApiBaseUrl() {
  return trimTrailingSlash(KBOR_PARSER_API_BASE_URL);
}

static std::string bundledParserApiKey() {
  return KBOR_PARSER_API_KEY;
}

static std::string readFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

static void ensureConfigDir() {
#ifdef __SWITCH__
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/kbore", 0777);
#else
  mkdir(".", 0777);
#endif
}

static std::string defaultNameForProvider(Provider provider) {
  switch (provider) {
    case Provider::M3u: return "M3U List";
    case Provider::Xtream: return "Xtream List";
    case Provider::Local: return "Local List";
  }

  return "Playlist";
}

static std::string safeIdPart(const std::string &value) {
  std::string out;

  for (char ch : value) {
    unsigned char c = static_cast<unsigned char>(ch);

    if (std::isalnum(c)) {
      out.push_back(static_cast<char>(std::tolower(c)));
      continue;
    }

    if (ch == '-' || ch == '_') {
      out.push_back(ch);
      continue;
    }

    if (!out.empty() && out.back() != '-') {
      out.push_back('-');
    }
  }

  while (!out.empty() && out.back() == '-') out.pop_back();

  if (out.empty()) {
    out = "playlist";
  }

  return out;
}


static std::string urlQueryEscape(const std::string &value) {
  std::ostringstream out;

  const char *hex = "0123456789ABCDEF";

  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out << static_cast<char>(c);
      continue;
    }

    out << '%';
    out << hex[(c >> 4) & 0x0F];
    out << hex[c & 0x0F];
  }

  return out.str();
}

static std::string makePlaylistId(const std::string &name, Provider provider, int index) {
  return safeIdPart(toString(provider) + "-" + name) + "-" + std::to_string(index + 1);
}

static PlaylistConfig playlistFromJson(const Json &json, int index) {
  PlaylistConfig playlist;

  playlist.provider = providerFromString(json["type"].asString(json["provider"].asString("local")));
  playlist.name = json["name"].asString(defaultNameForProvider(playlist.provider));
  playlist.id = json["id"].asString(makePlaylistId(playlist.name, playlist.provider, index));

  playlist.m3uUrl = json["m3u_url"].asString(json["m3uUrl"].asString(json["url"].asString("")));
  playlist.epgUrl = json["epg_url"].asString(json["epgUrl"].asString(""));
  playlist.serverUrl = trimTrailingSlash(json["server_url"].asString(json["serverUrl"].asString("")));
  playlist.username = json["username"].asString("");
  playlist.password = json["password"].asString("");
  playlist.epgOffsetMinutes = json["epg_offset_minutes"].asInt(json["epgOffsetMinutes"].asInt(0));

  // Compatibility: old xtream source stored as source/url.
  if (playlist.provider == Provider::Xtream && playlist.serverUrl.empty()) {
    playlist.serverUrl = trimTrailingSlash(json["source"].asString(json["url"].asString("")));
  }

  return playlist;
}

static Json playlistToJson(const PlaylistConfig &playlist, const std::string &activePlaylistId) {
  Json json(Json::object_t{});

  json["id"] = playlist.id;
  json["name"] = playlist.name;
  json["type"] = toString(playlist.provider);
  json["active"] = playlist.id == activePlaylistId;

  if (playlist.provider == Provider::M3u) {
    json["m3u_url"] = playlist.m3uUrl;
    if (!playlist.epgUrl.empty()) {
      json["epg_url"] = playlist.epgUrl;
    }
    if (playlist.epgOffsetMinutes != 0) {
      json["epg_offset_minutes"] = playlist.epgOffsetMinutes;
    }
  } else if (playlist.provider == Provider::Xtream) {
    json["server_url"] = playlist.serverUrl;
    json["username"] = playlist.username;
    json["password"] = playlist.password;
    if (playlist.epgOffsetMinutes != 0) {
      json["epg_offset_minutes"] = playlist.epgOffsetMinutes;
    }
  }

  return json;
}

} // namespace

std::string configPath() {
#ifdef __SWITCH__
  return "sdmc:/switch/kbore/config.json";
#else
  return "./config.json";
#endif
}

std::string PlaylistConfig::sourceUrl() const {
  if (provider == Provider::M3u) {
    return m3uUrl;
  }

  if (provider == Provider::Xtream) {
    if (!serverUrl.empty() && !username.empty() && !password.empty()) {
      return trimTrailingSlash(serverUrl) +
        "/player_api.php?username=" + urlQueryEscape(username) +
        "&password=" + urlQueryEscape(password);
    }

    // Allows compatibility with a full Xtream URL saved in serverUrl.
    return serverUrl;
  }

  return {};
}

const PlaylistConfig *Config::activePlaylist() const {
  if (!activePlaylistId.empty()) {
    for (const auto &playlist : playlists) {
      if (playlist.id == activePlaylistId) {
        return &playlist;
      }
    }
  }

  if (!playlists.empty()) {
    return &playlists.front();
  }

  return nullptr;
}

Config loadConfig() {
  auto parseConfigText = [](const std::string &text) {
    Config cfg;

    if (!text.empty()) {
      try {
        Json json = Json::parse(text);

        cfg.parserApiBaseUrl = trimTrailingSlash(json["parserApiBaseUrl"].asString(json["baseUrl"].asString("")));
        cfg.apiKey = json["apiKey"].asString("");

        cfg.defaultPlaylistUrl = json["defaultPlaylistUrl"].asString("");
        cfg.defaultXtreamUrl = json["defaultXtreamUrl"].asString("");

        cfg.activePlaylistId = json["active"].asString(json["active_playlist_id"].asString(json["activePlaylistId"].asString("")));

        cfg.pageSize = json["pageSize"].asInt(20);
        cfg.preloadThreshold = json["preloadThreshold"].asInt(8);
        cfg.useUnicodeIcons = json["useUnicodeIcons"].asBool(false);
        cfg.playbackSleepBehavior = playbackSleepBehaviorFromString(
          json["playback_sleep_behavior"].asString(json["playbackSleepBehavior"].asString("docked_only"))
        );
        cfg.dockedSleepTimerMinutes = json["docked_sleep_timer_minutes"].asInt(json["dockedSleepTimerMinutes"].asInt(0));
        cfg.batterySleepTimeoutMinutes = json["battery_sleep_timeout_minutes"].asInt(json["batterySleepTimeoutMinutes"].asInt(10));
        cfg.sleepWarningSeconds = json["sleep_warning_seconds"].asInt(json["sleepWarningSeconds"].asInt(60));

        if (json["playlists"].isArray()) {
          int index = 0;

          for (const Json &item : json["playlists"].asArray()) {
            PlaylistConfig playlist = playlistFromJson(item, index++);
            const bool itemActive = item["active"].asBool(false);

            if (!playlist.sourceUrl().empty()) {
              if (itemActive) {
                cfg.activePlaylistId = playlist.id;
              }

              cfg.playlists.push_back(playlist);
            }
          }
        }
      } catch (...) {
        // Fall through to defaults/legacy migration.
      }
    }

    // Legacy compatibility: if the user already had config.json with default
    // values, migrate them into saved playlists in memory. saveConfig() will
    // persist the new format after the first list operation.
    if (cfg.playlists.empty()) {
      if (!cfg.defaultPlaylistUrl.empty()) {
        PlaylistConfig playlist;
        playlist.id = "legacy-m3u";
        playlist.name = "M3U";
        playlist.provider = Provider::M3u;
        playlist.m3uUrl = cfg.defaultPlaylistUrl;
        cfg.playlists.push_back(playlist);
      }

      if (!cfg.defaultXtreamUrl.empty()) {
        PlaylistConfig playlist;
        playlist.id = "legacy-xtream";
        playlist.name = "Xtream";
        playlist.provider = Provider::Xtream;
        playlist.serverUrl = cfg.defaultXtreamUrl;
        cfg.playlists.push_back(playlist);
      }
    }

    if (cfg.activePlaylistId.empty() && !cfg.playlists.empty()) {
      cfg.activePlaylistId = cfg.playlists.front().id;
    }

    if (cfg.pageSize < 1) cfg.pageSize = 20;
    if (cfg.pageSize > 1000) cfg.pageSize = 1000;
    if (cfg.preloadThreshold < 1) cfg.preloadThreshold = 1;
    if (cfg.preloadThreshold > 50) cfg.preloadThreshold = 50;
    if (cfg.dockedSleepTimerMinutes < 0) cfg.dockedSleepTimerMinutes = 0;
    if (cfg.dockedSleepTimerMinutes > 240) cfg.dockedSleepTimerMinutes = 240;
    if (cfg.batterySleepTimeoutMinutes < 1) cfg.batterySleepTimeoutMinutes = 10;
    if (cfg.batterySleepTimeoutMinutes > 120) cfg.batterySleepTimeoutMinutes = 120;
    if (cfg.sleepWarningSeconds < 10) cfg.sleepWarningSeconds = 10;
    if (cfg.sleepWarningSeconds > 300) cfg.sleepWarningSeconds = 300;

    return cfg;
  };

  Config cfg = parseConfigText(readFile(configPath()));

#ifdef __SWITCH__
  /*
    One-time compatibility bridge.

    Older NSTV builds used:
      sdmc:/switch/nstv/config.json

    Kboré now uses:
      sdmc:/switch/kbore/config.json

    If the new config already exists but is missing parserApiBaseUrl/apiKey, keep
    the user's old parser settings. Without this, newly added playlists can be
    saved correctly but fail to load because the parser API endpoint is empty.
  */
  Config legacy = parseConfigText(readFile("sdmc:/switch/nstv/config.json"));

  if (cfg.parserApiBaseUrl.empty() && !legacy.parserApiBaseUrl.empty()) {
    cfg.parserApiBaseUrl = legacy.parserApiBaseUrl;
  }

  if (cfg.apiKey.empty() && !legacy.apiKey.empty()) {
    cfg.apiKey = legacy.apiKey;
  }

  if (cfg.defaultPlaylistUrl.empty() && !legacy.defaultPlaylistUrl.empty()) {
    cfg.defaultPlaylistUrl = legacy.defaultPlaylistUrl;
  }

  if (cfg.defaultXtreamUrl.empty() && !legacy.defaultXtreamUrl.empty()) {
    cfg.defaultXtreamUrl = legacy.defaultXtreamUrl;
  }

  if (cfg.playlists.empty() && !legacy.playlists.empty()) {
    cfg.playlists = legacy.playlists;
    cfg.activePlaylistId = legacy.activePlaylistId;
  }

  if (cfg.playbackSleepBehavior == PlaybackSleepBehavior::DockedOnly &&
      legacy.playbackSleepBehavior != PlaybackSleepBehavior::DockedOnly) {
    cfg.playbackSleepBehavior = legacy.playbackSleepBehavior;
  }
  if (cfg.dockedSleepTimerMinutes == 0 && legacy.dockedSleepTimerMinutes != 0) {
    cfg.dockedSleepTimerMinutes = legacy.dockedSleepTimerMinutes;
  }
  if (cfg.batterySleepTimeoutMinutes == 10 && legacy.batterySleepTimeoutMinutes != 10) {
    cfg.batterySleepTimeoutMinutes = legacy.batterySleepTimeoutMinutes;
  }
  if (cfg.sleepWarningSeconds == 60 && legacy.sleepWarningSeconds != 60) {
    cfg.sleepWarningSeconds = legacy.sleepWarningSeconds;
  }
#endif

  if (cfg.parserApiBaseUrl.empty()) {
    cfg.parserApiBaseUrl = bundledParserApiBaseUrl();
  }

  if (cfg.apiKey.empty()) {
    cfg.apiKey = bundledParserApiKey();
  }

  if (cfg.activePlaylistId.empty() && !cfg.playlists.empty()) {
    cfg.activePlaylistId = cfg.playlists.front().id;
  }

  return cfg;
}

bool saveConfig(const Config &config) {
  ensureConfigDir();

  Json json(Json::object_t{});

  // Parser API settings are bundled in the app and intentionally not saved
  // to config.json. Users should only manage playlist sources here.
  json["pageSize"] = config.pageSize;
  json["preloadThreshold"] = config.preloadThreshold;
  json["useUnicodeIcons"] = config.useUnicodeIcons;
  json["playback_sleep_behavior"] = toString(config.playbackSleepBehavior);
  json["docked_sleep_timer_minutes"] = config.dockedSleepTimerMinutes;
  json["battery_sleep_timeout_minutes"] = config.batterySleepTimeoutMinutes;
  json["sleep_warning_seconds"] = config.sleepWarningSeconds;
  json["active"] = config.activePlaylistId;
  json["active_playlist_id"] = config.activePlaylistId;

  // Keep legacy fields for compatibility with older builds/tools.
  json["defaultPlaylistUrl"] = config.defaultPlaylistUrl;
  json["defaultXtreamUrl"] = config.defaultXtreamUrl;

  Json::array_t playlists;

  for (const auto &playlist : config.playlists) {
    playlists.push_back(playlistToJson(playlist, config.activePlaylistId));
  }

  json["playlists"] = Json(playlists);

  std::ofstream file(configPath(), std::ios::binary);

  if (!file) {
    return false;
  }

  file << json.stringify();
  return true;
}

} // namespace nstv
