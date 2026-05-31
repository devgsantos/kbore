#include "nstv/app.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

namespace nstv {

namespace {
const char *ANSI_RESET = "\x1b[0m";
const char *ANSI_BOLD = "\x1b[1m";
const char *FG_WHITE = "\x1b[38;5;255m";
const char *FG_MUTED = "\x1b[38;5;146m";
const char *FG_BLUE = "\x1b[38;5;39m";
const char *FG_GREEN = "\x1b[38;5;46m";
const char *FG_YELLOW = "\x1b[38;5;220m";
const char *BG_APP = "\x1b[48;5;16m";
const char *BG_PANEL = "\x1b[48;5;17m";
const char *BG_PANEL_DARK = "\x1b[48;5;234m";
const char *BG_SELECTED = "\x1b[48;5;27m";
const char *BG_CARD = "\x1b[48;5;235m";

std::string repeat(char ch, int count) {
  if (count <= 0) return {};
  return std::string(static_cast<std::size_t>(count), ch);
}

long long nowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    clock::now().time_since_epoch()
  ).count();
}

std::string formatSystemClockTime() {
  std::time_t rawTime = std::time(nullptr);

  if (rawTime <= 0) {
    return "--:--";
  }

  std::tm localTime{};

#if defined(_WIN32)
  localtime_s(&localTime, &rawTime);
#else
  std::tm *result = std::localtime(&rawTime);

  if (!result) {
    return "--:--";
  }

  localTime = *result;
#endif

  char buffer[8] = {};
  std::snprintf(
    buffer,
    sizeof(buffer),
    "%02d:%02d",
    localTime.tm_hour,
    localTime.tm_min
  );

  return buffer;
}

std::vector<std::string> wrapText(const std::string &text, std::size_t maxCharsPerLine) {
  std::vector<std::string> lines;

  if (text.empty()) {
    return lines;
  }

  std::string current;
  std::string word;

  auto flushWord = [&]() {
    if (word.empty()) {
      return;
    }

    if (word.size() > maxCharsPerLine) {
      if (!current.empty()) {
        lines.push_back(current);
        current.clear();
      }

      for (std::size_t i = 0; i < word.size(); i += maxCharsPerLine) {
        lines.push_back(word.substr(i, maxCharsPerLine));
      }

      word.clear();
      return;
    }

    if (current.empty()) {
      current = word;
    } else if (current.size() + 1 + word.size() <= maxCharsPerLine) {
      current += " ";
      current += word;
    } else {
      lines.push_back(current);
      current = word;
    }

    word.clear();
  };

  for (char ch : text) {
    if (ch == ' ' || ch == '\n' || ch == '\t') {
      flushWord();

      if (ch == '\n' && !current.empty()) {
        lines.push_back(current);
        current.clear();
      }

      continue;
    }

    word.push_back(ch);
  }

  flushWord();

  if (!current.empty()) {
    lines.push_back(current);
  }

  return lines;
}

void drawWrappedText(
  Graphics &gfx,
  const std::string &text,
  int x,
  int y,
  int maxLines,
  std::size_t maxCharsPerLine,
  int scale,
  Color color,
  bool bold = false
) {
  const std::vector<std::string> lines = wrapText(text, maxCharsPerLine);

  const int lineHeight = scale <= 1 ? 14 : 18;

  for (int i = 0; i < maxLines && i < static_cast<int>(lines.size()); ++i) {
    std::string line = lines[static_cast<std::size_t>(i)];

    if (i == maxLines - 1 && static_cast<int>(lines.size()) > maxLines) {
      if (line.size() > 3) {
        line = line.substr(0, line.size() - 3) + "...";
      } else {
        line += "...";
      }
    }

    gfx.drawText(line, x, y + i * lineHeight, scale, color, bold);
  }
}

std::string safePlaylistId(const std::string &name, Provider provider) {
  std::string out = toString(provider);

  for (char ch : name) {
    unsigned char c = static_cast<unsigned char>(ch);

    if (std::isalnum(c)) {
      out.push_back('-');
      out.push_back(static_cast<char>(std::tolower(c)));
    } else if ((ch == '-' || ch == '_') && !out.empty() && out.back() != '-') {
      out.push_back('-');
    }
  }

  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }

  return out + "-" + std::to_string(nowMs());
}

std::string trimText(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }

  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }

  return value;
}

std::string trimTrailingSlashLocal(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }

  return value;
}

}

App::App() : api_(loadConfig()), player_(createPlayerBackend()) {
  splashStartedAtMs_ = nowMs();
  splashVisible_ = true;

  state_.config = loadConfig();
  api_ = ParserApiClient(state_.config);

  const PlaylistConfig *playlist = activePlaylist();

  if (playlist) {
    bool usedCache = false;

    if (loadManifestForPlaylist(state_.manifest, playlist->id)) {
      const bool cachedMatchesActive =
        state_.manifest.id == playlist->id &&
        (!state_.manifest.nodes.empty() || !state_.manifest.types.empty());

      if (cachedMatchesActive) {
        state_.hasManifest = true;
        usedCache = true;
        normalizeIndexes();

        saveManifest(state_.manifest);

        state_.message =
          "Loaded cached manifest: " +
          std::to_string(state_.manifest.totalChannels > 0 ? state_.manifest.totalChannels : state_.manifest.totalItems) +
          " items";
      }
    }

    if (!usedCache) {
      startPlaylistLoad(*playlist);
    }

    if (!loadCachedPlaylist(*playlist)) {
      importPlaylist(*playlist);
    }
  } else if (loadManifest(state_.manifest) && (!state_.manifest.nodes.empty() || !state_.manifest.types.empty())) {
    state_.hasManifest = true;
    state_.message =
      "Loaded cached manifest: " +
      std::to_string(state_.manifest.totalChannels) +
      " channels";
  } else {
    state_.message = "Press + to add an M3U or Xtream playlist";
  }
}


App::~App() {
  if (playlistLoadThread_.joinable()) {
    playlistLoadThread_.join();
  }

  if (cacheSaveThread_.joinable()) {
    cacheSaveThread_.join();
  }
}

int App::run() {
  render();

  while (state_.running) {
    updatePlaylistLoad();
    updateCacheSave();

    if (splashVisible_) {
      Button button = pollButton();

      if (button == Button::Quit) {
        state_.running = false;
        break;
      }

      render();
      sleepMs(16);
      continue;
    }

    if (playlistLoadActive()) {
      Button button = pollButton();

      if (button == Button::Quit) {
        state_.running = false;
        break;
      }

      render();
      sleepMs(16);
      continue;
    }

    if (state_.screen == ScreenId::Player) {
      Button button = pollButton();

      if (button != Button::None) {
        handle(button);
      }

      render();
      sleepMs(16);
      continue;
    }

    Button button = pollButtonBlocking();
    handle(button);
    updatePlaylistLoad();
    updateCacheSave();
    render();
  }

  return 0;
}

void App::handle(Button button) {
  if (button == Button::Quit) {
    state_.running = false;
    return;
  }

  switch (state_.screen) {
    case ScreenId::Dashboard: handleDashboard(button); break;
    case ScreenId::AddPlaylist: handleAddPlaylist(button); break;
    case ScreenId::Player:
      if (button != Button::None) {
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }

      if (button == Button::Back) {
        if (player_) {
          player_->close();
        }

        state_.screen = ScreenId::Dashboard;
        state_.message = "Playback stopped";
        state_.playerStarted = false;
        state_.playerFrameSeen = false;
        state_.playerLoading = false;
        state_.playerLoadFailed = false;
        state_.playerErrorMessage.clear();
      } else if (button == Button::Select) {
        if (player_) {
          player_->togglePause();
          state_.message = player_->isPaused() ? "Playback paused" : "Playback resumed";
        }
      }
      break;
    case ScreenId::Settings:
      if (button == Button::Back || button == Button::Select) state_.screen = ScreenId::Dashboard;
      break;
    case ScreenId::Playlists:
      state_.screen = ScreenId::AddPlaylist;
      break;
  }
  normalizeIndexes();
}

