#pragma once

#include "nstv/config.hpp"
#include "nstv/models.hpp"
#include "nstv/parser_api_client.hpp"
#include "nstv/platform.hpp"
#include "nstv/storage.hpp"
#include "nstv/graphics.hpp"
#include "nstv/image_cache.hpp"
#include "nstv/player_backend.hpp"
#include "nstv/player_backend_factory.hpp"
#include "nstv/video_player.hpp"
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <memory>
#include <utility>
#include <vector>

namespace nstv {

enum class ScreenId { Dashboard, Playlists, AddPlaylist, Player, Settings };
enum class FocusColumn { Playlist, Types, Categories, Channels };

struct AppState {
  ScreenId screen = ScreenId::Dashboard;
  FocusColumn focus = FocusColumn::Types;

  Config config;
  Manifest manifest;
  bool hasManifest = false;
  std::vector<Channel> loadedChannels;
  int loadedPage = 1;
  int loadedTotal = 0;
  int loadedTotalPages = 1;
  std::string loadedCategoryKey;
  bool channelListLoading = false;
  std::string channelListLoadingKey;

  std::map<std::string, EpgPage> epgByChannel;
  std::string currentEpgKey;
  bool currentEpgAvailable = false;

  int selectedType = 0;
  int selectedCategory = 0;
  int selectedChannel = 0;
  int selectedAddOption = 0;

  std::set<std::string> favorites;
  std::string message;
  bool loading = false;
  std::string loadingMessage;
  bool running = true;

  long long playerOverlayUntilMs = 0;
  bool playerStarted = false;
  bool playerFrameSeen = false;
  bool playerLoading = false;
  bool playerLoadFailed = false;
  std::string playerErrorMessage;
};

class App {
public:
  App();
  ~App();
  int run();

private:
  struct EpgJob {
    Config config;
    Channel channel;
    std::string key;
    std::string manifestId;
    std::string source;
    Provider provider = Provider::M3u;
    std::string manualEpgUrl;
    int pageSize = 4;
  };

  struct EpgResult {
    std::string manifestId;
    std::string key;
    EpgPage page;
  };

  struct ChannelLoadJob {
    Config config;
    std::string manifestId;
    std::string source;
    Provider provider = Provider::M3u;
    StreamType type = StreamType::Live;
    std::string categoryId;
    std::string categoryKey;
    int page = 1;
    bool append = false;
  };

  struct ChannelLoadResult {
    std::string manifestId;
    std::string categoryKey;
    StreamType type = StreamType::Live;
    std::string categoryId;
    int page = 1;
    bool append = false;
    bool ok = false;
    ChannelPage pageData;
    std::string error;
  };

  void render();
  void renderSplashGraphic();
  void renderDashboard();
  void renderDashboardGraphic();
  void renderAddPlaylistGraphic();
  void renderPlayerGraphic();
  void renderSettingsGraphic();
  void renderLoadingOverlay(const std::string &message);
  void renderAddPlaylist();
  void renderPlayer();
  void renderSettings();
  void handle(Button button);
  void handleDashboard(Button button);
  void handleAddPlaylist(Button button);

  const PlaylistConfig *activePlaylist() const;
  std::string activePlaylistName() const;
  bool loadCachedPlaylist(const PlaylistConfig &playlist);
  void activatePlaylist(int index);
  void importPlaylist(const PlaylistConfig &playlist);
  void addM3uPlaylist();
  void addXtreamPlaylist();
  void deletePlaylist(int index);

  void importM3u();
  void importXtream();
  void loadCategory(bool append = false);
  void playSelectedChannel();
  void resetLoadedChannels();
  void normalizeIndexes();
  void maybePreloadNextPage();
  void loadSelectedEpg(bool force = false, bool fetchRemote = true);
  void loadEpgForChannels(const std::vector<Channel> &channels);
  void loadVisibleEpgForChannelList();
  void startChannelWorker();
  void stopChannelWorker();
  void channelWorkerLoop();
  void drainFinishedChannelLoads();
  void startEpgWorker();
  void stopEpgWorker();
  void epgWorkerLoop();
  void drainFinishedEpg();
  std::string channelEpgKey(const Channel &channel) const;
  std::string epgLineForChannel(const Channel &channel) const;
  std::string epgNowNextLine(const Channel &channel) const;

  std::vector<TypeGroup> visibleTypes() const;
  const TypeGroup *selectedTypeGroup() const;
  const Category *selectedCategoryPtr() const;
  const Channel *selectedChannelPtr() const;

  template <typename T, typename LabelFn>
  std::vector<std::string> buildWindowRows(
    const std::vector<T> &items,
    int selected,
    int width,
    int maxRows,
    LabelFn labelFn
  ) const;

  template <typename T, typename LabelFn>
  void printWindow(const std::string &title, const std::vector<T> &items, int selected, int width, LabelFn labelFn) const;

  static std::string crop(const std::string &value, std::size_t max);
  static std::string typeIcon(StreamType type, bool unicodeIcons);
  static std::string categoryIcon(const Category &category, bool unicodeIcons);
  static std::string channelIcon(const Channel &channel, bool unicodeIcons);
  static std::string providerLabel(Provider provider);
  static std::string screenTitle(ScreenId screen);

  ParserApiClient api_;
  AppState state_;
  Graphics gfx_;
  ImageCache imageCache_;
  std::unique_ptr<IPlayerBackend> player_;
  std::thread channelWorker_;
  std::mutex channelMutex_;
  std::condition_variable channelCv_;
  std::vector<ChannelLoadJob> channelQueue_;
  std::set<std::string> channelQueuedKeys_;
  std::vector<ChannelLoadResult> channelFinished_;
  std::atomic<bool> channelStop_{false};
  std::thread epgWorker_;
  std::mutex epgMutex_;
  std::condition_variable epgCv_;
  std::vector<EpgJob> epgQueue_;
  std::set<std::string> epgQueuedKeys_;
  std::vector<EpgResult> epgFinished_;
  std::atomic<bool> epgStop_{false};

  bool splashVisible_ = true;
  long long splashStartedAtMs_ = 0;
  long long splashDurationMs_ = 1800;
};

} // namespace nstv
