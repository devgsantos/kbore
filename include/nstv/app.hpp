#pragma once

#include "nstv/config.hpp"
#include "nstv/models.hpp"
#include "nstv/parser_api_client.hpp"
#include "nstv/platform.hpp"
#include "nstv/storage.hpp"
#include "nstv/graphics.hpp"
#include "nstv/image_cache.hpp"
#include "nstv/video_player.hpp"
#include <set>
#include <string>

namespace nstv {

enum class ScreenId { Dashboard, Playlists, AddPlaylist, Player, Settings };
enum class FocusColumn { Types, Categories, Channels };

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

  int selectedType = 0;
  int selectedCategory = 0;
  int selectedChannel = 0;
  int selectedAddOption = 0;

  std::set<std::string> favorites;
  std::string message;
  bool running = true;
};

class App {
public:
  App();
  int run();

private:
  void render();
  void renderDashboard();
  void renderDashboardGraphic();
  void renderAddPlaylistGraphic();
  void renderPlayerGraphic();
  void renderSettingsGraphic();
  void renderAddPlaylist();
  void renderPlayer();
  void renderSettings();
  void handle(Button button);
  void handleDashboard(Button button);
  void handleAddPlaylist(Button button);

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
  VideoPlayer videoPlayer_;
};

} // namespace nstv