void App::handleDashboard(Button button) {
  if (button == Button::Menu) {
    state_.screen = ScreenId::AddPlaylist;
    return;
  }

  if (button == Button::Back && !usingNodeTree()) {
    if (state_.focus == FocusColumn::Playlist) {
      state_.focus = FocusColumn::Types;
    } else if (state_.focus == FocusColumn::Channels) {
      state_.focus = FocusColumn::Categories;
    } else if (state_.focus == FocusColumn::Categories) {
      state_.focus = FocusColumn::Types;
    }
    return;
  }


  if (usingNodeTree()) {
    if (button == Button::Back) {
      if (state_.focus == FocusColumn::Channels) {
        state_.focus = FocusColumn::Categories;
      } else if (state_.focus == FocusColumn::Categories) {
        if (!state_.nodePath.empty()) {
          state_.nodePath.pop_back();
          state_.selectedCategory = 0;
          state_.selectedChannel = 0;
        } else {
          state_.focus = FocusColumn::Types;
        }
      } else if (state_.focus == FocusColumn::Types) {
        state_.focus = FocusColumn::Playlist;
      }
      return;
    }

    if (button == Button::Left) {
      if (state_.focus == FocusColumn::Playlist) {
        const int count = static_cast<int>(state_.config.playlists.size());
        if (count > 1) {
          int activeIndex = 0;

          for (int i = 0; i < count; ++i) {
            if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
              activeIndex = i;
              break;
            }
          }

          activeIndex = (activeIndex + count - 1) % count;
          activatePlaylist(activeIndex);
        }
        return;
      }

      if (state_.focus == FocusColumn::Channels) {
        state_.focus = FocusColumn::Categories;
      } else if (state_.focus == FocusColumn::Categories) {
        state_.focus = FocusColumn::Types;
      }
      return;
    }

    if (button == Button::Right) {
      if (state_.focus == FocusColumn::Playlist) {
        const int count = static_cast<int>(state_.config.playlists.size());
        if (count > 1) {
          int activeIndex = 0;

          for (int i = 0; i < count; ++i) {
            if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
              activeIndex = i;
              break;
            }
          }

          activeIndex = (activeIndex + 1) % count;
          activatePlaylist(activeIndex);
        }
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        state_.focus = FocusColumn::Categories;
      } else if (state_.focus == FocusColumn::Categories) {
        state_.focus = FocusColumn::Channels;
      }
      return;
    }

    if (button == Button::Up) {
      if (state_.focus == FocusColumn::Playlist) {
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        if (state_.selectedType <= 0) {
          state_.focus = FocusColumn::Playlist;
        } else {
          state_.selectedType--;
          state_.nodePath.clear();
          state_.selectedCategory = 0;
          state_.selectedChannel = 0;
        }
        return;
      }

      if (state_.focus == FocusColumn::Categories) {
        if (state_.selectedCategory <= 0) {
          state_.focus = FocusColumn::Playlist;
        } else {
          state_.selectedCategory--;
          state_.selectedChannel = 0;
        }
        return;
      }

      if (state_.focus == FocusColumn::Channels) {
        if (state_.selectedChannel <= 0) {
          state_.focus = FocusColumn::Categories;
        } else {
          state_.selectedChannel--;
        }
        return;
      }
    }

    if (button == Button::Down) {
      if (state_.focus == FocusColumn::Playlist) {
        state_.focus = FocusColumn::Types;
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        state_.selectedType++;
        state_.nodePath.clear();
        state_.selectedCategory = 0;
        state_.selectedChannel = 0;
      } else if (state_.focus == FocusColumn::Categories) {
        state_.selectedCategory++;
        state_.selectedChannel = 0;
      } else if (state_.focus == FocusColumn::Channels) {
        state_.selectedChannel++;
      }

      normalizeIndexes();
      return;
    }

    if (button == Button::Favorite) {
      const MediaNode *node = state_.focus == FocusColumn::Channels
        ? selectedPreviewNode()
        : selectedCurrentNode();

      if (!node || node->url.empty()) {
        return;
      }

      if (state_.favorites.count(node->id)) {
        state_.favorites.erase(node->id);
        state_.message = "Removed favorite: " + (node->title.empty() ? node->name : node->title);
      } else {
        state_.favorites.insert(node->id);
        state_.message = "Favorite: " + (node->title.empty() ? node->name : node->title);
      }
      return;
    }

    if (button == Button::Select) {
      if (state_.focus == FocusColumn::Playlist) {
        state_.screen = ScreenId::AddPlaylist;
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        state_.nodePath.clear();
        state_.selectedCategory = 0;
        state_.selectedChannel = 0;
        state_.focus = FocusColumn::Categories;
        return;
      }

      if (state_.focus == FocusColumn::Categories) {
        const MediaNode *node = selectedCurrentNode();

        if (!node) {
          return;
        }

        if (!node->children.empty()) {
          enterNode(*node, state_.selectedCategory);
          return;
        }

        if (node->playable || !node->url.empty()) {
          playNode(*node);
          return;
        }

        state_.message = "Empty folder";
        return;
      }

      if (state_.focus == FocusColumn::Channels) {
        const MediaNode *preview = selectedPreviewNode();

        if (!preview) {
          return;
        }

        if (!preview->children.empty()) {
          // Preview nodes are children of the currently selected category.
          state_.nodePath.push_back(state_.selectedCategory);
          state_.nodePath.push_back(state_.selectedChannel);
          state_.selectedCategory = 0;
          state_.selectedChannel = 0;
          state_.focus = FocusColumn::Categories;
          normalizeIndexes();
          return;
        }

        if (preview->playable || !preview->url.empty()) {
          playNode(*preview);
          return;
        }

        state_.message = "Empty folder";
        return;
      }
    }
  }

  if (button == Button::Left) {
    if (state_.focus == FocusColumn::Playlist) {
      const int count = static_cast<int>(state_.config.playlists.size());
      if (count > 1) {
        int activeIndex = 0;

        for (int i = 0; i < count; ++i) {
          if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
            activeIndex = i;
            break;
          }
        }

        activeIndex = (activeIndex + count - 1) % count;
        activatePlaylist(activeIndex);
      }
      return;
    }

    if (state_.focus == FocusColumn::Channels) {
      state_.focus = FocusColumn::Categories;
    } else if (state_.focus == FocusColumn::Categories) {
      state_.focus = FocusColumn::Types;
    }
    return;
  }

  if (button == Button::Right) {
    if (state_.focus == FocusColumn::Playlist) {
      const int count = static_cast<int>(state_.config.playlists.size());
      if (count > 1) {
        int activeIndex = 0;

        for (int i = 0; i < count; ++i) {
          if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
            activeIndex = i;
            break;
          }
        }

        activeIndex = (activeIndex + 1) % count;
        activatePlaylist(activeIndex);
      }
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      state_.focus = FocusColumn::Categories;
    } else if (state_.focus == FocusColumn::Categories) {
      state_.focus = FocusColumn::Channels;
    }
    return;
  }

  if (button == Button::Up) {
    if (state_.focus == FocusColumn::Playlist) {
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      if (state_.selectedType <= 0) {
        state_.focus = FocusColumn::Playlist;
      } else {
        state_.selectedType--;
        resetLoadedChannels();
      }
      return;
    }

    if (state_.focus == FocusColumn::Categories) {
      if (state_.selectedCategory <= 0) {
        state_.focus = FocusColumn::Playlist;
      } else {
        state_.selectedCategory--;
        resetLoadedChannels();
      }
      return;
    }

    if (state_.focus == FocusColumn::Channels) {
      if (state_.selectedChannel <= 0) {
        state_.focus = FocusColumn::Playlist;
      } else {
        state_.selectedChannel--;
      }
      return;
    }
  }

  if (button == Button::Down) {
    if (state_.focus == FocusColumn::Playlist) {
      state_.focus = FocusColumn::Types;
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      state_.selectedType++;
      resetLoadedChannels();
    } else if (state_.focus == FocusColumn::Categories) {
      state_.selectedCategory++;
      resetLoadedChannels();
    } else {
      state_.selectedChannel++;
      normalizeIndexes();
      maybePreloadNextPage();
    }
    return;
  }

  if (button == Button::Favorite) {
    const Channel *channel = selectedChannelPtr();
    if (!channel) return;
    if (state_.favorites.count(channel->id)) {
      state_.favorites.erase(channel->id);
      state_.message = "Removed favorite: " + channel->name;
    } else {
      state_.favorites.insert(channel->id);
      state_.message = "Favorite: " + channel->name;
    }
    return;
  }

  if (button == Button::Select) {
    if (state_.focus == FocusColumn::Playlist) {
      state_.screen = ScreenId::AddPlaylist;
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      state_.focus = FocusColumn::Categories;
      return;
    }

    if (state_.focus == FocusColumn::Categories) {
      loadCategory(false);
      state_.focus = FocusColumn::Channels;
      return;
    }

    playSelectedChannel();
  }
}

void App::handleAddPlaylist(Button button) {
  const int playlistCount = static_cast<int>(state_.config.playlists.size());
  const int addM3uIndex = playlistCount;
  const int addXtreamIndex = playlistCount + 1;
  const int backIndex = playlistCount + 2;
  const int maxIndex = backIndex;

  if (button == Button::Back) {
    state_.screen = ScreenId::Dashboard;
    return;
  }

  if (button == Button::Up) {
    state_.selectedAddOption--;
  } else if (button == Button::Down) {
    state_.selectedAddOption++;
  }

  if (state_.selectedAddOption < 0) {
    state_.selectedAddOption = 0;
  }

  if (state_.selectedAddOption > maxIndex) {
    state_.selectedAddOption = maxIndex;
  }

  if (button == Button::Favorite) {
    if (state_.selectedAddOption < playlistCount) {
      deletePlaylist(state_.selectedAddOption);
    }
    return;
  }

  if (button == Button::Select) {
    if (state_.selectedAddOption < playlistCount) {
      activatePlaylist(state_.selectedAddOption);
      return;
    }


    if (state_.selectedAddOption == addM3uIndex) {
      addM3uPlaylist();
      return;
    }

    if (state_.selectedAddOption == addXtreamIndex) {
      addXtreamPlaylist();
      return;
    }

    if (state_.selectedAddOption == backIndex) {
      state_.screen = ScreenId::Dashboard;
      return;
    }
  }
}


const PlaylistConfig *App::activePlaylist() const {
  return state_.config.activePlaylist();
}

std::string App::activePlaylistName() const {
  const PlaylistConfig *playlist = activePlaylist();

  if (!playlist) {
    return "ADD LIST";
  }

  if (!playlist->name.empty()) {
    return playlist->name;
  }

  return playlist->provider == Provider::Xtream ? "Xtream" : "M3U";
}

bool App::loadCachedPlaylist(const PlaylistConfig &playlist) {
  Manifest cached;

  if (!loadManifest(playlist.id, cached) || cached.types.empty()) {
    return false;
  }

  cached.id = playlist.id;
  cached.name = playlist.name.empty() ? cached.name : playlist.name;
  cached.provider = playlist.provider;
  if (cached.source.empty()) {
    cached.source = playlist.sourceUrl();
  }

  state_.manifest = cached;
  state_.hasManifest = true;
  state_.screen = ScreenId::Dashboard;
  state_.focus = FocusColumn::Types;
  state_.selectedType = 0;
  state_.selectedCategory = 0;
  state_.selectedChannel = 0;
  resetLoadedChannels();
  normalizeIndexes();

  state_.message =
    "Loaded cached manifest: " +
    std::to_string(state_.manifest.totalChannels) +
    " channels";

  return true;
}

