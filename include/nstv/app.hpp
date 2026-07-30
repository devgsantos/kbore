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
#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <memory>
#include <utility>
#include <vector>
#include <mutex>
#include <thread>

namespace nstv {

enum class ScreenId { Dashboard, Playlists, AddPlaylist, Player, Settings, Parental };
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
  bool channelGridView = false;

  std::map<std::string, EpgPage> epgByChannel;
  std::map<std::string, long long> epgRefreshAttemptMsByKey;
  std::string currentEpgKey;
  bool currentEpgAvailable = false;
  std::map<std::string, VodDetails> vodDetailsByKey;
  std::set<std::string> vodDetailsAttemptedKeys;
  std::string currentVodDetailsKey;

  // Dynamic tree navigation. When manifest.nodes is available, the dashboard
  // behaves as a three-column tree browser:
  //   roots -> current folder children -> selected child preview/items.
  std::vector<int> nodePath;

  int selectedType = 0;
  int selectedCategory = 0;
  int selectedChannel = 0;
  int selectedAddOption = 0;
  int settingsScroll = 0;
  int selectedSettingsOption = 0;
  int selectedParentalType = 0;
  int selectedParentalCategory = 0;
  bool parentalUnlocked = false;
  std::string parentalDeniedKey;

  std::set<std::string> favoriteIds;
  std::vector<Channel> favoriteChannels;
  MediaNode favoritesRootNode;
  TypeGroup favoritesTypeGroup;
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
  bool hasPlaybackChannel = false;
  Channel playbackChannel;
  long long playbackStartedAtMs = 0;
  long long lastPlaybackInputMs = 0;
  long long lastSleepKeepAliveMs = 0;
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
    int pageSize = 48;
    int epgOffsetMinutes = 0;
  };

  struct EpgResult {
    std::string manifestId;
    std::string key;
    Channel channel;
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

  struct VodDetailsJob {
    Config config;
    std::string manifestId;
    std::string source;
    Provider provider = Provider::M3u;
    Channel channel;
    std::string key;
    std::string language = "pt-BR";
  };

  struct VodDetailsResult {
    std::string manifestId;
    std::string key;
    Channel channel;
    bool ok = false;
    VodDetails details;
    std::string error;
  };

  struct NodeSelection {
    int selectedType = 0;
    std::vector<int> nodePath;
    int selectedCategory = 0;
    int selectedChannel = 0;
    FocusColumn focus = FocusColumn::Channels;
  };

  void render();
  void renderSplashGraphic();
  void renderDashboard();
  void renderDashboardGraphic();
  void renderAddPlaylistGraphic();
  void renderPlayerGraphic();
  void renderSettingsGraphic();
  void renderParentalGraphic();
  void renderLoadingOverlay(const std::string &message);
  void renderAddPlaylist();
  void renderPlayer();
  void renderSettings();
  void handle(Button button);
  void handleInput(const InputEvent &event);
  void handleTouchTap(int x, int y);
  void handleDashboardTouchTap(int x, int y);
  void handleDashboardTouchDrag(int x, int y);
  void handleAddPlaylistTouchTap(int x, int y);
  void handleSettingsTouchTap(int x, int y);
  void handleParentalTouchTap(int x, int y);
  void handleSecondaryTouchDrag(int x, int y);
  void handlePlayerTouchTap(int x, int y);
  void handlePlayerTouchDrag(int x, int y);
  void handleDashboard(Button button);
  void handleAddPlaylist(Button button);
  void handleParental(Button button);

  bool touchActive_ = false;
  bool touchDragging_ = false;
  int touchFingerId_ = -1;
  int touchStartX_ = 0;
  int touchStartY_ = 0;
  int touchLastY_ = 0;
  bool playerTouchOverlayWasVisible_ = false;
  long long playerTouchLastSeekMs_ = 0;

  const PlaylistConfig *activePlaylist() const;
  std::string activePlaylistName() const;
  bool loadCachedPlaylist(const PlaylistConfig &playlist);
  void activatePlaylist(int index);
  void importPlaylist(const PlaylistConfig &playlist);
  void startPlaylistLoad(const PlaylistConfig &playlist, bool forceRefresh = false);
  void updatePlaylistLoad();
  bool playlistLoadActive() const;

  void updateCacheSave();
  bool cacheSaveActive() const;

  void addM3uPlaylist();
  void addXtreamPlaylist();
  void deletePlaylist(int index);

  void importM3u();
  void importXtream();
  void loadCategory(bool append = false);
  void playSelectedChannel();
  void openChannel(const Channel &channel);
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
  std::string vodDetailsKeyForChannel(const Channel &channel) const;
  const VodDetails *cachedVodDetailsForChannel(const Channel &channel) const;
  void requestVodDetailsForChannel(const Channel &channel);
  void updateVodDetailsLoad();
  std::string channelEpgKey(const Channel &channel) const;
  std::vector<std::string> channelEpgKeys(const Channel &channel) const;
  const EpgPage *cachedEpgForChannel(const Channel &channel) const;
  std::string epgLineForChannel(const Channel &channel) const;
  std::string epgNowNextLine(const Channel &channel) const;
  void applyPlaybackSleepPolicy();
  void resetPlaybackSleepTimers();
  std::string playbackSleepWarningText(bool compact = false) const;

  std::vector<TypeGroup> visibleTypes() const;
  const TypeGroup *selectedTypeGroup() const;
  std::vector<Category> visibleCategoriesForSelectedType() const;
  const Category *selectedCategoryPtr() const;
  const Channel *selectedChannelPtr() const;

  bool usingNodeTree() const;
  const MediaNode *selectedRootNode() const;
  MediaNode *selectedRootNode();
  const MediaNode *nodeAtPath(const MediaNode *root, const std::vector<int> &path) const;
  MediaNode *nodeAtPath(MediaNode *root, const std::vector<int> &path);
  const MediaNode *currentNodeParent() const;
  MediaNode *currentNodeParent();
  const MediaNode *selectedCurrentNode() const;
  MediaNode *selectedCurrentNode();
  const MediaNode *selectedPreviewNode() const;
  MediaNode *selectedPreviewNode();
  std::vector<const MediaNode *> currentNodeChildren() const;
  std::vector<const MediaNode *> previewNodeChildren() const;
  std::string parentalKeyForCategory(StreamType type, const std::string &id, const std::string &name) const;
  std::string parentalKeyForCategory(const Category &category) const;
  std::string parentalKeyForNode(const MediaNode &node) const;
  ParentalRule parentalRuleForKey(const std::string &key) const;
  bool isParentalHidden(const std::string &key) const;
  bool isParentalLocked(const std::string &key) const;
  bool requestParentalUnlock(const std::string &key, const std::string &title);
  bool verifyParentalPin(const std::string &title, int maxAttempts = 3);
  void changeParentalPin();
  std::vector<const MediaNode *> filteredNodeChildren(const MediaNode *parent) const;
  std::vector<Category> parentalCategoriesForType(StreamType type) const;
  bool currentNodeChildrenAreItems() const;
  bool ensureNodeChildrenLoaded(MediaNode &node);
  Channel channelFromNode(const MediaNode &node) const;
  bool resolveNodeSelectionForChannel(const Channel &channel, NodeSelection &selection) const;
  void applyNodeSelection(const NodeSelection &selection);
  void enterNode(const MediaNode &node, int childIndex);
  void playNode(const MediaNode &node);
  void loadFavoritesForActivePlaylist();
  void rebuildFavoritesNode();
  std::string favoriteIdForChannel(const Channel &channel) const;
  bool isFavorite(const Channel &channel) const;
  bool toggleFavorite(const Channel &channel);
  bool selectedTypeIsFavorites() const;
  bool favoritesRootSelected() const;
  std::string breadcrumbText() const;

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

  std::thread vodDetailsThread_;
  mutable std::mutex vodDetailsMutex_;
  bool vodDetailsActive_ = false;
  bool vodDetailsDone_ = false;
  VodDetailsJob vodDetailsJob_;
  VodDetailsResult vodDetailsResult_;

  std::thread playlistLoadThread_;
  mutable std::mutex playlistLoadMutex_;
  bool playlistLoadActive_ = false;
  bool playlistLoadDone_ = false;
  bool playlistLoadSuccess_ = false;
  bool playlistLoadForceRefresh_ = false;
  PlaylistConfig playlistLoadPlaylist_;
  Manifest playlistLoadManifest_;
  std::string playlistLoadMessage_;
  std::string playlistLoadError_;

  std::thread cacheSaveThread_;
  mutable std::mutex cacheSaveMutex_;
  bool cacheSaveActive_ = false;
  bool cacheSaveDone_ = false;
  bool cacheSaveSuccess_ = false;
  std::string cacheSavePlaylistId_;
  std::string cacheSaveError_;
  std::size_t cacheSaveWritten_ = 0;
  std::size_t cacheSaveTotal_ = 0;

  bool splashVisible_ = true;
  long long splashStartedAtMs_ = 0;
  long long splashDurationMs_ = 1800;
};

} // namespace nstv
