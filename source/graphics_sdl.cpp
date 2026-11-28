#ifdef NSTV_USE_SDL

#include "nstv/graphics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#ifdef NSTV_USE_SDL_IMAGE
#include <SDL2/SDL_image.h>
#endif
#ifdef NSTV_USE_SDL_TTF
#include <SDL2/SDL_ttf.h>
#endif

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace nstv {
namespace {
SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
bool g_sdlReady = false;
bool g_nativeVideoSuspended = false;

#ifdef __SWITCH__
bool g_romfsReady = false;
#endif

#ifdef NSTV_USE_SDL_TTF
std::map<std::string, TTF_Font *> g_fontCache;
bool g_ttfReady = false;
#endif

SDL_Color toSDL(Color c) { return SDL_Color{c.r, c.g, c.b, c.a}; }

void setDraw(Color c) {
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
}

Color mix(Color a, Color b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  return Color{
    static_cast<uint8_t>(a.r + (b.r - a.r) * t),
    static_cast<uint8_t>(a.g + (b.g - a.g) * t),
    static_cast<uint8_t>(a.b + (b.b - a.b) * t),
    static_cast<uint8_t>(a.a + (b.a - a.a) * t)
  };
}

std::string normalizeAscii(const std::string &input) {
  // Normalize display text for Switch UI:
  // - remove emoji, variation selectors and decorative symbols that break rendering
  // - transliterate common Latin accents to ASCII
  // - keep only safe printable ASCII punctuation/text
  std::string out;
  out.reserve(input.size());

  auto appendAscii = [&](char c) {
    if (c < 0x20 && c != '\n' && c != '\t') return;
    out.push_back(c);
  };

  for (std::size_t i = 0; i < input.size();) {
    unsigned char c = static_cast<unsigned char>(input[i]);

    if (c < 0x80) {
      appendAscii(static_cast<char>(c));
      ++i;
      continue;
    }

    uint32_t cp = 0;
    std::size_t len = 0;

    if ((c & 0xE0) == 0xC0 && i + 1 < input.size()) {
      cp = ((c & 0x1F) << 6) |
           (static_cast<unsigned char>(input[i + 1]) & 0x3F);
      len = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < input.size()) {
      cp = ((c & 0x0F) << 12) |
           ((static_cast<unsigned char>(input[i + 1]) & 0x3F) << 6) |
           (static_cast<unsigned char>(input[i + 2]) & 0x3F);
      len = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < input.size()) {
      cp = ((c & 0x07) << 18) |
           ((static_cast<unsigned char>(input[i + 1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(input[i + 2]) & 0x3F) << 6) |
           (static_cast<unsigned char>(input[i + 3]) & 0x3F);
      len = 4;
    } else {
      ++i;
      continue;
    }

    switch (cp) {
      case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
      case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
        out.push_back('A'); break;
      case 0x00C7: case 0x00E7:
        out.push_back('C'); break;
      case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
      case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
        out.push_back('E'); break;
      case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
      case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
        out.push_back('I'); break;
      case 0x00D1: case 0x00F1:
        out.push_back('N'); break;
      case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6:
      case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6:
        out.push_back('O'); break;
      case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
      case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
        out.push_back('U'); break;
      default:
        // Drop symbols/emoji/dingbats/variation selectors.
        break;
    }

    i += len;
  }

  // Collapse repeated whitespace caused by icon removal.
  std::string cleaned;
  cleaned.reserve(out.size());
  bool previousSpace = false;

  for (char ch : out) {
    bool isSpace = ch == ' ' || ch == '\t' || ch == '\n';

    if (isSpace) {
      if (!previousSpace && !cleaned.empty()) {
        cleaned.push_back(' ');
      }
      previousSpace = true;
      continue;
    }

    cleaned.push_back(ch);
    previousSpace = false;
  }

  while (!cleaned.empty() && cleaned.back() == ' ') {
    cleaned.pop_back();
  }

  return cleaned;
}

std::array<uint8_t, 7> fallbackGlyph(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');

  switch (ch) {
    case 'A': return {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'B': return {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
    case 'C': return {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F};
    case 'D': return {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
    case 'E': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
    case 'F': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    case 'G': return {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F};
    case 'H': return {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'I': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
    case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0E};
    case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
    case 'M': return {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    case 'O': return {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'P': return {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    case 'Q': return {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
    case 'R': return {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
    case 'S': return {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    case 'T': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
    case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'V': return {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04};
    case 'W': return {0x11,0x11,0x11,0x15,0x15,0x1B,0x11};
    case 'X': return {0x11,0x0A,0x04,0x04,0x04,0x0A,0x11};
    case 'Y': return {0x11,0x0A,0x04,0x04,0x04,0x04,0x04};
    case 'Z': return {0x1F,0x02,0x04,0x08,0x10,0x10,0x1F};

    case '0': return {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
    case '1': return {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
    case '2': return {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
    case '3': return {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
    case '4': return {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
    case '5': return {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
    case '6': return {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
    case '7': return {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
    case '8': return {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
    case '9': return {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};

    case ':': return {0x00,0x04,0x04,0x00,0x04,0x04,0x00};
    case '.': return {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
    case ',': return {0x00,0x00,0x00,0x00,0x04,0x04,0x08};
    case '-': return {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    case '_': return {0x00,0x00,0x00,0x00,0x00,0x00,0x1F};
    case '/': return {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    case '|': return {0x04,0x04,0x04,0x04,0x04,0x04,0x04};
    case '+': return {0x00,0x04,0x04,0x1F,0x04,0x04,0x00};
    case '*': return {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00};
    case '<': return {0x02,0x04,0x08,0x10,0x08,0x04,0x02};
    case '>': return {0x08,0x04,0x02,0x01,0x02,0x04,0x08};
    case '(': return {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    case ')': return {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    case '[': return {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E};
    case ']': return {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E};
    case '!': return {0x04,0x04,0x04,0x04,0x04,0x00,0x04};
    case '?': return {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};
    case '#': return {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00};
    case '%': return {0x19,0x19,0x02,0x04,0x08,0x13,0x13};
    case '&': return {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D};
    case ' ': return {0x00,0x00,0x00,0x00,0x00,0x00,0x00};

    default: return {0x1F,0x11,0x05,0x02,0x05,0x11,0x1F};
  }
}

const char *firstExistingFont() {
  static std::vector<std::string> paths = {
#ifdef __SWITCH__
    "romfs:/fonts/OpenSans-Regular.ttf",
    "romfs:/fonts/OpenSans-SemiBold.ttf",
    "romfs:/fonts/Roboto-Regular.ttf",
    "romfs:/fonts/Roboto-Medium.ttf",
    "romfs:/fonts/DejaVuSans.ttf",

    "sdmc:/switch/nstv-native/fonts/OpenSans-Regular.ttf",
    "sdmc:/switch/nstv-native/fonts/OpenSans-SemiBold.ttf",
    "sdmc:/switch/nstv-native/fonts/Roboto-Regular.ttf",
    "sdmc:/switch/nstv-native/fonts/Roboto-Medium.ttf",
    "sdmc:/switch/nstv-native/fonts/DejaVuSans.ttf",

    "sdmc:/switch/kbore/fonts/OpenSans-Regular.ttf",
    "sdmc:/switch/kbore/fonts/OpenSans-SemiBold.ttf",
    "sdmc:/switch/kbore/fonts/Roboto-Regular.ttf",
    "sdmc:/switch/kbore/fonts/Roboto-Medium.ttf",
    "sdmc:/switch/kbore/fonts/DejaVuSans.ttf",
#else
    "./romfs/fonts/OpenSans-Regular.ttf",
    "./romfs/fonts/OpenSans-SemiBold.ttf",
    "./romfs/fonts/Roboto-Regular.ttf",
    "./romfs/fonts/Roboto-Medium.ttf",
    "./fonts/OpenSans-Regular.ttf",
    "./fonts/OpenSans-SemiBold.ttf",
    "./fonts/Roboto-Regular.ttf",
    "./fonts/Roboto-Medium.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
#endif
  };

  // Probed once: this runs for every text draw, and used to open the font file (and log) each time.
  static bool probed = false;
  static const char *found = nullptr;
  if (probed) {
    return found;
  }
  probed = true;

  for (const auto &path : paths) {
    FILE *fp = std::fopen(path.c_str(), "rb");

    if (fp) {
      std::fclose(fp);
      std::printf("[NSTV] Found font: %s\n", path.c_str());
      found = path.c_str();
      return found;
    }
  }

  std::printf("[NSTV] No font found\n");
  return nullptr;
}

#ifdef NSTV_USE_SDL_TTF
TTF_Font *fontForPointSize(int size, bool bold);

TTF_Font *fontForScale(int scale, bool bold) {
  // Map legacy scale values to TV-friendly point sizes.
  int size = 12;
  switch (scale) {
    case 1: size = 12; break;
    case 2: size = 16; break;
    case 3: size = 22; break;
    case 4: size = 28; break;
    case 5: size = 34; break;
    case 6: size = 42; break;
    default: size = std::max(10, scale * 7); break;
  }
  return fontForPointSize(size, bold);
}

TTF_Font *fontForPointSize(int size, bool bold) {
  if (!g_ttfReady) {
    std::printf("[NSTV] SDL_ttf is not ready\n");
    return nullptr;
  }

  const char *fontPath = firstExistingFont();

  if (!fontPath) {
    std::printf("[NSTV] No TTF font path available\n");
    return nullptr;
  }

  size = std::clamp(size, 8, 96);
  std::string key = std::string(fontPath) + ":" + std::to_string(size) + (bold ? ":bold" : ":regular");
  auto found = g_fontCache.find(key);
  if (found != g_fontCache.end()) return found->second;

  TTF_Font *font = TTF_OpenFont(fontPath, size);

  if (!font) {
    std::printf("[NSTV] TTF_OpenFont failed: %s | path=%s\n", TTF_GetError(), fontPath);
    return nullptr;
  }

  if (bold) {
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);
  }

  std::printf("[NSTV] Loaded font: %s size=%d bold=%d\n", fontPath, size, bold ? 1 : 0);

  g_fontCache[key] = font;
  return font;
}
#endif

struct CachedTexture {
  SDL_Texture *texture = nullptr;
  int width = 0;
  int height = 0;
  Uint32 format = 0;
};

std::map<const uint8_t *, CachedTexture> g_textureCache;
std::map<std::string, CachedTexture> g_fileTextureCache;

// ---------------------------------------------------------------------------------------------
// Frame-scoped GPU caches.
//
// The UI redraws everything every frame. Rasterising text with SDL_ttf, re-uploading logo
// pixels and drawing rounded shapes/gradients line by line used to dominate frame time on the
// Switch. These caches keep the resulting textures and evict entries that have not been drawn
// recently, so memory stays bounded while the visible frame is served almost entirely from GPU.
// ---------------------------------------------------------------------------------------------
uint64_t g_frameIndex = 0;

struct LruTexture {
  SDL_Texture *texture = nullptr;
  int width = 0;
  int height = 0;
  uint64_t lastUsedFrame = 0;
};

using LruTextureMap = std::unordered_map<std::string, LruTexture>;

LruTextureMap g_textTextureCache;   // key: font pointer + raw text; white glyphs, tinted per draw
LruTextureMap g_shapeTextureCache;  // key: shape kind + geometry + colour
std::unordered_map<uint64_t, LruTexture> g_bitmapTextureCache;  // key: Bitmap::id
std::unordered_map<std::string, int> g_textWidthCache;

constexpr std::size_t kMaxTextTextures = 600;
constexpr std::size_t kMaxShapeTextures = 400;
constexpr std::size_t kMaxBitmapTextures = 160;
constexpr std::size_t kMaxTextWidths = 4096;
// Entries untouched for this many frames (~2 s at 60 fps) are evicted first when over budget.
constexpr uint64_t kStaleFrames = 120;

template <typename Map>
void destroyAllTextures(Map &map) {
  for (auto &item : map) {
    if (item.second.texture) {
      SDL_DestroyTexture(item.second.texture);
    }
  }
  map.clear();
}

template <typename Map>
void evictTextures(Map &map, std::size_t maxEntries) {
  if (map.size() <= maxEntries) {
    return;
  }

  const uint64_t keepFrom = g_frameIndex > kStaleFrames ? g_frameIndex - kStaleFrames : 0;
  for (auto it = map.begin(); it != map.end();) {
    if (it->second.lastUsedFrame < keepFrom) {
      if (it->second.texture) SDL_DestroyTexture(it->second.texture);
      it = map.erase(it);
    } else {
      ++it;
    }
  }

  // Everything is recent (a very busy screen): drop least recently used until within budget.
  while (map.size() > maxEntries) {
    auto oldest = map.begin();
    for (auto it = map.begin(); it != map.end(); ++it) {
      if (it->second.lastUsedFrame < oldest->second.lastUsedFrame) oldest = it;
    }
    if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
    map.erase(oldest);
  }
}

void clearFrameCaches() {
  destroyAllTextures(g_textTextureCache);
  destroyAllTextures(g_shapeTextureCache);
  destroyAllTextures(g_bitmapTextureCache);
  g_textWidthCache.clear();
}

// Creates a static RGBA texture from CPU pixels (non-premultiplied, blended like draw calls).
SDL_Texture *createStaticTexture(int w, int h, const std::vector<uint8_t> &rgbaPixels) {
  SDL_Texture *texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!texture) {
    return nullptr;
  }
  SDL_UpdateTexture(texture, nullptr, rgbaPixels.data(), w * 4);
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  return texture;
}

std::string colorKey(Color c) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
  return buffer;
}

void putRgba(std::vector<uint8_t> &pixels, int index, Color c) {
  pixels[static_cast<std::size_t>(index) * 4 + 0] = c.r;
  pixels[static_cast<std::size_t>(index) * 4 + 1] = c.g;
  pixels[static_cast<std::size_t>(index) * 4 + 2] = c.b;
  pixels[static_cast<std::size_t>(index) * 4 + 3] = c.a;
}

// Inside test for a rounded rectangle matching the previous scanline-based drawing.
bool insideRoundRect(int px, int py, int w, int h, int radius) {
  if (px < 0 || py < 0 || px >= w || py >= h) return false;
  if (radius <= 0) return true;
  int cy;
  if (py < radius) {
    cy = radius;
  } else if (py >= h - radius) {
    cy = h - radius - 1;
  } else {
    return true;
  }
  const int dy = py - cy;
  const int dx = static_cast<int>(std::sqrt(std::max(0, radius * radius - dy * dy)));
  return px >= radius - dx && px < w - radius + dx;
}

// Looks up or builds a cached shape texture, then draws it at (x, y) with size (w, h).
template <typename Builder>
void drawCachedShape(const std::string &key, int x, int y, int w, int h, Builder build) {
  if (!g_renderer || w <= 0 || h <= 0) return;
  auto found = g_shapeTextureCache.find(key);
  if (found == g_shapeTextureCache.end()) {
    int texW = 0;
    int texH = 0;
    std::vector<uint8_t> pixels;
    build(texW, texH, pixels);
    LruTexture entry;
    entry.texture = texW > 0 && texH > 0 ? createStaticTexture(texW, texH, pixels) : nullptr;
    entry.width = texW;
    entry.height = texH;
    found = g_shapeTextureCache.emplace(key, entry).first;
  }
  found->second.lastUsedFrame = g_frameIndex;
  if (!found->second.texture) return;
  SDL_Rect dst{x, y, w, h};
  SDL_RenderCopy(g_renderer, found->second.texture, nullptr, &dst);
}

void clearTextureCaches() {
  clearFrameCaches();
  for (auto &item : g_textureCache) {
    if (item.second.texture) {
      SDL_DestroyTexture(item.second.texture);
    }
  }
  g_textureCache.clear();

  for (auto &item : g_fileTextureCache) {
    if (item.second.texture) {
      SDL_DestroyTexture(item.second.texture);
    }
  }
  g_fileTextureCache.clear();
}

void closeFonts() {
#ifdef NSTV_USE_SDL_TTF
  for (auto &item : g_fontCache) {
    if (item.second) {
      TTF_CloseFont(item.second);
    }
  }
  g_fontCache.clear();
#endif
}

#ifdef NSTV_USE_SDL_IMAGE
SDL_Texture *loadTextureFromFile(const std::string &path, int &outW, int &outH) {
  auto found = g_fileTextureCache.find(path);

  if (found != g_fileTextureCache.end() && found->second.texture) {
    outW = found->second.width;
    outH = found->second.height;
    return found->second.texture;
  }

  SDL_Surface *surface = IMG_Load(path.c_str());

  if (!surface) {
    std::printf("[NSTV] IMG_Load failed: %s | path=%s\n", IMG_GetError(), path.c_str());
    return nullptr;
  }

  SDL_Texture *texture = SDL_CreateTextureFromSurface(g_renderer, surface);

  if (!texture) {
    std::printf("[NSTV] SDL_CreateTextureFromSurface failed: %s | path=%s\n", SDL_GetError(), path.c_str());
    SDL_FreeSurface(surface);
    return nullptr;
  }

  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

  CachedTexture cached;
  cached.texture = texture;
  cached.width = surface->w;
  cached.height = surface->h;

  outW = surface->w;
  outH = surface->h;

  SDL_FreeSurface(surface);

  g_fileTextureCache[path] = cached;

  return texture;
}
#endif

} // namespace

Color rgb(uint8_t r, uint8_t g, uint8_t b) { return Color{r,g,b,255}; }
Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return Color{r,g,b,a}; }
Color lighten(Color c, int a) { return Color{uint8_t(std::min(255, c.r+a)), uint8_t(std::min(255, c.g+a)), uint8_t(std::min(255, c.b+a)), c.a}; }
Color darken(Color c, int a) { return Color{uint8_t(std::max(0, c.r-a)), uint8_t(std::max(0, c.g-a)), uint8_t(std::max(0, c.b-a)), c.a}; }
Color typeColor(const std::string &type) {
  if (type == "movies") return rgb(126, 34, 206);
  if (type == "series") return rgb(13, 148, 136);
  if (type == "radio") return rgb(194, 65, 12);
  if (type == "favorites") return rgb(20, 132, 255);
  return rgb(37, 99, 235);
}

Graphics::Graphics() {
  if (g_sdlReady) return;

  #ifdef __SWITCH__
    if (!g_romfsReady) {
      Result rc = romfsInit();

      if (R_SUCCEEDED(rc)) {
        g_romfsReady = true;
        std::printf("[NSTV] RomFS initialized\n");
      } else {
        std::printf("[NSTV] romfsInit failed: 0x%x\n", rc);
      }
    }
  #endif

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO);

#ifdef NSTV_USE_SDL_IMAGE
  int imgFlags = IMG_INIT_PNG;

  if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
    std::printf("[NSTV] IMG_Init PNG failed: %s\n", IMG_GetError());
  }
#endif

#ifdef NSTV_USE_SDL_TTF
  if (TTF_Init() == 0) g_ttfReady = true;
#endif
  g_window = SDL_CreateWindow(
    "NSTV",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    Width,
    Height,
    SDL_WINDOW_SHOWN
  );
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
#ifdef __SWITCH__
  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
#else
  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!g_renderer) {
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
  }
#endif
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  g_sdlReady = true;
  g_nativeVideoSuspended = false;
}

void Graphics::suspendForNativeVideo() {
#ifdef __SWITCH__
  if (g_nativeVideoSuspended) {
    return;
  }

  clearTextureCaches();
  closeFonts();

  if (g_renderer) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }

  if (g_window) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }

  g_sdlReady = false;
  g_nativeVideoSuspended = true;
  std::printf("[NSTV] SDL video suspended for native deko3d playback\n");
#endif
}

void Graphics::resumeAfterNativeVideo() {
#ifdef __SWITCH__
  if (!g_nativeVideoSuspended) {
    return;
  }

  g_nativeVideoSuspended = false;
  g_sdlReady = false;
  Graphics();
  std::printf("[NSTV] SDL video resumed after native deko3d playback\n");
#endif
}

bool Graphics::isSuspendedForNativeVideo() const {
  return g_nativeVideoSuspended;
}

uint64_t Graphics::nextBitmapId() {
  static std::atomic<uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

void Graphics::beginFrame(Color color) {
  if (!g_renderer) return;
  ++g_frameIndex;
  evictTextures(g_textTextureCache, kMaxTextTextures);
  evictTextures(g_shapeTextureCache, kMaxShapeTextures);
  evictTextures(g_bitmapTextureCache, kMaxBitmapTextures);
  if (g_textWidthCache.size() > kMaxTextWidths) g_textWidthCache.clear();
  setDraw(color);
  SDL_RenderClear(g_renderer);
  fillVerticalGradient(0, 0, Width, Height, rgb(6, 9, 18), rgb(2, 5, 11));
  fillHorizontalGradient(0, 0, Width, 120, rgba(11, 19, 42, 170), rgba(2, 5, 16, 0));
}

void Graphics::present() {
  if (!g_renderer) return;
  SDL_RenderPresent(g_renderer);
#ifndef __SWITCH__
  // Host preview screenshot for quick visual validation.
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, Width, Height, 32, SDL_PIXELFORMAT_RGBA32);
  if (surface) {
    SDL_RenderReadPixels(g_renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch);
    SDL_SaveBMP(surface, "nstv-frame.bmp");
    SDL_FreeSurface(surface);
  }
#endif
}

void Graphics::putPixel(int x, int y, Color color) {
  if (!g_renderer) return;
  if (x < 0 || y < 0 || x >= Width || y >= Height) return;
  setDraw(color);
  SDL_RenderDrawPoint(g_renderer, x, y);
}

void Graphics::blendPixel(int x, int y, Color color) { putPixel(x, y, color); }

void Graphics::fillRect(int x, int y, int w, int h, Color color) {
  if (!g_renderer || w <= 0 || h <= 0 || color.a == 0) return;
  setDraw(color);
  SDL_Rect r{x,y,w,h};
  SDL_RenderFillRect(g_renderer, &r);
}

void Graphics::strokeRect(int x, int y, int w, int h, Color color, int thickness) {
  if (!g_renderer || w <= 0 || h <= 0) return;
  setDraw(color);
  for (int i = 0; i < thickness; ++i) {
    SDL_Rect r{x+i,y+i,w-2*i,h-2*i};
    SDL_RenderDrawRect(g_renderer, &r);
  }
}

void Graphics::fillRoundRect(int x, int y, int w, int h, int radius, Color color) {
  if (!g_renderer || w <= 0 || h <= 0 || color.a == 0) return;
  radius = std::max(0, std::min(radius, std::min(w,h)/2));
  if (radius == 0) {
    fillRect(x, y, w, h, color);
    return;
  }
  const std::string key = "frr|" + std::to_string(w) + "x" + std::to_string(h) + "|" + std::to_string(radius) + "|" + colorKey(color);
  drawCachedShape(key, x, y, w, h, [&](int &tw, int &th, std::vector<uint8_t> &pixels) {
    tw = w;
    th = h;
    pixels.assign(static_cast<std::size_t>(w) * h * 4, 0);
    for (int py = 0; py < h; ++py) {
      for (int px = 0; px < w; ++px) {
        if (insideRoundRect(px, py, w, h, radius)) putRgba(pixels, py * w + px, color);
      }
    }
  });
}

void Graphics::strokeRoundRect(int x, int y, int w, int h, int radius, Color color, int thickness) {
  if (!g_renderer || w <= 0 || h <= 0 || color.a == 0 || thickness <= 0) return;
  radius = std::max(0, std::min(radius, std::min(w,h)/2));
  const std::string key = "srr|" + std::to_string(w) + "x" + std::to_string(h) + "|" + std::to_string(radius) + "|" +
    std::to_string(thickness) + "|" + colorKey(color);
  drawCachedShape(key, x, y, w, h, [&](int &tw, int &th, std::vector<uint8_t> &pixels) {
    tw = w;
    th = h;
    pixels.assign(static_cast<std::size_t>(w) * h * 4, 0);
    const int innerRadius = std::max(0, radius - thickness);
    for (int py = 0; py < h; ++py) {
      for (int px = 0; px < w; ++px) {
        if (insideRoundRect(px, py, w, h, radius) &&
            !insideRoundRect(px - thickness, py - thickness, w - 2 * thickness, h - 2 * thickness, innerRadius)) {
          putRgba(pixels, py * w + px, color);
        }
      }
    }
  });
}

// Gradients are cached as 1-pixel strips and stretched along the constant axis.
void Graphics::fillVerticalGradient(int x, int y, int w, int h, Color top, Color bottom) {
  if (h <= 0 || w <= 0) return;
  const std::string key = "vg|" + std::to_string(h) + "|" + colorKey(top) + colorKey(bottom);
  drawCachedShape(key, x, y, w, h, [&](int &tw, int &th, std::vector<uint8_t> &pixels) {
    tw = 1;
    th = h;
    pixels.assign(static_cast<std::size_t>(h) * 4, 0);
    for (int yy = 0; yy < h; ++yy) {
      putRgba(pixels, yy, mix(top, bottom, float(yy) / float(std::max(1, h-1))));
    }
  });
}

void Graphics::fillHorizontalGradient(int x, int y, int w, int h, Color left, Color right) {
  if (w <= 0 || h <= 0) return;
  const std::string key = "hg|" + std::to_string(w) + "|" + colorKey(left) + colorKey(right);
  drawCachedShape(key, x, y, w, h, [&](int &tw, int &th, std::vector<uint8_t> &pixels) {
    tw = w;
    th = 1;
    pixels.assign(static_cast<std::size_t>(w) * 4, 0);
    for (int xx = 0; xx < w; ++xx) {
      putRgba(pixels, xx, mix(left, right, float(xx) / float(std::max(1, w-1))));
    }
  });
}

void Graphics::fillCircle(int cx, int cy, int radius, Color color) {
  if (radius <= 0 || color.a == 0) return;
  const int size = radius * 2 + 1;
  const std::string key = "fc|" + std::to_string(radius) + "|" + colorKey(color);
  drawCachedShape(key, cx - radius, cy - radius, size, size, [&](int &tw, int &th, std::vector<uint8_t> &pixels) {
    tw = size;
    th = size;
    pixels.assign(static_cast<std::size_t>(size) * size * 4, 0);
    for (int yy = -radius; yy <= radius; ++yy) {
      const int dx = static_cast<int>(std::sqrt(std::max(0, radius*radius - yy*yy)));
      for (int xx = -dx; xx <= dx; ++xx) putRgba(pixels, (yy + radius) * size + (xx + radius), color);
    }
  });
}

void Graphics::strokeCircle(int cx, int cy, int radius, Color color, int thickness) {
  if (radius <= 0 || color.a == 0 || thickness <= 0) return;
  const int size = radius * 2 + 1;
  const std::string key = "sc|" + std::to_string(radius) + "|" + std::to_string(thickness) + "|" + colorKey(color);
  drawCachedShape(key, cx - radius, cy - radius, size, size, [&](int &tw, int &th, std::vector<uint8_t> &pixels) {
    tw = size;
    th = size;
    pixels.assign(static_cast<std::size_t>(size) * size * 4, 0);
    for (int t = 0; t < thickness; ++t) {
      const int r = radius - t;
      for (int a = 0; a < 360; ++a) {
        const float rad = a * 3.1415926f / 180.0f;
        const int px = int(std::cos(rad) * r) + radius;
        const int py = int(std::sin(rad) * r) + radius;
        if (px >= 0 && py >= 0 && px < size && py < size) putRgba(pixels, py * size + px, color);
      }
    }
  });
}

void Graphics::drawLine(int x0, int y0, int x1, int y1, Color color, int thickness) {
  setDraw(color);
  for (int i=0; i<thickness; ++i) SDL_RenderDrawLine(g_renderer, x0, y0+i, x1, y1+i);
}

#ifdef NSTV_USE_SDL_TTF
namespace {
// Draws text with a given font through the text texture cache. Returns false without a font.
bool drawTextWithFont(TTF_Font *font, const std::string &text, int x, int y, Color color);
int textWidthWithFont(TTF_Font *font, const std::string &text);
}
#endif

void Graphics::drawTextSize(const std::string &text, int x, int y, int pointSize, Color color, bool bold) {
  if (!g_renderer) return;
#ifdef NSTV_USE_SDL_TTF
  if (drawTextWithFont(fontForPointSize(pointSize, bold), text, x, y, color)) return;
#endif
  drawText(text, x, y, std::max(1, pointSize / 8), color, bold);
}

int Graphics::textWidthSize(const std::string &text, int pointSize, bool bold) const {
#ifdef NSTV_USE_SDL_TTF
  if (TTF_Font *font = fontForPointSize(pointSize, bold)) return textWidthWithFont(font, text);
#endif
  return textWidth(text, std::max(1, pointSize / 8));
}

void Graphics::drawText(const std::string &text, int x, int y, int scale, Color color, bool bold) {
  if (!g_renderer) return;
#ifdef NSTV_USE_SDL_TTF
  if (drawTextWithFont(fontForScale(scale, bold), text, x, y, color)) return;
#endif
  const std::string s = normalizeAscii(text);
  drawBitmapFallbackText(s, x, y, scale, color, bold);
}

#ifdef NSTV_USE_SDL_TTF
namespace {
bool drawTextWithFont(TTF_Font *font, const std::string &text, int x, int y, Color color) {
  if (!font) return false;
  if (text.empty() || color.a == 0) return true;
  // Glyphs are rasterised once in white and tinted per draw, so a label keeps one texture
  // regardless of focus/selection colour changes.
  std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(font));
  key.push_back('\x1f');
  key += text;
  auto found = g_textTextureCache.find(key);
  if (found == g_textTextureCache.end()) {
    LruTexture entry;
    const std::string s = normalizeAscii(text);
    SDL_Surface *surface = s.empty() ? nullptr : TTF_RenderUTF8_Blended(font, s.c_str(), SDL_Color{255, 255, 255, 255});
    if (surface) {
      entry.texture = SDL_CreateTextureFromSurface(g_renderer, surface);
      entry.width = surface->w;
      entry.height = surface->h;
      if (entry.texture) SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
      SDL_FreeSurface(surface);
    }
    found = g_textTextureCache.emplace(std::move(key), entry).first;
  }
  LruTexture &entry = found->second;
  entry.lastUsedFrame = g_frameIndex;
  if (!entry.texture) return true;
  SDL_SetTextureColorMod(entry.texture, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(entry.texture, color.a);
  SDL_Rect dst{x, y, entry.width, entry.height};
  SDL_RenderCopy(g_renderer, entry.texture, nullptr, &dst);
  return true;
}

int textWidthWithFont(TTF_Font *font, const std::string &text) {
  std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(font));
  key.push_back('\x1f');
  key += text;
  auto textFound = g_textTextureCache.find(key);
  if (textFound != g_textTextureCache.end()) return textFound->second.width;
  auto found = g_textWidthCache.find(key);
  if (found != g_textWidthCache.end()) return found->second;
  const std::string s = normalizeAscii(text);
  int w = 0, h = 0;
  TTF_SizeUTF8(font, s.c_str(), &w, &h);
  g_textWidthCache.emplace(std::move(key), w);
  return w;
}
} // namespace
#endif

// Built-in bitmap fallback. This prevents the UI from disappearing if SDL_ttf cannot load the
// TTF font on the Switch SD card.
void Graphics::drawBitmapFallbackText(const std::string &s, int x, int y, int scale, Color color, bool bold) {
  int cx = x;
  const int pixel = std::max(1, scale);
  const int step = 6 * pixel + (bold ? 1 : 0);

  for (char ch : s) {
    auto glyph = fallbackGlyph(ch);

    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((glyph[row] >> (4 - col)) & 1) {
          fillRect(cx + col * pixel, y + row * pixel, pixel, pixel, color);
          if (bold) {
            fillRect(cx + col * pixel + 1, y + row * pixel, pixel, pixel, color);
          }
        }
      }
    }

    cx += step;
  }
}

void Graphics::drawTextRight(const std::string &text, int rightX, int y, int scale, Color color, bool bold) {
  drawText(text, rightX - textWidth(text, scale), y, scale, color, bold);
}

int Graphics::textWidth(const std::string &text, int scale) const {
#ifdef NSTV_USE_SDL_TTF
  if (TTF_Font *font = fontForScale(scale, false)) return textWidthWithFont(font, text);
#endif
  const std::string s = normalizeAscii(text);
  return int(s.size()) * (6*scale);
}

// Tinted icon from romfs:/icons/ui/<name>.svg (white strokes, rasterised once at the size set in
// the file, then scaled and coloured per draw).
void Graphics::drawIcon(const std::string &name, int x, int y, int size, Color color) {
  if (!g_renderer || size <= 0 || color.a == 0) return;
#ifdef NSTV_USE_SDL_IMAGE
#ifdef __SWITCH__
  const std::string path = "romfs:/icons/ui/" + name + ".svg";
#else
  const std::string path = "./romfs/icons/ui/" + name + ".svg";
#endif
  int texW = 0, texH = 0;
  SDL_Texture *texture = loadTextureFromFile(path, texW, texH);
  if (!texture) return;
  SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(texture, color.a);
  SDL_Rect dst{x, y, size, size};
  SDL_RenderCopy(g_renderer, texture, nullptr, &dst);
  SDL_SetTextureColorMod(texture, 255, 255, 255);
  SDL_SetTextureAlphaMod(texture, 255);
#else
  (void)name; (void)x; (void)y; (void)color;
#endif
}

std::string Graphics::fitText(const std::string &text, int maxChars) {
  std::string s = normalizeAscii(text);
  if (int(s.size()) <= maxChars) return s;
  if (maxChars <= 3) return s.substr(0, std::max(0, maxChars));
  return s.substr(0, maxChars-3) + "...";
}

void Graphics::drawBadge(const std::string &text, int x, int y, int w, int h, Color bg, Color fg) {
  fillRoundRect(x,y,w,h,h/2,bg);
  strokeRoundRect(x,y,w,h,h/2,rgba(72,92,128,35),1);
  int tw = textWidth(text, 1);
  drawText(text, x + (w-tw)/2, y + (h-12)/2, 1, fg, true);
}

void Graphics::drawIconBox(const std::string &kind, int x, int y, int size, Color bg1, Color bg2, Color fg) {
  fillVerticalGradient(x,y,size,size,bg1,bg2);
  fillRoundRect(x,y,size,size,10,rgba(0,0,0,0));
  strokeRoundRect(x,y,size,size,10,rgba(72,92,128,35),1);
  drawHeaderIcon(kind, x+6, y+6, size-12, fg);
}

void Graphics::drawHeaderIcon(const std::string &name, int x, int y, int size, Color color) {
  if (name == "config" || name == "+") {
    int cx = x + size / 2; int cy = y + size / 2;
    strokeCircle(cx, cy, size / 4, color, std::max(2, size/18));
    for (int i=0;i<8;i++){float a=i*6.2831853f/8.0f; drawLine(cx+int(std::cos(a)*size*.34f), cy+int(std::sin(a)*size*.34f), cx+int(std::cos(a)*size*.45f), cy+int(std::sin(a)*size*.45f), color, std::max(2,size/16));}
    return;
  }
  if (name == "categories") {
    int t=std::max(2,size/18); for(int i=0;i<3;i++){int yy=y+size/4+i*size/5; fillCircle(x+size/5,yy,t+2,color); drawLine(x+size/3,yy,x+size-size/8,yy,color,t);} return;
  }
  if (name == "channels" || name == "live") {
    int t=std::max(2,size/18); strokeRoundRect(x+size/7,y+size/4,size*5/7,size/2,size/12,color,t); drawLine(x+size/2,y+size/4,x+size/2-size/7,y+size/10,color,t); drawLine(x+size/2,y+size/4,x+size/2+size/7,y+size/10,color,t); return;
  }
  if (name == "layers") {
    int t=std::max(2,size/20); for(int i=0;i<3;i++){int yy=y+size/5+i*size/5; drawLine(x+size/2,yy,x+size-size/6,yy+size/8,color,t); drawLine(x+size-size/6,yy+size/8,x+size/2,yy+size/4,color,t); drawLine(x+size/2,yy+size/4,x+size/6,yy+size/8,color,t); drawLine(x+size/6,yy+size/8,x+size/2,yy,color,t);} return;
  }
  if (name == "movies") { fillCircle(x+size/2,y+size/2,size/3,color); fillCircle(x+size/2,y+size/2,size/10,rgb(30,20,60)); return; }
  if (name == "series") { strokeRoundRect(x+size/5,y+size/4,size*3/5,size/2,size/14,color,std::max(2,size/18)); fillRect(x+size/4,y+size/2,size/2,std::max(2,size/12),color); return; }
  if (name == "radio") { strokeRoundRect(x+size/5,y+size/3,size*3/5,size/3,size/12,color,std::max(2,size/18)); drawLine(x+size/4,y+size/3,x+size*3/4,y+size/8,color,std::max(2,size/18)); return; }
  if (name == "favorites") {
    int t = std::max(3, size / 9);
    int cx = x + size / 2;
    int cy = y + size / 2;
    fillCircle(x + size * 38 / 100, y + size * 38 / 100, std::max(4, size / 7), color);
    fillCircle(x + size * 62 / 100, y + size * 38 / 100, std::max(4, size / 7), color);
    drawLine(x + size * 28 / 100, y + size * 42 / 100, cx, y + size * 73 / 100, color, t);
    drawLine(x + size * 72 / 100, y + size * 42 / 100, cx, y + size * 73 / 100, color, t);
    drawLine(x + size * 35 / 100, y + size * 34 / 100, cx, cy, color, t);
    drawLine(x + size * 65 / 100, y + size * 34 / 100, cx, cy, color, t);
    return;
  }
}

void Graphics::drawLogoPlaceholder(const std::string &name, const std::string &logoUrl, int x, int y, int w, int h) {
  (void)logoUrl;
  drawLogoFallback(name, x, y, w, h, w <= 52 ? 2 : 3);
}

void Graphics::drawLogoFallback(const std::string &name, int x, int y, int w, int h, int scale) {
  (void)name; (void)scale;
  fillVerticalGradient(x, y, w, h, rgb(28,38,67), rgb(12,18,32));
  fillRoundRect(x, y, w, h, 9, rgba(0,0,0,0));
  strokeRoundRect(x, y, w, h, 9, rgba(72,92,128,35), 1);
  Color fg = rgb(165, 190, 230);
  drawHeaderIcon("channels", x + w/6, y + h/7, std::min(w,h)*2/3, fg);
}

void Graphics::drawImage(const Bitmap &bitmap, int x, int y, int w, int h) {
  if (!bitmap.valid() || !g_renderer || w <= 0 || h <= 0) return;

  if (bitmap.id != 0) {
    // Immutable bitmap: upload once, then reuse the texture on every frame.
    LruTexture &entry = g_bitmapTextureCache[bitmap.id];
    if (!entry.texture) {
      entry.texture = createStaticTexture(bitmap.width, bitmap.height, bitmap.rgba);
      entry.width = bitmap.width;
      entry.height = bitmap.height;
    }
    entry.lastUsedFrame = g_frameIndex;
    if (!entry.texture) return;
    const float srcRatio = float(bitmap.width) / float(bitmap.height);
    const float dstRatio = float(w) / float(h);
    int drawW = w;
    int drawH = h;
    if (srcRatio > dstRatio) {
      drawH = std::max(1, int(w / srcRatio));
    } else {
      drawW = std::max(1, int(h * srcRatio));
    }
    SDL_Rect dst{x + (w - drawW) / 2, y + (h - drawH) / 2, drawW, drawH};
    SDL_RenderCopy(g_renderer, entry.texture, nullptr, &dst);
    return;
  }

  const uint8_t *key = bitmap.rgba.data();
  CachedTexture &cached = g_textureCache[key];

  if (!cached.texture || cached.width != bitmap.width || cached.height != bitmap.height) {
    if (cached.texture) {
      SDL_DestroyTexture(cached.texture);
      cached.texture = nullptr;
    }

    cached.texture = SDL_CreateTexture(
      g_renderer,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_STREAMING,
      bitmap.width,
      bitmap.height
    );

    cached.width = bitmap.width;
    cached.height = bitmap.height;

    if (cached.texture) {
      SDL_SetTextureBlendMode(cached.texture, SDL_BLENDMODE_BLEND);
    }
  }

  if (!cached.texture) {
    return;
  }

  SDL_UpdateTexture(
    cached.texture,
    nullptr,
    bitmap.rgba.data(),
    bitmap.width * 4
  );

  float srcRatio = float(bitmap.width) / float(bitmap.height);
  float dstRatio = float(w) / float(h);

  int drawW = w;
  int drawH = h;

  if (srcRatio > dstRatio) {
    drawH = std::max(1, int(w / srcRatio));
  } else {
    drawW = std::max(1, int(h * srcRatio));
  }

  SDL_Rect dst{x + (w - drawW) / 2, y + (h - drawH) / 2, drawW, drawH};
  SDL_RenderCopy(g_renderer, cached.texture, nullptr, &dst);
}


void Graphics::drawYuvFrame(const YuvFrame &frame, int x, int y, int w, int h) {
  if (!frame.valid() || !g_renderer || w <= 0 || h <= 0) return;

  const uint8_t *key = frame.y.data();
  CachedTexture &cached = g_textureCache[key];

  const Uint32 textureFormat =
    frame.format == YuvFrame::Format::NV12
      ? SDL_PIXELFORMAT_NV12
      : SDL_PIXELFORMAT_IYUV;

  if (!cached.texture || cached.width != frame.width || cached.height != frame.height || cached.format != textureFormat) {
    if (cached.texture) {
      SDL_DestroyTexture(cached.texture);
      cached.texture = nullptr;
    }

    cached.texture = SDL_CreateTexture(
      g_renderer,
      textureFormat,
      SDL_TEXTUREACCESS_STREAMING,
      frame.width,
      frame.height
    );

    cached.width = frame.width;
    cached.height = frame.height;
    cached.format = textureFormat;
  }

  if (!cached.texture) {
    return;
  }

  if (frame.format == YuvFrame::Format::NV12) {
    SDL_UpdateNVTexture(
      cached.texture,
      nullptr,
      frame.y.data(),
      frame.yPitch,
      frame.u.data(),
      frame.uPitch
    );
  } else {
    SDL_UpdateYUVTexture(
      cached.texture,
      nullptr,
      frame.y.data(),
      frame.yPitch,
      frame.u.data(),
      frame.uPitch,
      frame.v.data(),
      frame.vPitch
    );
  }

  float srcRatio = float(frame.width) / float(frame.height);
  float dstRatio = float(w) / float(h);

  int drawW = w;
  int drawH = h;

  if (srcRatio > dstRatio) {
    drawH = std::max(1, int(w / srcRatio));
  } else {
    drawW = std::max(1, int(h * srcRatio));
  }

  SDL_Rect dst{x + (w - drawW) / 2, y + (h - drawH) / 2, drawW, drawH};
  SDL_RenderCopy(g_renderer, cached.texture, nullptr, &dst);
}



void Graphics::drawImageFile(
  const std::string &path,
  int x,
  int y,
  int w,
  int h,
  bool cover
) {
  if (!g_renderer || w <= 0 || h <= 0) {
    return;
  }

#ifndef NSTV_USE_SDL_IMAGE
  (void)path;
  (void)x;
  (void)y;
  return;
#else
  int imageW = 0;
  int imageH = 0;

  SDL_Texture *texture = loadTextureFromFile(path, imageW, imageH);

  if (!texture || imageW <= 0 || imageH <= 0) {
    return;
  }

  const float srcRatio = static_cast<float>(imageW) / static_cast<float>(imageH);
  const float dstRatio = static_cast<float>(w) / static_cast<float>(h);

  int drawW = w;
  int drawH = h;

  if (cover) {
    if (srcRatio > dstRatio) {
      drawW = std::max(1, static_cast<int>(h * srcRatio));
      drawH = h;
    } else {
      drawW = w;
      drawH = std::max(1, static_cast<int>(w / srcRatio));
    }
  } else {
    if (srcRatio > dstRatio) {
      drawW = w;
      drawH = std::max(1, static_cast<int>(w / srcRatio));
    } else {
      drawW = std::max(1, static_cast<int>(h * srcRatio));
      drawH = h;
    }
  }

  SDL_Rect dst{
    x + (w - drawW) / 2,
    y + (h - drawH) / 2,
    drawW,
    drawH
  };

  SDL_RenderCopy(g_renderer, texture, nullptr, &dst);
#endif
}

void Graphics::drawImageFileCentered(
  const std::string &path,
  int x,
  int y,
  int w,
  int h
) {
  drawImageFile(path, x, y, w, h, false);
}

} // namespace nstv

#endif // NSTV_USE_SDL