void App::activatePlaylist(int index) {
  if (index < 0 || index >= static_cast<int>(state_.config.playlists.size())) {
    return;
  }

  PlaylistConfig playlist = state_.config.playlists[static_cast<std::size_t>(index)];
  state_.config.activePlaylistId = playlist.id;
  saveConfig(state_.config);

  /*
    Manifest loading can be heavy for dynamic node manifests (tens of MB).
    Always load it through the background worker, even when it is already
    cached locally, so the UI keeps rendering the Loading spinner.
  */
  startPlaylistLoad(playlist, false);
}

void App::importPlaylist(const PlaylistConfig &playlist) {
  startPlaylistLoad(playlist, false);
}

bool App::playlistLoadActive() const {
  std::lock_guard<std::mutex> lock(playlistLoadMutex_);
  return playlistLoadActive_;
}

void App::startPlaylistLoad(const PlaylistConfig &playlist, bool forceRefresh) {
  updatePlaylistLoad();

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);

    if (playlistLoadActive_) {
      state_.message = "Playlist is already loading. Please wait.";
      return;
    }
  }

  if (playlistLoadThread_.joinable()) {
    playlistLoadThread_.join();
  }

  state_.loading = true;
  state_.loadingMessage = "Loading playlist...";
  state_.message = "Loading " + playlist.name + "...";

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);
    playlistLoadActive_ = true;
    playlistLoadDone_ = false;
    playlistLoadSuccess_ = false;
    playlistLoadForceRefresh_ = forceRefresh;
    playlistLoadPlaylist_ = playlist;
    playlistLoadManifest_ = Manifest{};
    playlistLoadCacheText_.clear();
    playlistLoadCacheTextIsGzip_ = false;
    playlistLoadMessage_ = "Loading playlist...";
    playlistLoadError_.clear();
  }

  Config config = state_.config;

  playlistLoadThread_ = std::thread([this, playlist, config, forceRefresh]() {
    auto setStatus = [this](const std::string &message) {
      std::lock_guard<std::mutex> lock(playlistLoadMutex_);
      playlistLoadMessage_ = message;
    };

    try {
      Manifest manifest;

      if (!forceRefresh) {
        setStatus("Loading cached manifest...");

        if (loadManifestForPlaylist(manifest, playlist.id) &&
            manifest.id == playlist.id &&
            (!manifest.nodes.empty() || !manifest.types.empty())) {
          manifest.id = playlist.id;
          manifest.name = playlist.name.empty() ? manifest.name : playlist.name;
          manifest.source = playlist.sourceUrl().empty() ? manifest.source : playlist.sourceUrl();
          manifest.provider = playlist.provider;

          std::lock_guard<std::mutex> lock(playlistLoadMutex_);
          playlistLoadManifest_ = std::move(manifest);
          playlistLoadSuccess_ = true;
          playlistLoadDone_ = true;
          playlistLoadMessage_ = "Preparing interface...";
          return;
        }
      }

      const std::string sourceUrl = playlist.sourceUrl();

      if (config.parserApiBaseUrl.empty()) {
        throw std::runtime_error("Internal parser API base URL is empty");
      }

      if (sourceUrl.empty()) {
        throw std::runtime_error("Playlist URL/source is empty");
      }

      setStatus("Downloading manifest...");

      ParserApiClient api(config, setStatus);
      ManifestLoadResult loadResult;

      if (playlist.provider == Provider::Xtream) {
        loadResult = api.loadXtreamManifestWithCacheText(sourceUrl);
      } else {
        loadResult = api.loadM3uManifestWithCacheText(sourceUrl);
      }

      setStatus("Preparing manifest...");

      Manifest imported = std::move(loadResult.manifest);
      imported.id = playlist.id;
      imported.name = playlist.name.empty() ? imported.name : playlist.name;
      imported.source = sourceUrl;
      imported.provider = playlist.provider;

      if (imported.nodes.empty() && imported.types.empty()) {
        throw std::runtime_error("Parser returned an empty manifest");
      }

      /*
        Save the raw API response instead of serializing the Manifest tree
        back to JSON. The API response is already the final contract and the
        storage loader accepts both { ok, manifest } and flattened manifests.
        This avoids the expensive recursive write that was freezing/crashing
        on large node manifests.
      */
      if (loadResult.cacheText.empty()) {
        throw std::runtime_error("Parser returned an empty cache body");
      }

      /*
        Do not block the playlist-loading worker on SD-card cache writes.

        Large dynamic manifests can be tens of MB after decompression. Even
        when saved as gzip, the final chunks/gzclose/rename can freeze the
        Switch enough to stop the UI spinner. The manifest is already parsed
        and ready for use here, so hand the UI the Manifest first and defer
        the cache write to a separate throttled background job.
      */
      setStatus("Preparing interface...");

      std::lock_guard<std::mutex> lock(playlistLoadMutex_);
      playlistLoadManifest_ = std::move(imported);
      playlistLoadCacheText_ = std::move(loadResult.cacheText);
      playlistLoadCacheTextIsGzip_ = loadResult.cacheTextIsGzip;
      playlistLoadSuccess_ = true;
      playlistLoadDone_ = true;
      playlistLoadMessage_ = "Preparing interface...";
    } catch (const std::exception &ex) {
      std::lock_guard<std::mutex> lock(playlistLoadMutex_);
      playlistLoadSuccess_ = false;
      playlistLoadDone_ = true;
      playlistLoadError_ = ex.what();
      playlistLoadMessage_ = "Failed";
    }
  });
}

void App::updatePlaylistLoad() {
  PlaylistConfig playlist;
  Manifest manifest;
  std::string cacheText;
  bool cacheTextIsGzip = false;
  std::string message;
  std::string error;
  bool active = false;
  bool done = false;
  bool success = false;

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);
    active = playlistLoadActive_;

    if (!active) {
      return;
    }

    done = playlistLoadDone_;
    success = playlistLoadSuccess_;
    playlist = playlistLoadPlaylist_;
    message = playlistLoadMessage_;
    error = playlistLoadError_;

    if (done && success) {
      /*
        The dynamic nodes manifest can be very large. Do not copy it from
        the worker result into the UI thread local variable; copying the
        whole tree here freezes the UI right after cache saving completes.
        Move it once, then reset the worker state below.
      */
      manifest = std::move(playlistLoadManifest_);
      cacheText = std::move(playlistLoadCacheText_);
      cacheTextIsGzip = playlistLoadCacheTextIsGzip_;
    }
  }

  if (!done) {
    state_.loading = true;
    state_.loadingMessage = message.empty() ? "Loading playlist..." : message;
    state_.message = state_.loadingMessage;
    return;
  }

  if (playlistLoadThread_.joinable()) {
    playlistLoadThread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);
    playlistLoadActive_ = false;
    playlistLoadDone_ = false;
    playlistLoadSuccess_ = false;
    playlistLoadManifest_ = Manifest{};
    playlistLoadCacheText_.clear();
    playlistLoadCacheTextIsGzip_ = false;
    playlistLoadMessage_.clear();
    playlistLoadError_.clear();
  }

  state_.loading = false;
  state_.loadingMessage.clear();

  if (success && !cacheText.empty()) {
    startDeferredCacheSave(playlist.id, std::move(cacheText), cacheTextIsGzip);
  }


  if (!success) {
    state_.hasManifest = false;
    resetLoadedChannels();
    state_.screen = ScreenId::AddPlaylist;
    state_.message = "Failed to load stream data. Check the playlist info and add it again.";

    if (!error.empty()) {
      std::printf("[KBORE] playlist load failed: %s\n", error.c_str());
    }

    return;
  }

  state_.manifest = std::move(manifest);
  state_.hasManifest = true;
  state_.screen = ScreenId::Dashboard;
  state_.focus = FocusColumn::Types;
  state_.selectedType = 0;
  state_.selectedCategory = 0;
  state_.selectedChannel = 0;
  state_.nodePath.clear();

  resetLoadedChannels();
  normalizeIndexes();

  // Keep the active manifest path compatible without forcing re-downloads.
  // The per-playlist cache is the source of truth for large dynamic manifests.
  // Avoid doing another large write here.
  state_.message =
    "Loaded " +
    playlist.name +
    ": " +
    std::to_string(state_.manifest.totalChannels > 0 ? state_.manifest.totalChannels : state_.manifest.totalItems) +
    " items";
}



void App::startDeferredCacheSave(std::string playlistId, std::string manifestText, bool alreadyGzip) {
  if (playlistId.empty() || manifestText.empty()) {
    return;
  }

  /*
    Only one cache writer is allowed at a time.

    The previous detached implementation could keep a zlib/SD-card write alive
    after App state changed or while another playlist was being loaded. On the
    Switch this can crash the homebrew process. This version owns the worker
    thread, tracks its lifecycle, joins completed work, and skips new cache
    jobs while a previous one is still active.
  */
  {
    std::lock_guard<std::mutex> lock(cacheSaveMutex_);

    if (cacheSaveActive_ && !cacheSaveDone_) {
      std::printf(
        "[KBORE][CACHE] cache save already active, skipping new save: current=%s new=%s\n",
        cacheSavePlaylistId_.c_str(),
        playlistId.c_str()
      );
      return;
    }
  }

  if (cacheSaveThread_.joinable()) {
    cacheSaveThread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(cacheSaveMutex_);
    cacheSaveActive_ = true;
    cacheSaveDone_ = false;
    cacheSaveSuccess_ = false;
    cacheSavePlaylistId_ = playlistId;
    cacheSaveError_.clear();
    cacheSaveWritten_ = 0;
    cacheSaveTotal_ = manifestText.size();
  }

  cacheSaveThread_ = std::thread(
    [this, playlistId = std::move(playlistId), manifestText = std::move(manifestText), alreadyGzip]() mutable {
      bool saved = false;
      std::string error;

      try {
        std::printf(
          "[KBORE][CACHE] lifecycle cache save started: playlist=%s size=%zu KB\n",
          playlistId.c_str(),
          manifestText.size() / 1024
        );

        auto progress = [this](std::size_t written, std::size_t total) {
          std::lock_guard<std::mutex> lock(cacheSaveMutex_);
          cacheSaveWritten_ = written;
          cacheSaveTotal_ = total;
        };

        if (alreadyGzip) {
          saved = saveManifestGzipBytesForPlaylist(
            manifestText,
            playlistId,
            progress
          );
        } else {
          saved = saveManifestTextForPlaylist(
            manifestText,
            playlistId,
            progress
          );
        }

        if (!saved) {
          error = alreadyGzip
            ? "saveManifestGzipBytesForPlaylist returned false"
            : "saveManifestTextForPlaylist returned false";
        }
      } catch (const std::exception &ex) {
        error = ex.what();
        saved = false;
      } catch (...) {
        error = "unknown cache save error";
        saved = false;
      }

      {
        std::lock_guard<std::mutex> lock(cacheSaveMutex_);
        cacheSaveSuccess_ = saved;
        cacheSaveError_ = error;
        cacheSaveDone_ = true;
      }

      std::printf(
        "[KBORE][CACHE] lifecycle cache save %s: playlist=%s%s%s\n",
        saved ? "completed" : "failed",
        playlistId.c_str(),
        error.empty() ? "" : " error=",
        error.empty() ? "" : error.c_str()
      );
    }
  );
}

