#include "nstv/app.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

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
}

App::App() : api_(loadConfig()) {
  state_.config = loadConfig();
  api_ = ParserApiClient(state_.config);
  if (loadManifest(state_.manifest)) {
    state_.hasManifest = true;
    state_.message = "Loaded cached manifest: " + std::to_string(state_.manifest.totalChannels) + " channels";
  } else {
    state_.message = "Configure /switch/nstv/config.json, then use Add Playlist.";
  }
}

int App::run() {
  render();
  while (state_.running) {
    Button button = pollButtonBlocking();
    handle(button);
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
      if (button == Button::Back || button == Button::Select) state_.screen = ScreenId::Dashboard;
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

  if (button == Button::Back) {
    if (state_.focus == FocusColumn::Channels) state_.focus = FocusColumn::Categories;
    else if (state_.focus == FocusColumn::Categories) state_.focus = FocusColumn::Types;
    return;
  }

  if (button == Button::Left) {
    if (state_.focus == FocusColumn::Channels) state_.focus = FocusColumn::Categories;
    else if (state_.focus == FocusColumn::Categories) state_.focus = FocusColumn::Types;
    return;
  }

  if (button == Button::Right) {
    if (state_.focus == FocusColumn::Types) state_.focus = FocusColumn::Categories;
    else if (state_.focus == FocusColumn::Categories) state_.focus = FocusColumn::Channels;
    return;
  }

  if (button == Button::Up) {
    if (state_.focus == FocusColumn::Types) { state_.selectedType--; resetLoadedChannels(); }
    else if (state_.focus == FocusColumn::Categories) { state_.selectedCategory--; resetLoadedChannels(); }
    else state_.selectedChannel--;
    return;
  }

  if (button == Button::Down) {
    if (state_.focus == FocusColumn::Types) { state_.selectedType++; resetLoadedChannels(); }
    else if (state_.focus == FocusColumn::Categories) { state_.selectedCategory++; resetLoadedChannels(); }
    else {
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
  if (button == Button::Back) {
    state_.screen = ScreenId::Dashboard;
    return;
  }
  if (button == Button::Up) state_.selectedAddOption--;
  if (button == Button::Down) state_.selectedAddOption++;
  if (state_.selectedAddOption < 0) state_.selectedAddOption = 0;
  if (state_.selectedAddOption > 2) state_.selectedAddOption = 2;

  if (button == Button::Select) {
    if (state_.selectedAddOption == 0) importM3u();
    else if (state_.selectedAddOption == 1) importXtream();
    else state_.screen = ScreenId::Dashboard;
  }
}

void App::importM3u() {
  try {
    if (state_.config.defaultPlaylistUrl.empty()) throw std::runtime_error("defaultPlaylistUrl is empty in config.json");
    state_.message = "Loading M3U manifest...";
    render();
    state_.manifest = api_.loadM3uManifest(state_.config.defaultPlaylistUrl);
    state_.hasManifest = true;
    state_.screen = ScreenId::Dashboard;
    state_.focus = FocusColumn::Types;
    resetLoadedChannels();
    saveManifest(state_.manifest);
    state_.message = "Loaded M3U manifest: " + std::to_string(state_.manifest.totalChannels) + " channels";
  } catch (const std::exception &ex) {
    state_.message = std::string("M3U import failed: ") + ex.what();
  }
}

void App::importXtream() {
  try {
    if (state_.config.defaultXtreamUrl.empty()) throw std::runtime_error("defaultXtreamUrl is empty in config.json");
    state_.message = "Loading Xtream manifest...";
    render();
    state_.manifest = api_.loadXtreamManifest(state_.config.defaultXtreamUrl);
    state_.hasManifest = true;
    state_.screen = ScreenId::Dashboard;
    state_.focus = FocusColumn::Types;
    resetLoadedChannels();
    saveManifest(state_.manifest);
    state_.message = "Loaded Xtream manifest: " + std::to_string(state_.manifest.totalChannels) + " channels";
  } catch (const std::exception &ex) {
    state_.message = std::string("Xtream import failed: ") + ex.what();
  }
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
      state_.manifest.provider,
      category->type,
      category->id,
      page,
      result
    );

    if (!fromCache) {
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

    const std::string origin = fromCache ? "cache" : "API";

    state_.message =
      std::to_string(state_.loadedChannels.size()) + "/" +
      std::to_string(result.totalChannels) +
      " channels loaded from " + origin +
      ", page " + std::to_string(result.page) +
      "/" + std::to_string(result.totalPages);
  } catch (const std::exception &ex) {
    state_.message = std::string("Load channels failed: ") + ex.what();
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
  if (!channel) return;
  state_.screen = ScreenId::Player;
  state_.message = "Selected: " + channel->name;
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

void App::normalizeIndexes() {
  auto types = visibleTypes();
  state_.selectedType = std::clamp(state_.selectedType, 0, std::max(0, static_cast<int>(types.size()) - 1));
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
  switch (state_.screen) {
    case ScreenId::Dashboard: renderDashboardGraphic(); break;
    case ScreenId::AddPlaylist: renderAddPlaylistGraphic(); break;
    case ScreenId::Player: renderPlayerGraphic(); break;
    case ScreenId::Settings: renderSettingsGraphic(); break;
    case ScreenId::Playlists: renderAddPlaylistGraphic(); break;
  }
  gfx_.present();
}

void App::renderDashboard() {
  renderDashboardGraphic();
}

void App::renderDashboardGraphic() {
  normalizeIndexes();

  auto types = visibleTypes();
  std::vector<Category> categories;
  const TypeGroup *type = selectedTypeGroup();
  if (type) categories = type->categories;

  const Channel *selectedChannel = selectedChannelPtr();
  const Category *selectedCategory = selectedCategoryPtr();
  const std::string provider = providerLabel(state_.hasManifest ? state_.manifest.provider : Provider::Local);

  const Color text = rgb(248,250,252);
  const Color textSoft = rgb(218,226,244);
  const Color muted = rgb(150,163,190);
  const Color panelTop = rgb(16,24,45);
  const Color panelBottom = rgb(7,11,22);
  const Color panelBorder = rgb(38,52,82);
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
  gfx_.drawIconBox("live", 24, 18, 54, rgb(239,68,68), rgb(127,29,29), rgb(255,255,255));
  gfx_.drawText("NSTV", 92, 18, 7, text, true);
  gfx_.drawText("IPTV PLAYER", 96, 62, 2, text, true);
  gfx_.fillCircle(984, 43, 6, green);
  gfx_.drawTextRight("ONLINE", 1080, 36, 3, text, true);
  gfx_.drawTextRight("21:45", 1190, 30, 5, text, false);
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

  drawPanel(typesPanel, "STREAM TYPES", "layers", state_.focus == FocusColumn::Types);
  drawPanel(categoriesPanel, "CATEGORIES", "categories", state_.focus == FocusColumn::Categories);
  std::string chTitle = "CHANNELS";
  if (type) chTitle += " (" + type->label + ")";
  drawPanel(channelsPanel, chTitle, "channels", state_.focus == FocusColumn::Channels);
  gfx_.drawTextRight(std::to_string(state_.loadedTotal > 0 ? state_.loadedTotal : (type ? type->totalChannels : 0)) + " CANAIS", channelsPanel.x + channelsPanel.w - 28, channelsPanel.y + 25, 2, muted, false);

  // Stream type cards -------------------------------------------------------
  int typeY = typesPanel.y + 70;
  for (int i=0; i<std::min(4, (int)types.size()); ++i) {
    const auto &t = types[i];
    const bool selected = i == state_.selectedType;
    Color base = typeColor(toString(t.id));
    int y = typeY + i * 74;
    gfx_.fillHorizontalGradient(typesPanel.x + 14, y, typesPanel.w - 28, 64, rgba(base.r,base.g,base.b, selected?110:55), rgba(12,18,34, selected?245:210));
    gfx_.fillRoundRect(typesPanel.x + 14, y, typesPanel.w - 28, 64, 13, rgba(0,0,0,0));
    gfx_.strokeRoundRect(typesPanel.x + 14, y, typesPanel.w - 28, 64, 13, selected ? brightBlue : rgba(255,255,255,18), selected ? 3 : 1);
    gfx_.drawIconBox(toString(t.id), typesPanel.x + 26, y + 10, 44, lighten(base,25), darken(base,30), text);
    gfx_.drawText(Graphics::fitText(t.label, 12), typesPanel.x + 82, y + 15, 3, text, true);
    std::string sub = t.id == StreamType::Live ? "CANAIS AO VIVO" : (t.id == StreamType::Movies ? "FILMES" : (t.id == StreamType::Series ? "SERIES" : "RADIOS"));
    gfx_.drawText(sub, typesPanel.x + 82, y + 42, 2, muted, false);
    if (t.totalChannels > 0) gfx_.drawBadge(std::to_string(t.totalChannels), typesPanel.x + typesPanel.w - 68, y + 21, 42, 24, rgba(30,41,59,210), text);
  }

  int cy = typesPanel.y + typesPanel.h - 74;
  gfx_.fillRoundRect(typesPanel.x + 14, cy, typesPanel.w - 28, 56, 13, rgba(15,23,42,220));
  gfx_.drawIconBox("OK", typesPanel.x+26, cy+9, 38, rgb(22,101,52), rgb(20,83,45), green);
  gfx_.drawText("CONEXAO", typesPanel.x+78, cy+12, 3, text, true);
  gfx_.drawText((provider + " ONLINE"), typesPanel.x+78, cy+37, 2, green, false);

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
    gfx_.strokeRoundRect(categoriesPanel.x + 14, y, categoriesPanel.w - 30, 36, 10, selected ? brightBlue : rgba(255,255,255,18), selected ? 2 : 1);
    gfx_.drawText(Graphics::fitText(c.name, 24), categoriesPanel.x + 32, y + 10, 2, selected ? text : textSoft, true);
    gfx_.drawBadge(std::to_string(c.totalChannels), categoriesPanel.x + categoriesPanel.w - 72, y + 8, 42, 22, rgba(41,54,82,220), text);
  }

  // Channels/movies: no numeric prefix, smaller text, wider panel ----------
  int channelRows = 7;
  int chanStart = windowStart(state_.selectedChannel, (int)state_.loadedChannels.size(), channelRows);
  for (int i=0; i<channelRows; ++i) {
    int index = chanStart + i;
    int y = channelsPanel.y + 66 + i * 55;
    if (index >= (int)state_.loadedChannels.size()) break;
    const auto &ch = state_.loadedChannels[index];
    bool selected = index == state_.selectedChannel;
    gfx_.fillRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? rgba(12,23,52,245) : rgba(10,15,29,215));
    gfx_.strokeRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? brightBlue : rgba(255,255,255,20), selected ? 2 : 1);

    // Prefer the real logo from the API. If it is missing or cannot be decoded, use a small acronym fallback.
    drawLogoOrFallback(ch, channelsPanel.x + 30, y + 7, 48, 35);

    const int nameX = channelsPanel.x + 94;
    gfx_.drawText(Graphics::fitText(ch.name, 35), nameX, y + 9, 3, text, true);
    std::string sub = selectedCategory ? selectedCategory->name : toString(ch.type);
    gfx_.drawText(Graphics::fitText(sub, 42), nameX, y + 32, 2, muted, false);
    gfx_.drawText(state_.favorites.count(ch.id) ? "*" : "<3", channelsPanel.x + channelsPanel.w - 42, y + 15, 2, muted, false);
  }
  if (state_.loadedChannels.empty()) {
    gfx_.drawText("SELECIONE UMA CATEGORIA", channelsPanel.x + 40, channelsPanel.y + 178, 3, muted, false);
    gfx_.drawText("PRESSIONE A PARA CARREGAR", channelsPanel.x + 40, channelsPanel.y + 206, 2, blue, true);
  }

  // Info panel: smaller footer text ----------------------------------------
  Rect info{18, 575, 1245, 88};
  gfx_.fillVerticalGradient(info.x, info.y, info.w, info.h, rgba(27,35,52,235), rgba(13,18,29,235));
  gfx_.fillRoundRect(info.x, info.y, info.w, info.h, 14, rgba(0,0,0,0));
  gfx_.strokeRoundRect(info.x, info.y, info.w, info.h, 14, rgba(255,255,255,35), 1);
  if (selectedChannel) {
    drawLogoOrFallback(*selectedChannel, info.x + 34, info.y + 13, 154, 62);
    gfx_.drawText(Graphics::fitText(selectedChannel->name, 42), info.x + 210, info.y + 18, 2, text, true);
    gfx_.drawText(selectedCategory ? Graphics::fitText(selectedCategory->name, 44) : "", info.x + 210, info.y + 46, 1, muted, false);
  } else {
    gfx_.drawText(state_.hasManifest ? Graphics::fitText(state_.manifest.name, 34) : "NSTV", info.x + 40, info.y + 30, 4, text, true);
  }
  gfx_.drawText("PAGINA " + std::to_string(state_.loadedPage) + " / " + std::to_string(state_.loadedTotalPages), info.x + 610, info.y + 24, 2, text, true);
  gfx_.drawText("CARREGADOS: " + std::to_string(state_.loadedChannels.size()) + " / " + std::to_string(state_.loadedTotal) + " CANAIS", info.x + 610, info.y + 50, 1, blue, true);
  gfx_.drawText(provider + ": " + Graphics::fitText(state_.hasManifest ? state_.manifest.name : "CONFIGURE", 38), info.x + 940, info.y + 24, 2, text, false);
  gfx_.drawText("ATUALIZADO", info.x + 978, info.y + 54, 1, green, false);

  // Controls footer: smaller text ------------------------------------------
  Rect foot{18, 675, 1245, 36};
  gfx_.fillRoundRect(foot.x, foot.y, foot.w, foot.h, 10, rgba(17,24,39,240));
  gfx_.strokeRoundRect(foot.x, foot.y, foot.w, foot.h, 10, rgba(255,255,255,24), 1);
  gfx_.drawText("L NAVEGAR   R TROCAR COLUNA   A SELECIONAR   B VOLTAR   X FAVORITOS   + MENU", foot.x + 26, foot.y + 13, 1, text, true);
}

void App::renderAddPlaylistGraphic() {
  gfx_.drawText("NSTV", 64, 48, 8, rgb(248,250,252), true);
  gfx_.drawText("ADD PLAYLIST", 64, 118, 5, rgb(166,178,207), true);
  const char* opts[] = {"ADD FROM M3U URL", "ADD XTREAM", "BACK"};
  for (int i=0;i<3;i++) {
    int y=190+i*78; bool sel=i==state_.selectedAddOption;
    gfx_.fillRoundRect(120,y,720,58,14, sel ? rgba(37,99,235,220) : rgba(17,24,39,220));
    gfx_.strokeRoundRect(120,y,720,58,14, sel ? rgb(0,191,255) : rgba(255,255,255,30), sel?3:1);
    gfx_.drawText(opts[i],160,y+18,4,rgb(248,250,252),true);
  }
  gfx_.drawText("A SELECT    B BACK", 120, 500, 3, rgb(166,178,207), true);
  gfx_.drawText(Graphics::fitText(state_.message, 80), 120, 545, 3, rgb(0,145,255), false);
}

void App::renderPlayerGraphic() {
  const Channel *channel = selectedChannelPtr();
  gfx_.drawText("NOW PLAYING", 80, 70, 6, rgb(248,250,252), true);
  if (channel) {
    if (!channel->logo.empty()) {
      const Bitmap *bitmap = imageCache_.get(channel->logo);
      if (bitmap && bitmap->valid()) gfx_.drawImage(*bitmap, 80, 150, 260, 150);
      else gfx_.drawLogoFallback(channel->name, 80, 150, 260, 150, 3);
    } else {
      gfx_.drawLogoFallback(channel->name, 80, 150, 260, 150, 3);
    }
    gfx_.drawText(Graphics::fitText(channel->name, 32), 380, 170, 6, rgb(248,250,252), true);
    gfx_.drawText(Graphics::fitText(channel->url, 48), 380, 230, 3, rgb(166,178,207), false);
  }
  gfx_.drawText("PLAYER STUB - B TO RETURN", 80, 620, 4, rgb(166,178,207), true);
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
    case Provider::M3u: return "M3U Parser API";
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
