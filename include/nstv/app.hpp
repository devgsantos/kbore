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
#include <cstddef>
#include <set>
#include <string>
#include <memory>
#include <mutex>
#include <thread>

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

  // Dynamic tree navigation. When manifest.nodes is available, the dashboard
  // behaves as a three-column tree browser:
  //   roots -> current folder children -> selected child preview/items.
  std::vector<int> nodePath;

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
  void resetLoadedChannels();
  void normalizeIndexes();
  void maybePreloadNextPage();

  std::vector<TypeGroup> visibleTypes() const;
  const TypeGroup *selectedTypeGroup() const;
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
  bool ensureNodeChildrenLoaded(MediaNode &node);
  Channel channelFromNode(const MediaNode &node) const;
  void enterNode(const MediaNode &node, int childIndex);
  void playNode(const MediaNode &node);
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