void App::updateCacheSave() {
  bool active = false;
  bool done = false;
  bool success = false;
  std::string playlistId;
  std::string error;
  std::size_t written = 0;
  std::size_t total = 0;

  {
    std::lock_guard<std::mutex> lock(cacheSaveMutex_);
    active = cacheSaveActive_;

    if (!active) {
      return;
    }

    done = cacheSaveDone_;
    success = cacheSaveSuccess_;
    playlistId = cacheSavePlaylistId_;
    error = cacheSaveError_;
    written = cacheSaveWritten_;
    total = cacheSaveTotal_;
  }

  if (!done) {
    /*
      Keep this non-invasive. The cache save happens after the playlist is
      already usable, so do not show a blocking overlay. Leave breadcrumbs in
      the status message only when the app is otherwise idle.
    */
    if (!state_.loading && state_.screen == ScreenId::Dashboard && total > 0) {
      std::ostringstream message;
      message << "Saving playlist cache... " << (written / 1024) << " / " << (total / 1024) << " KB";
      state_.message = message.str();
    }

    return;
  }

  if (cacheSaveThread_.joinable()) {
    cacheSaveThread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(cacheSaveMutex_);
    cacheSaveActive_ = false;
    cacheSaveDone_ = false;
    cacheSaveSuccess_ = false;
    cacheSavePlaylistId_.clear();
    cacheSaveError_.clear();
    cacheSaveWritten_ = 0;
    cacheSaveTotal_ = 0;
  }

  if (!state_.loading && state_.screen == ScreenId::Dashboard) {
    state_.message = success
      ? "Playlist cache saved"
      : "Playlist cache save failed";
  }

  if (!success && !error.empty()) {
    std::printf("[KBORE][CACHE] cache save failed for %s: %s\n", playlistId.c_str(), error.c_str());
  }
}

bool App::cacheSaveActive() const {
  std::lock_guard<std::mutex> lock(cacheSaveMutex_);
  return cacheSaveActive_;
}




void App::addM3uPlaylist() {
  std::string name = trimText(requestTextInput("Playlist name", "M3U", 80));

  if (name.empty()) {
    state_.message = "Playlist name is required";
    return;
  }

  std::string url = trimText(requestTextInput("M3U URL", "", 900));

  if (url.empty()) {
    state_.message = "M3U URL is required";
    return;
  }

  PlaylistConfig playlist;
  playlist.id = safePlaylistId(name, Provider::M3u);
  playlist.name = name;
  playlist.provider = Provider::M3u;
  playlist.m3uUrl = url;

  state_.config.playlists.push_back(playlist);
  state_.config.activePlaylistId = playlist.id;
  state_.config.defaultPlaylistUrl = playlist.m3uUrl;

  if (!saveConfig(state_.config)) {
    state_.message = "Could not save config.json";
    return;
  }

  importPlaylist(playlist);
}

void App::addXtreamPlaylist() {
  std::string name = trimText(requestTextInput("Playlist name", "Xtream", 80));

  if (name.empty()) {
    state_.message = "Playlist name is required";
    return;
  }

  std::string server = trimTrailingSlashLocal(trimText(requestTextInput("Xtream server URL", "http://", 500)));

  if (server.empty() || server == "http://" || server == "https://") {
    state_.message = "Xtream server URL is required";
    return;
  }

  std::string username = trimText(requestTextInput("Xtream username", "", 180));

  if (username.empty()) {
    state_.message = "Xtream username is required";
    return;
  }

  std::string password = trimText(requestTextInput("Xtream password", "", 180));

  if (password.empty()) {
    state_.message = "Xtream password is required";
    return;
  }

  PlaylistConfig playlist;
  playlist.id = safePlaylistId(name, Provider::Xtream);
  playlist.name = name;
  playlist.provider = Provider::Xtream;
  playlist.serverUrl = server;
  playlist.username = username;
  playlist.password = password;

  state_.config.playlists.push_back(playlist);
  state_.config.activePlaylistId = playlist.id;
  state_.config.defaultXtreamUrl = playlist.sourceUrl();

  if (!saveConfig(state_.config)) {
    state_.message = "Could not save config.json";
    return;
  }

  importPlaylist(playlist);
}


void App::deletePlaylist(int index) {
  if (index < 0 || index >= static_cast<int>(state_.config.playlists.size())) {
    return;
  }

  const PlaylistConfig removed = state_.config.playlists[static_cast<std::size_t>(index)];
  state_.config.playlists.erase(state_.config.playlists.begin() + index);

  if (state_.config.activePlaylistId == removed.id) {
    if (!state_.config.playlists.empty()) {
      const int nextIndex = std::min(index, static_cast<int>(state_.config.playlists.size()) - 1);
      state_.config.activePlaylistId = state_.config.playlists[static_cast<std::size_t>(nextIndex)].id;
    } else {
      state_.config.activePlaylistId.clear();
    }
  }

  if (state_.selectedAddOption >= static_cast<int>(state_.config.playlists.size())) {
    state_.selectedAddOption = std::max(0, static_cast<int>(state_.config.playlists.size()) - 1);
  }

  saveConfig(state_.config);

  if (state_.config.playlists.empty()) {
    state_.hasManifest = false;
    state_.manifest = Manifest{};
    resetLoadedChannels();
    state_.message = "Playlist deleted. Add a new list to continue.";
    return;
  }

  state_.message = "Playlist deleted: " + removed.name;

  const PlaylistConfig *playlist = activePlaylist();

  if (playlist) {
    if (!loadCachedPlaylist(*playlist)) {
      importPlaylist(*playlist);
    }
  }
}

void App::importM3u() {
  addM3uPlaylist();
}

void App::importXtream() {
  addXtreamPlaylist();
}

void App::loadCategory(bool append) {
  try {
    const Category *category = selectedCategoryPtr();

    if (!category) {
      state_.message = "No category selected";
      return;
    }

    if (!state_.hasManifest) {
      state_.message = "No playlist loaded";
      return;
    }

    int page = append ? state_.loadedPage + 1 : 1;
    std::string key =
      toString(state_.manifest.provider) + ":" +
      toString(category->type) + ":" +
      category->id;

    if (!append && key == state_.loadedCategoryKey && !state_.loadedChannels.empty()) {
      return;
    }

    ChannelPage result;
    bool fromCache = loadChannelPage(
      state_.manifest.id,
      state_.manifest.provider,
      category->type,
      category->id,
      page,
      result
    );

    if (!fromCache) {
      state_.loading = true;
      state_.loadingMessage = "Loading";
      state_.message = append
        ? "Loading more channels from API..."
        : "Loading channels from API...";
      render();

      result = api_.loadChannels(
        state_.manifest.source,
        state_.manifest.provider,
        category->type,
        category->id,
        page
      );

      saveChannelPage(
        state_.manifest.id,
        state_.manifest.provider,
        category->type,
        category->id,
        result
      );
    } else {
      state_.message = append
        ? "Loading more channels from cache..."
        : "Loading channels from cache...";
    }

    if (append) {
      state_.loadedChannels.insert(
        state_.loadedChannels.end(),
        result.channels.begin(),
        result.channels.end()
      );
    } else {
      state_.loadedChannels = result.channels;
      state_.selectedChannel = 0;
    }

    state_.loadedPage = result.page;
    state_.loadedTotal = result.totalChannels;
    state_.loadedTotalPages = result.totalPages;
    state_.loadedCategoryKey = key;
    state_.loading = false;
    state_.loadingMessage.clear();

    const std::string origin = fromCache ? "cache" : "API";

    state_.message =
      std::to_string(state_.loadedChannels.size()) + "/" +
      std::to_string(result.totalChannels) +
      " channels loaded from " + origin +
      ", page " + std::to_string(result.page) +
      "/" + std::to_string(result.totalPages);
    } catch (const std::exception &ex) {
      std::printf("[KBORE] loadCategory failed: %s\n", ex.what());

      state_.loading = false;
      state_.loadingMessage.clear();

      /*
        Não apague o manifest aqui.

        Se o manifest já carregou, types/categories devem continuar visíveis.
        Uma falha ao carregar channels pode ser:
          - categoria vazia
          - endpoint de channels com formato diferente
          - erro temporário de rede
          - cache antigo/inválido
          - categoria não suportada pela API

        Mas isso não significa que a playlist inteira é inválida.
      */
      resetLoadedChannels();

      state_.screen = ScreenId::Dashboard;
      state_.focus = FocusColumn::Categories;
      state_.message = "Failed to load channels for this category.";
    }
}

void App::maybePreloadNextPage() {
  if (state_.focus != FocusColumn::Channels) {
    return;
  }

  if (state_.loadedChannels.empty()) {
    return;
  }

  if (state_.loadedPage >= state_.loadedTotalPages) {
    return;
  }

  const int preloadThreshold = std::max(1, state_.config.preloadThreshold);
  const int remaining =
    static_cast<int>(state_.loadedChannels.size()) - 1 - state_.selectedChannel;

  if (remaining <= preloadThreshold) {
    loadCategory(true);
  }
}


void App::playSelectedChannel() {
  const Channel *channel = selectedChannelPtr();

  if (!channel) {
    return;
  }

  state_.screen = ScreenId::Player;
  state_.message = "Loading video";
  state_.playerStarted = false;
  state_.playerFrameSeen = false;
  state_.playerLoading = true;
  state_.playerLoadFailed = false;
  state_.playerErrorMessage.clear();
  state_.playerOverlayUntilMs = nowMs() + 5000;

  render();

  if (!player_) {
    player_ = createPlayerBackend();
  }

  if (player_->open(channel->url)) {
    state_.message = "Playing: " + channel->name;
    state_.playerStarted = true;
    state_.playerLoading = false;
    state_.playerLoadFailed = false;
    state_.playerErrorMessage.clear();
  } else {
    state_.message = "Failed to load video";
    state_.playerStarted = false;
    state_.playerLoading = false;
    state_.playerLoadFailed = true;
    state_.playerErrorMessage = player_->error();
  }
}

void App::resetLoadedChannels() {
  state_.loadedChannels.clear();
  state_.loadedCategoryKey.clear();
  state_.loadedPage = 1;
  state_.loadedTotal = 0;
  state_.loadedTotalPages = 1;
  state_.selectedChannel = 0;
}

std::vector<TypeGroup> App::visibleTypes() const {
  if (usingNodeTree()) {
    std::vector<TypeGroup> groups;

    for (const auto &node : state_.manifest.nodes) {
      TypeGroup group;
      group.id = streamTypeFromString(node.type);
      group.label = node.title.empty() ? node.name : node.title;
      group.totalChannels = node.totalChannels > 0 ? node.totalChannels : node.totalItems;
      groups.push_back(group);
    }

    return groups;
  }

  if (state_.hasManifest) return state_.manifest.types;
  return {
    {StreamType::Live, "Live TV", 0, {}},
    {StreamType::Movies, "Movies", 0, {}},
    {StreamType::Series, "Series", 0, {}},
    {StreamType::Radio, "Radio", 0, {}}
  };
}

const TypeGroup *App::selectedTypeGroup() const {
  auto types = visibleTypes();
  if (types.empty()) return nullptr;
  int index = std::clamp(state_.selectedType, 0, static_cast<int>(types.size()) - 1);
  // Safe because render/use is immediate; avoid returning pointer to temp? Use manifest direct below.
  if (!state_.hasManifest) return nullptr;
  if (index >= 0 && index < static_cast<int>(state_.manifest.types.size())) return &state_.manifest.types[index];
  return nullptr;
}

const Category *App::selectedCategoryPtr() const {
  if (usingNodeTree()) {
    return nullptr;
  }

  const TypeGroup *type = selectedTypeGroup();
  if (!type || type->categories.empty()) return nullptr;
  int index = std::clamp(state_.selectedCategory, 0, static_cast<int>(type->categories.size()) - 1);
  return &type->categories[index];
}

const Channel *App::selectedChannelPtr() const {
  if (state_.loadedChannels.empty()) return nullptr;
  int index = std::clamp(state_.selectedChannel, 0, static_cast<int>(state_.loadedChannels.size()) - 1);
  return &state_.loadedChannels[index];
}


bool App::usingNodeTree() const {
  return state_.hasManifest && !state_.manifest.nodes.empty();
}

const MediaNode *App::selectedRootNode() const {
  if (!usingNodeTree()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedType,
    0,
    std::max(0, static_cast<int>(state_.manifest.nodes.size()) - 1)
  );

  if (index < 0 || index >= static_cast<int>(state_.manifest.nodes.size())) {
    return nullptr;
  }

  return &state_.manifest.nodes[static_cast<std::size_t>(index)];
}

const MediaNode *App::nodeAtPath(const MediaNode *root, const std::vector<int> &path) const {
  const MediaNode *node = root;

  for (int index : path) {
    if (!node || node->children.empty()) {
      return node;
    }

    int safeIndex = std::clamp(
      index,
      0,
      std::max(0, static_cast<int>(node->children.size()) - 1)
    );

    node = &node->children[static_cast<std::size_t>(safeIndex)];
  }

  return node;
}

const MediaNode *App::currentNodeParent() const {
  return nodeAtPath(selectedRootNode(), state_.nodePath);
}

std::vector<const MediaNode *> App::currentNodeChildren() const {
  std::vector<const MediaNode *> nodes;
  const MediaNode *parent = currentNodeParent();

  if (!parent) {
    return nodes;
  }

  for (const auto &child : parent->children) {
    nodes.push_back(&child);
  }

  return nodes;
}

const MediaNode *App::selectedCurrentNode() const {
  std::vector<const MediaNode *> children = currentNodeChildren();

  if (children.empty()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedCategory,
    0,
    std::max(0, static_cast<int>(children.size()) - 1)
  );

  return children[static_cast<std::size_t>(index)];
}

std::vector<const MediaNode *> App::previewNodeChildren() const {
  std::vector<const MediaNode *> nodes;
  const MediaNode *selected = selectedCurrentNode();

  if (!selected) {
    return nodes;
  }

  if (!selected->children.empty()) {
    for (const auto &child : selected->children) {
      nodes.push_back(&child);
    }
    return nodes;
  }

  if (selected->playable || !selected->url.empty()) {
    nodes.push_back(selected);
  }

  return nodes;
}

const MediaNode *App::selectedPreviewNode() const {
  std::vector<const MediaNode *> nodes = previewNodeChildren();

  if (nodes.empty()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedChannel,
    0,
    std::max(0, static_cast<int>(nodes.size()) - 1)
  );

  return nodes[static_cast<std::size_t>(index)];
}

Channel App::channelFromNode(const MediaNode &node) const {
  Channel channel;
  channel.id = node.id;
  channel.name = node.title.empty() ? node.name : node.title;
  channel.url = node.url;
  channel.logo = node.logo;
  channel.group = node.group;
  channel.groupId = node.groupId;
  channel.type = streamTypeFromString(node.type);
  return channel;
}

void App::enterNode(const MediaNode &node, int childIndex) {
  if (!usingNodeTree()) {
    return;
  }

  if (node.children.empty()) {
    return;
  }

  state_.nodePath.push_back(childIndex);
  state_.selectedCategory = 0;
  state_.selectedChannel = 0;
  resetLoadedChannels();
  state_.focus = FocusColumn::Categories;
}

void App::playNode(const MediaNode &node) {
  if (node.url.empty()) {
    state_.message = "This item has no playback URL";
    return;
  }

  Channel channel = channelFromNode(node);
  state_.loadedChannels.clear();
  state_.loadedChannels.push_back(channel);
  state_.selectedChannel = 0;
  playSelectedChannel();
}

std::string App::breadcrumbText() const {
  if (!usingNodeTree()) {
    return "";
  }

  std::string out = activePlaylistName();
  const MediaNode *root = selectedRootNode();

  if (!root) {
    return out;
  }

  out += " > ";
  out += root->title.empty() ? root->name : root->title;

  const MediaNode *node = root;
  for (int index : state_.nodePath) {
    if (!node || node->children.empty()) {
      break;
    }

    int safeIndex = std::clamp(
      index,
      0,
      std::max(0, static_cast<int>(node->children.size()) - 1)
    );

    node = &node->children[static_cast<std::size_t>(safeIndex)];
    out += " > ";
    out += node->title.empty() ? node->name : node->title;
  }

  return out;
}

void App::normalizeIndexes() {
  auto types = visibleTypes();
  state_.selectedType = std::clamp(state_.selectedType, 0, std::max(0, static_cast<int>(types.size()) - 1));

  if (usingNodeTree()) {
    const MediaNode *root = selectedRootNode();

    if (!root) {
      state_.nodePath.clear();
      state_.selectedCategory = 0;
      state_.selectedChannel = 0;
      return;
    }

    // Clamp the stored path defensively, because different playlists may have
    // different tree depths and branch sizes.
    const MediaNode *node = root;
    std::vector<int> safePath;

    for (int index : state_.nodePath) {
      if (!node || node->children.empty()) {
        break;
      }

      int safeIndex = std::clamp(
        index,
        0,
        std::max(0, static_cast<int>(node->children.size()) - 1)
      );

      safePath.push_back(safeIndex);
      node = &node->children[static_cast<std::size_t>(safeIndex)];
    }

    state_.nodePath = safePath;

    const auto children = currentNodeChildren();
    state_.selectedCategory = std::clamp(
      state_.selectedCategory,
      0,
      std::max(0, static_cast<int>(children.size()) - 1)
    );

    const auto preview = previewNodeChildren();
    state_.selectedChannel = std::clamp(
      state_.selectedChannel,
      0,
      std::max(0, static_cast<int>(preview.size()) - 1)
    );

    return;
  }

  const TypeGroup *type = selectedTypeGroup();
  int categories = type ? static_cast<int>(type->categories.size()) : 0;
  state_.selectedCategory = std::clamp(state_.selectedCategory, 0, std::max(0, categories - 1));
  state_.selectedChannel = std::clamp(state_.selectedChannel, 0, std::max(0, static_cast<int>(state_.loadedChannels.size()) - 1));
}


static int windowStart(int selected, int size, int maxRows) {
  if (size <= maxRows) return 0;
  int half = maxRows / 2;
  return std::max(0, std::min(selected - half, size - maxRows));
}

void App::render() {
  gfx_.beginFrame();

  if (splashVisible_) {
    const long long elapsed = nowMs() - splashStartedAtMs_;

    if (elapsed < splashDurationMs_) {
      renderSplashGraphic();
      gfx_.present();
      return;
    }

    splashVisible_ = false;
  }

  switch (state_.screen) {
    case ScreenId::Dashboard: renderDashboardGraphic(); break;
    case ScreenId::AddPlaylist: renderAddPlaylistGraphic(); break;
    case ScreenId::Player: renderPlayerGraphic(); break;
    case ScreenId::Settings: renderSettingsGraphic(); break;
    case ScreenId::Playlists: renderAddPlaylistGraphic(); break;
  }

  if (state_.screen == ScreenId::Player && player_ && player_->nativeVideoActive()) {
    return;
  }

  gfx_.present();
}

void App::renderSplashGraphic() {
  gfx_.drawImageFile(
    "romfs:/logo/splash.png",
    0,
    0,
    Graphics::Width,
    Graphics::Height,
    true
  );
}

void App::renderDashboard() {
  renderDashboardGraphic();
}

void App::renderDashboardGraphic() {
  normalizeIndexes();

  auto types = visibleTypes();
  std::vector<Category> categories;
  const TypeGroup *type = selectedTypeGroup();

  std::vector<const MediaNode *> nodeChildren;
  std::vector<const MediaNode *> nodePreview;
  Channel selectedNodeChannel;
  bool hasSelectedNodeChannel = false;

  if (usingNodeTree()) {
    nodeChildren = currentNodeChildren();
    nodePreview = previewNodeChildren();

    for (const MediaNode *node : nodeChildren) {
      if (!node) {
        continue;
      }

      Category category;
      category.id = node->id;
      category.name = node->title.empty() ? node->name : node->title;
      category.totalChannels = node->totalChannels > 0 ? node->totalChannels : node->totalItems;
      category.type = streamTypeFromString(node->type);
      categories.push_back(category);
    }

    const MediaNode *previewNode = selectedPreviewNode();
    if (previewNode && (previewNode->playable || !previewNode->url.empty())) {
      selectedNodeChannel = channelFromNode(*previewNode);
      hasSelectedNodeChannel = true;
    }
  } else if (type) {
    categories = type->categories;
  }

  const Channel *selectedChannel = hasSelectedNodeChannel
    ? &selectedNodeChannel
    : selectedChannelPtr();

  const Category *selectedCategory = selectedCategoryPtr();
  const std::string provider = providerLabel(state_.hasManifest ? state_.manifest.provider : Provider::Local);

  const Color text = rgb(248,250,252);
  const Color textSoft = rgb(218,226,244);
  const Color muted = rgb(150,163,190);
  const Color panelTop = rgb(16,24,45);
  const Color panelBottom = rgb(7,11,22);
  const Color panelBorder = rgb(30,42,68);
  const Color blue = rgb(20,132,255);
  const Color brightBlue = rgb(0,190,255);
  const Color green = rgb(57,220,35);
  const Color card = rgba(13,20,37,220);

  auto drawLogoOrFallback = [&](const Channel &channel, int x, int y, int w, int h) {
    if (!channel.logo.empty()) {
      const Bitmap *bitmap = imageCache_.get(channel.logo);
      if (bitmap && bitmap->valid()) {
        gfx_.drawImage(*bitmap, x, y, w, h);
        return;
      }
    }

    gfx_.drawLogoFallback(channel.name, x, y, w, h, 2);
  };


  // Header -----------------------------------------------------------------
  gfx_.drawImageFileCentered(
    "romfs:/logo/logo-horizontal.png",
    24,
    14,
    280,
    66
  );

  // Active playlist switcher. It intentionally shows only the user-defined
  // playlist name, keeping the header clean.
  Rect playlistSwitch{322, 22, 250, 42};
  gfx_.fillRoundRect(
    playlistSwitch.x,
    playlistSwitch.y,
    playlistSwitch.w,
    playlistSwitch.h,
    13,
    rgba(15, 23, 42, 210)
  );
  const bool playlistFocused = state_.focus == FocusColumn::Playlist;
  gfx_.strokeRoundRect(
    playlistSwitch.x,
    playlistSwitch.y,
    playlistSwitch.w,
    playlistSwitch.h,
    13,
    playlistFocused ? brightBlue : rgba(72, 92, 128, 42),
    playlistFocused ? 3 : 1
  );
  gfx_.drawText(
    Graphics::fitText(activePlaylistName(), 18),
    playlistSwitch.x + 18,
    playlistSwitch.y + 14,
    2,
    text,
    true
  );
  gfx_.drawTextRight(
    "v",
    playlistSwitch.x + playlistSwitch.w - 16,
    playlistSwitch.y + 14,
    2,
    playlistFocused ? brightBlue : muted,
    true
  );

  gfx_.fillCircle(984, 43, 6, green);
  gfx_.drawTextRight("ONLINE", 1080, 36, 3, text, true);
  gfx_.drawTextRight(formatSystemClockTime(), 1190, 30, 5, text, false);
  gfx_.drawHeaderIcon("config", 1216, 20, 38, text);

  auto drawPanel = [&](Rect r, const std::string &title, const std::string &icon, bool focused){
    gfx_.fillVerticalGradient(r.x, r.y, r.w, r.h, panelTop, panelBottom);
    gfx_.strokeRoundRect(r.x, r.y, r.w, r.h, 16, focused ? blue : panelBorder, focused ? 2 : 1);
    gfx_.drawHeaderIcon(icon, r.x + 20, r.y + 17, 34, blue);
    gfx_.drawText(title, r.x + 62, r.y + 23, 3, textSoft, true);
  };

  // Narrower Stream Types + wider channels.
  Rect typesPanel{18, 90, 300, 470};
  Rect categoriesPanel{332, 90, 330, 470};
  Rect channelsPanel{676, 90, 587, 470};

  drawPanel(typesPanel, usingNodeTree() ? "ROOT" : "STREAM TYPES", "layers", state_.focus == FocusColumn::Types);

  std::string categoriesTitle = "CATEGORIES";
  if (usingNodeTree()) {
    const MediaNode *parent = currentNodeParent();
    if (parent) {
      categoriesTitle = Graphics::fitText(parent->title.empty() ? parent->name : parent->title, 18);
    }
  }

  drawPanel(categoriesPanel, categoriesTitle, "categories", state_.focus == FocusColumn::Categories);

  std::string chTitle = usingNodeTree() ? "NEXT / ITEMS" : "CHANNELS";
  if (!usingNodeTree() && type) chTitle += " (" + type->label + ")";
  drawPanel(channelsPanel, chTitle, "channels", state_.focus == FocusColumn::Channels);

  int panelTotal = state_.loadedTotal > 0
    ? state_.loadedTotal
    : (
        usingNodeTree()
          ? static_cast<int>(nodePreview.size())
          : (type ? type->totalChannels : 0)
      );

  gfx_.drawTextRight(std::to_string(panelTotal) + " ITEMS", channelsPanel.x + channelsPanel.w - 28, channelsPanel.y + 25, 2, muted, false);

  // Stream type cards -------------------------------------------------------
  int typeY = typesPanel.y + 70;
  for (int i=0; i<std::min(4, (int)types.size()); ++i) {
    const auto &t = types[i];
    const bool selected = i == state_.selectedType;
    Color base = typeColor(toString(t.id));
    int y = typeY + i * 74;
    gfx_.fillHorizontalGradient(typesPanel.x + 14, y, typesPanel.w - 28, 64, rgba(base.r,base.g,base.b, selected?110:55), rgba(12,18,34, selected?245:210));
    gfx_.fillRoundRect(typesPanel.x + 14, y, typesPanel.w - 28, 64, 13, rgba(0,0,0,0));
    gfx_.strokeRoundRect(typesPanel.x + 14, y, typesPanel.w - 28, 64, 13, selected ? brightBlue : rgba(72,92,128,24), selected ? 3 : 1);
    gfx_.drawIconBox(toString(t.id), typesPanel.x + 26, y + 10, 44, lighten(base,25), darken(base,30), text);
    gfx_.drawText(Graphics::fitText(t.label, 12), typesPanel.x + 82, y + 15, 3, text, true);
    // Stream types sub label
    // std::string sub = t.id == StreamType::Live ? "LIVE CHANNELS" : (t.id == StreamType::Movies ? "MOVIES" : (t.id == StreamType::Series ? "SERIES" : "RADIOS"));
    // gfx_.drawText(sub, typesPanel.x + 82, y + 42, 2, muted, false);
    if (t.totalChannels > 0) gfx_.drawBadge(std::to_string(t.totalChannels), typesPanel.x + typesPanel.w - 68, y + 21, 42, 24, rgba(30,41,59,210), text);
  }

  int cy = typesPanel.y + typesPanel.h - 74;
  gfx_.fillRoundRect(typesPanel.x + 14, cy, typesPanel.w - 28, 56, 13, rgba(15,23,42,220));
  gfx_.drawIconBox("OK", typesPanel.x+26, cy+9, 38, rgb(22,101,52), rgb(20,83,45), green);
  gfx_.drawText("CONNECTED", typesPanel.x+78, cy+12, 3, text, true);
  gfx_.drawText((provider + " ONLINE"), typesPanel.x+78, cy+37, 2, green, false);

  if (usingNodeTree()) {
    gfx_.drawText(
      Graphics::fitText(breadcrumbText(), 64),
      categoriesPanel.x + 18,
      categoriesPanel.y + categoriesPanel.h - 28,
      1,
      muted,
      false
    );
  }

  // Categories: no side acronym/icon, smaller text -------------------------
  int catRows = 9;
  int catStart = windowStart(state_.selectedCategory, (int)categories.size(), catRows);
  for (int i=0; i<catRows; ++i) {
    int index = catStart + i;
    int y = categoriesPanel.y + 68 + i * 42;
    if (index >= (int)categories.size()) break;
    const auto &c = categories[index];
    bool selected = index == state_.selectedCategory;
    gfx_.fillRoundRect(categoriesPanel.x + 14, y, categoriesPanel.w - 30, 36, 10, selected ? rgba(18,45,94,230) : rgba(15,23,42,180));
    gfx_.strokeRoundRect(categoriesPanel.x + 14, y, categoriesPanel.w - 30, 36, 10, selected ? brightBlue : rgba(72,92,128,24), selected ? 2 : 1);
    gfx_.drawText(Graphics::fitText(c.name, 24), categoriesPanel.x + 32, y + 10, 2, selected ? text : textSoft, true);
    gfx_.drawBadge(std::to_string(c.totalChannels), categoriesPanel.x + categoriesPanel.w - 72, y + 8, 42, 22, rgba(41,54,82,220), text);
  }

  // Channels/movies/items: no numeric prefix, smaller text, wider panel ----
  int channelRows = 7;

  if (usingNodeTree()) {
    int chanStart = windowStart(state_.selectedChannel, static_cast<int>(nodePreview.size()), channelRows);

    for (int i = 0; i < channelRows; ++i) {
      int index = chanStart + i;
      int y = channelsPanel.y + 66 + i * 55;

      if (index >= static_cast<int>(nodePreview.size())) {
        break;
      }

      const MediaNode *node = nodePreview[static_cast<std::size_t>(index)];
      if (!node) {
        continue;
      }

      bool selected = index == state_.selectedChannel;
      const bool playable = node->playable || !node->url.empty();
      const bool folder = !node->children.empty();

      gfx_.fillRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? rgba(12,23,52,245) : rgba(10,15,29,215));
      gfx_.strokeRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? brightBlue : rgba(72,92,128,24), selected ? 2 : 1);

      Channel ch = channelFromNode(*node);
      drawLogoOrFallback(ch, channelsPanel.x + 30, y + 7, 48, 35);

      const int nameX = channelsPanel.x + 94;
      gfx_.drawText(Graphics::fitText(ch.name, 35), nameX, y + 9, 3, text, true);

      std::string sub = folder
        ? ("FOLDER • " + std::to_string(static_cast<int>(node->children.size())) + " ITEMS")
        : (playable ? "PLAYABLE" : "EMPTY");

      gfx_.drawText(sub, nameX, y + 32, 2, muted, false);
      gfx_.drawText(state_.favorites.count(node->id) ? "*" : (folder ? ">" : "<3"), channelsPanel.x + channelsPanel.w - 42, y + 15, 2, muted, false);
    }

    if (nodePreview.empty()) {
      gfx_.drawText("SELECT A FOLDER OR ITEM", channelsPanel.x + 40, channelsPanel.y + 178, 3, muted, false);
      gfx_.drawText("PRESS A TO OPEN", channelsPanel.x + 40, channelsPanel.y + 206, 2, blue, true);
    }
  } else {
    int chanStart = windowStart(state_.selectedChannel, (int)state_.loadedChannels.size(), channelRows);
    for (int i=0; i<channelRows; ++i) {
      int index = chanStart + i;
      int y = channelsPanel.y + 66 + i * 55;
      if (index >= (int)state_.loadedChannels.size()) break;
      const auto &ch = state_.loadedChannels[index];
      bool selected = index == state_.selectedChannel;
      gfx_.fillRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? rgba(12,23,52,245) : rgba(10,15,29,215));
      gfx_.strokeRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? brightBlue : rgba(72,92,128,24), selected ? 2 : 1);

      // Prefer the real logo from the API. If it is missing or cannot be decoded, use a small acronym fallback.
      drawLogoOrFallback(ch, channelsPanel.x + 30, y + 7, 48, 35);

      const int nameX = channelsPanel.x + 94;
      gfx_.drawText(Graphics::fitText(ch.name, 35), nameX, y + 9, 3, text, true);
      gfx_.drawText("EPG unavailable", nameX, y + 32, 2, muted, false);
      gfx_.drawText(state_.favorites.count(ch.id) ? "*" : "<3", channelsPanel.x + channelsPanel.w - 42, y + 15, 2, muted, false);
    }
    if (state_.loadedChannels.empty()) {
      gfx_.drawText("SELECT A CATEGORY", channelsPanel.x + 40, channelsPanel.y + 178, 3, muted, false);
      gfx_.drawText("PRESS A TO LOAD", channelsPanel.x + 40, channelsPanel.y + 206, 2, blue, true);
    }
  }

  // Info panel: smaller footer text ----------------------------------------
  Rect info{18, 575, 1245, 88};
  gfx_.fillVerticalGradient(info.x, info.y, info.w, info.h, rgba(27,35,52,235), rgba(13,18,29,235));
  gfx_.fillRoundRect(info.x, info.y, info.w, info.h, 14, rgba(0,0,0,0));
  gfx_.strokeRoundRect(info.x, info.y, info.w, info.h, 14, rgba(72,92,128,35), 1);
  if (selectedChannel) {
    drawLogoOrFallback(*selectedChannel, info.x + 34, info.y + 13, 154, 62);
    gfx_.drawText(Graphics::fitText(selectedChannel->name, 42), info.x + 210, info.y + 18, 2, text, true);
    gfx_.drawText("EPG unavailable", info.x + 210, info.y + 46, 1, muted, false);
  } else {
    gfx_.drawText(state_.hasManifest ? Graphics::fitText(state_.manifest.name, 34) : "NSTV", info.x + 40, info.y + 30, 4, text, true);
  }
  gfx_.drawText("PAGE " + std::to_string(state_.loadedPage) + " / " + std::to_string(state_.loadedTotalPages), info.x + 610, info.y + 24, 2, text, true);
  gfx_.drawText("LOADED: " + std::to_string(state_.loadedChannels.size()) + " / " + std::to_string(state_.loadedTotal) + " CHANNELS", info.x + 610, info.y + 50, 1, blue, true);
  gfx_.drawText(provider + ": " + Graphics::fitText(state_.hasManifest ? state_.manifest.name : "CONFIGURE", 38), info.x + 940, info.y + 24, 2, text, false);
  gfx_.drawText("UPDATED", info.x + 978, info.y + 54, 1, green, false);

  // Controls footer: smaller text ------------------------------------------
  Rect foot{18, 675, 1245, 36};
  gfx_.fillRoundRect(foot.x, foot.y, foot.w, foot.h, 10, rgba(17,24,39,240));
  gfx_.strokeRoundRect(foot.x, foot.y, foot.w, foot.h, 10, rgba(72,92,128,28), 1);
  gfx_.drawText("UP - PLAYLISTS   |    LEFT/RIGHT - COLUMNS   |    A  - SELECT   |    B - BACK   |    X - FAVORITES   |    + - PLAYLISTS", foot.x + 26, foot.y + 13, 1, text, true);

  if (state_.loading) {
    renderLoadingOverlay(state_.loadingMessage.empty() ? "Loading" : state_.loadingMessage);
  }
}


void App::renderLoadingOverlay(const std::string &message) {
  const Color text = rgb(248, 250, 252);
  const Color muted = rgb(148, 163, 184);
  const Color blue = rgb(0, 191, 255);

  gfx_.fillRoundRect(
    0,
    0,
    Graphics::Width,
    Graphics::Height,
    0,
    rgba(2, 6, 18, 180)
  );

  const int cx = Graphics::Width / 2;
  const int cy = Graphics::Height / 2 - 18;

  const char spinnerChars[4] = {'|', '/', '-', '\\'};
  const int index = static_cast<int>((nowMs() / 140) % 4);
  std::string spinner(1, spinnerChars[index]);

  gfx_.fillRoundRect(cx - 170, cy - 74, 340, 170, 22, rgba(15, 23, 42, 235));
  gfx_.strokeRoundRect(cx - 170, cy - 74, 340, 170, 22, rgba(72, 92, 128, 70), 1);

  gfx_.drawText(spinner, cx - 10, cy - 36, 5, blue, true);
  gfx_.drawText(message.empty() ? "Loading" : message, cx - 58, cy + 22, 3, text, true);
  gfx_.drawText("Please wait", cx - 56, cy + 56, 1, muted, false);
}

void App::renderAddPlaylistGraphic() {
  const Color text = rgb(248, 250, 252);
  const Color muted = rgb(166, 178, 207);
  const Color blue = rgb(37, 99, 235);
  const Color brightBlue = rgb(0, 191, 255);
  const Color panel = rgba(17, 24, 39, 225);

  gfx_.drawImageFileCentered(
    "romfs:/logo/logo-horizontal.png",
    64,
    42,
    260,
    62
  );

  gfx_.drawText("PLAYLISTS", 64, 124, 5, muted, true);
  gfx_.drawText("Select a saved list or add a new source", 64, 166, 2, muted, false);

  const int playlistCount = static_cast<int>(state_.config.playlists.size());
  const int addM3uIndex = playlistCount;
  const int addXtreamIndex = playlistCount + 1;
  const int backIndex = playlistCount + 2;
  const int totalOptions = playlistCount + 3;

  const int rows = 6;
  const int start = windowStart(state_.selectedAddOption, totalOptions, rows);

  for (int i = 0; i < rows; ++i) {
    const int optionIndex = start + i;

    if (optionIndex >= totalOptions) {
      break;
    }

    const int y = 210 + i * 66;
    const bool selected = optionIndex == state_.selectedAddOption;

    gfx_.fillRoundRect(
      96,
      y,
      900,
      54,
      14,
      selected ? rgba(37, 99, 235, 225) : panel
    );

    gfx_.strokeRoundRect(
      96,
      y,
      900,
      54,
      14,
      selected ? brightBlue : rgba(72, 92, 128, 30),
      selected ? 3 : 1
    );

    std::string title;
    std::string subtitle;

    if (optionIndex < playlistCount) {
      const PlaylistConfig &playlist = state_.config.playlists[static_cast<std::size_t>(optionIndex)];
      const bool active = playlist.id == state_.config.activePlaylistId;

      title = (active ? "* " : "  ") + playlist.name;
      subtitle =
        std::string(active ? "ACTIVE • " : "") +
        (playlist.provider == Provider::Xtream ? "XTREAM" : "M3U");
    } else if (optionIndex == addM3uIndex) {
      title = "+ ADD M3U URL";
      subtitle = "Add a playlist using an M3U link";
    } else if (optionIndex == addXtreamIndex) {
      title = "+ ADD XTREAM";
      subtitle = "Add server, username and password";
    } else {
      title = "BACK";
      subtitle = "Return to dashboard";
    }

    gfx_.drawText(Graphics::fitText(title, 36), 134, y + 11, 3, text, true);
    gfx_.drawText(Graphics::fitText(subtitle, 60), 134, y + 34, 1, selected ? text : muted, false);
  }

  gfx_.drawText("A SELECT    X DELETE SELECTED    B BACK", 96, 632, 2, muted, true);
  gfx_.drawText(Graphics::fitText(state_.message, 86), 96, 666, 2, rgb(0, 145, 255), false);

  if (state_.loading) {
    renderLoadingOverlay(state_.loadingMessage.empty() ? "Loading" : state_.loadingMessage);
  }
}

void App::renderPlayerGraphic() {
  const Channel *channel = selectedChannelPtr();

  gfx_.fillRect(0, 0, Graphics::Width, Graphics::Height, rgb(0, 0, 0));

  bool hasFrame = false;
  const bool isOpen = player_ && player_->isOpen();
  const bool isPaused = player_ && player_->isPaused();

  if (isOpen) {
    player_->update();

    if (player_->nativeVideoActive()) {
      hasFrame = true;

      if (!state_.playerFrameSeen) {
        state_.playerFrameSeen = true;
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }
    } else if (player_->yuvFrame().valid()) {
      hasFrame = true;

      gfx_.drawYuvFrame(
        player_->yuvFrame(),
        0,
        0,
        Graphics::Width,
        Graphics::Height
      );

      if (!state_.playerFrameSeen) {
        state_.playerFrameSeen = true;
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }
    } else if (player_->frame().valid()) {
      hasFrame = true;

      gfx_.drawImage(
        player_->frame(),
        0,
        0,
        Graphics::Width,
        Graphics::Height
      );

      if (!state_.playerFrameSeen) {
        state_.playerFrameSeen = true;
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }
    } else {
      gfx_.drawText("Loading...", 80, 320, 3, rgb(248, 250, 252), true);
    }
  } else {
    gfx_.fillVerticalGradient(
      0,
      0,
      Graphics::Width,
      Graphics::Height,
      rgb(7, 11, 22),
      rgb(2, 5, 11)
    );

    const int boxW = 520;
    const int boxH = 190;
    const int boxX = (Graphics::Width - boxW) / 2;
    const int boxY = (Graphics::Height - boxH) / 2;

    gfx_.fillRoundRect(
      boxX,
      boxY,
      boxW,
      boxH,
      24,
      rgba(15, 23, 42, 232)
    );

    gfx_.strokeRoundRect(
      boxX,
      boxY,
      boxW,
      boxH,
      24,
      state_.playerLoadFailed ? rgba(248, 113, 113, 120) : rgba(72, 92, 128, 70),
      1
    );

    if (state_.playerLoadFailed) {
      gfx_.drawText(
        "Failed to load video",
        boxX + 82,
        boxY + 58,
        3,
        rgb(248, 113, 113),
        true
      );

      gfx_.drawText(
        "Press B to return",
        boxX + 164,
        boxY + 104,
        1,
        rgb(203, 213, 225),
        false
      );
    } else {
      const char spinnerChars[4] = {'|', '/', '-', '\\'};
      const int index = static_cast<int>((nowMs() / 140) % 4);
      std::string spinner(1, spinnerChars[index]);

      gfx_.drawText(
        spinner,
        boxX + 244,
        boxY + 40,
        5,
        rgb(0, 191, 255),
        true
      );

      gfx_.drawText(
        "Loading video",
        boxX + 138,
        boxY + 106,
        3,
        rgb(248, 250, 252),
        true
      );
    }
  }

  const bool showOverlay =
    !state_.playerFrameSeen ||
    nowMs() < state_.playerOverlayUntilMs ||
    isPaused ||
    !isOpen;

  if (showOverlay && isOpen) {
    gfx_.fillHorizontalGradient(
      0,
      0,
      Graphics::Width,
      76,
      rgba(0, 0, 0, 210),
      rgba(0, 0, 0, 80)
    );

    gfx_.fillHorizontalGradient(
      0,
      Graphics::Height - 76,
      Graphics::Width,
      76,
      rgba(0, 0, 0, 80),
      rgba(0, 0, 0, 220)
    );

    if (channel) {
      gfx_.drawText(
        Graphics::fitText(channel->name, 54),
        28,
        18,
        2,
        rgb(248, 250, 252),
        true
      );

      if (player_) {
        gfx_.drawText(
          std::string("Backend: ") + player_->name(),
          28,
          46,
          1,
          rgb(150, 163, 190),
          false
        );
      }

      std::string status;

      if (isPaused) {
        status = "PAUSED";
      } else if (isOpen && hasFrame) {
        status = "PLAYING";
      } else if (isOpen) {
        status = "LOADING";
      } else {
        status = "ERROR";
      }

      gfx_.drawText(
        status,
        28,
        Graphics::Height - 50,
        1,
        isOpen ? rgb(57, 220, 35) : rgb(248, 113, 113),
        true
      );
    }

    gfx_.drawTextRight(
      "A PAUSE/RESUME   B BACK",
      Graphics::Width - 28,
      Graphics::Height - 50,
      1,
      rgb(248, 250, 252),
      true
    );
  }
}

void App::renderSettingsGraphic() {
  gfx_.drawText("SETTINGS", 80, 80, 7, rgb(248,250,252), true);
  gfx_.drawText("CONFIG: " + configPath(), 80, 160, 3, rgb(166,178,207), false);
  gfx_.drawText("B BACK", 80, 620, 4, rgb(166,178,207), true);
}


template <typename T, typename LabelFn>
std::vector<std::string> App::buildWindowRows(
  const std::vector<T> &items,
  int selected,
  int width,
  int maxRows,
  LabelFn labelFn
) const {
  std::vector<std::string> rows;

  if (items.empty()) {
    rows.push_back("  No items");
    return rows;
  }

  int half = maxRows / 2;
  int start = std::max(
    0,
    std::min(
      selected - half,
      std::max(0, static_cast<int>(items.size()) - maxRows)
    )
  );

  int end = std::min(static_cast<int>(items.size()), start + maxRows);

  for (int i = start; i < end; ++i) {
    std::string prefix = (i == selected ? "> " : "  ");
    rows.push_back(crop(prefix + labelFn(items[i]), width));
  }

  return rows;
}

template <typename T, typename LabelFn>
void App::printWindow(const std::string &title, const std::vector<T> &items, int selected, int width, LabelFn labelFn) const {
  std::cout << "\n" << title << "\n";
  if (items.empty()) {
    std::cout << "  No items\n";
    return;
  }
  const int maxRows = 10;
  int half = maxRows / 2;
  int start = std::max(0, std::min(selected - half, std::max(0, static_cast<int>(items.size()) - maxRows)));
  int end = std::min(static_cast<int>(items.size()), start + maxRows);
  for (int i = start; i < end; ++i) {
    std::string label = crop(labelFn(items[i]), width);
    std::cout << (i == selected ? "> " : "  ") << label << "\n";
  }
}

std::string App::crop(const std::string &value, std::size_t max) {
  if (max == 0) return "";

  std::string output;
  output.reserve(std::min(value.size(), max));

  std::size_t visible = 0;
  for (std::size_t i = 0; i < value.size();) {
    unsigned char c = static_cast<unsigned char>(value[i]);

    if (c < 0x20 && value[i] != '\n' && value[i] != '\t') {
      ++i;
      continue;
    }

    std::size_t charLen = 1;
    if ((c & 0x80) == 0x00) charLen = 1;
    else if ((c & 0xE0) == 0xC0) charLen = 2;
    else if ((c & 0xF0) == 0xE0) charLen = 3;
    else if ((c & 0xF8) == 0xF0) charLen = 4;

    if (i + charLen > value.size()) break;

    const bool needsEllipsis = visible + 1 > max;
    if (needsEllipsis) break;

    output.append(value, i, charLen);
    i += charLen;
    ++visible;
  }

  if (output.size() < value.size() && max > 3) {
    while (!output.empty()) {
      unsigned char last = static_cast<unsigned char>(output.back());
      if ((last & 0xC0) != 0x80) break;
      output.pop_back();
    }
    return output + "...";
  }

  return output;
}

std::string App::typeIcon(StreamType type, bool unicodeIcons) {
  if (unicodeIcons) {
    switch (type) {
      case StreamType::Live: return "📺";
      case StreamType::Movies: return "🎬";
      case StreamType::Series: return "▣";
      case StreamType::Radio: return "♪";
      case StreamType::Favorites: return "★";
    }
  }

  switch (type) {
    case StreamType::Live: return "[TV]";
    case StreamType::Movies: return "[MOV]";
    case StreamType::Series: return "[SER]";
    case StreamType::Radio: return "[RAD]";
    case StreamType::Favorites: return "[*]";
  }
  return "[ ]";
}

std::string App::categoryIcon(const Category &category, bool unicodeIcons) {
  return typeIcon(category.type, unicodeIcons);
}

std::string App::channelIcon(const Channel &channel, bool unicodeIcons) {
  if (!channel.logo.empty()) {
    return unicodeIcons ? "🖼" : "[IMG]";
  }

  return typeIcon(channel.type, unicodeIcons);
}

std::string App::providerLabel(Provider provider) {
  switch (provider) {
    case Provider::M3u: return "M3U";
    case Provider::Xtream: return "Xtream API";
    case Provider::Local: return "Local";
  }
  return "Local";
}

std::string App::screenTitle(ScreenId screen) {
  switch (screen) {
    case ScreenId::Dashboard: return "Dashboard";
    case ScreenId::Playlists: return "Playlists";
    case ScreenId::AddPlaylist: return "Add Playlist";
    case ScreenId::Player: return "Player";
    case ScreenId::Settings: return "Settings";
  }
  return "NSTV";
}

} // namespace nstv
