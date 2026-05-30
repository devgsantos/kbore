#ifdef NSTV_USE_SDL

#include "nstv/graphics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

  for (const auto &path : paths) {
    FILE *fp = std::fopen(path.c_str(), "rb");

    if (fp) {
      std::fclose(fp);
      std::printf("[NSTV] Found font: %s\n", path.c_str());
      return path.c_str();
    }
  }

  std::printf("[NSTV] No font found\n");
  return nullptr;
}

#ifdef NSTV_USE_SDL_TTF
TTF_Font *fontForScale(int scale, bool bold) {
  if (!g_ttfReady) {
    std::printf("[NSTV] SDL_ttf is not ready\n");
    return nullptr;
  }

  const char *fontPath = firstExistingFont();

  if (!fontPath) {
    std::printf("[NSTV] No TTF font path available\n");
    return nullptr;
  }

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
};

std::map<const uint8_t *, CachedTexture> g_textureCache;
std::map<std::string, CachedTexture> g_fileTextureCache;

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
}

void Graphics::beginFrame(Color color) {
  if (!g_renderer) return;
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
  fillRect(x + radius, y, w - 2 * radius, h, color);
  fillRect(x, y + radius, w, h - 2 * radius, color);
  setDraw(color);
  for (int yy = -radius; yy <= radius; ++yy) {
    int dx = static_cast<int>(std::sqrt(std::max(0, radius*radius - yy*yy)));
    SDL_RenderDrawLine(g_renderer, x + radius - dx, y + radius + yy, x + radius, y + radius + yy);
    SDL_RenderDrawLine(g_renderer, x + w - radius, y + radius + yy, x + w - radius + dx, y + radius + yy);
    SDL_RenderDrawLine(g_renderer, x + radius - dx, y + h - radius + yy - 1, x + radius, y + h - radius + yy - 1);
    SDL_RenderDrawLine(g_renderer, x + w - radius, y + h - radius + yy - 1, x + w - radius + dx, y + h - radius + yy - 1);
  }
}

void Graphics::strokeRoundRect(int x, int y, int w, int h, int radius, Color color, int thickness) {
  if (!g_renderer || w <= 0 || h <= 0) return;
  radius = std::max(0, std::min(radius, std::min(w,h)/2));
  setDraw(color);
  for (int t = 0; t < thickness; ++t) {
    int ix = x + t;
    int iy = y + t;
    int iw = w - 2*t;
    int ih = h - 2*t;
    int r = std::max(0, radius - t);
    SDL_RenderDrawLine(g_renderer, ix+r, iy, ix+iw-r, iy);
    SDL_RenderDrawLine(g_renderer, ix+r, iy+ih-1, ix+iw-r, iy+ih-1);
    SDL_RenderDrawLine(g_renderer, ix, iy+r, ix, iy+ih-r);
    SDL_RenderDrawLine(g_renderer, ix+iw-1, iy+r, ix+iw-1, iy+ih-r);
    for (int a = 0; a <= 90; ++a) {
      float rad = a * 3.1415926f / 180.0f;
      int dx = static_cast<int>(std::cos(rad) * r);
      int dy = static_cast<int>(std::sin(rad) * r);
      SDL_RenderDrawPoint(g_renderer, ix+r-dx, iy+r-dy);
      SDL_RenderDrawPoint(g_renderer, ix+iw-r+dx-1, iy+r-dy);
      SDL_RenderDrawPoint(g_renderer, ix+r-dx, iy+ih-r+dy-1);
      SDL_RenderDrawPoint(g_renderer, ix+iw-r+dx-1, iy+ih-r+dy-1);
    }
  }
}

void Graphics::fillVerticalGradient(int x, int y, int w, int h, Color top, Color bottom) {
  if (h <= 0) return;
  for (int yy=0; yy<h; ++yy) {
    fillRect(x, y+yy, w, 1, mix(top, bottom, float(yy) / float(std::max(1, h-1))));
  }
}

void Graphics::fillHorizontalGradient(int x, int y, int w, int h, Color left, Color right) {
  if (w <= 0) return;
  for (int xx=0; xx<w; ++xx) {
    fillRect(x+xx, y, 1, h, mix(left, right, float(xx) / float(std::max(1, w-1))));
  }
}

void Graphics::fillCircle(int cx, int cy, int radius, Color color) {
  if (radius <= 0) return;
  setDraw(color);
  for (int y=-radius; y<=radius; ++y) {
    int dx = static_cast<int>(std::sqrt(std::max(0, radius*radius - y*y)));
    SDL_RenderDrawLine(g_renderer, cx-dx, cy+y, cx+dx, cy+y);
  }
}

void Graphics::strokeCircle(int cx, int cy, int radius, Color color, int thickness) {
  setDraw(color);
  for (int t=0; t<thickness; ++t) {
    int r = radius - t;
    for (int a=0; a<360; ++a) {
      float rad=a*3.1415926f/180.0f;
      SDL_RenderDrawPoint(g_renderer, cx+int(std::cos(rad)*r), cy+int(std::sin(rad)*r));
    }
  }
}

void Graphics::drawLine(int x0, int y0, int x1, int y1, Color color, int thickness) {
  setDraw(color);
  for (int i=0; i<thickness; ++i) SDL_RenderDrawLine(g_renderer, x0, y0+i, x1, y1+i);
}

void Graphics::drawText(const std::string &text, int x, int y, int scale, Color color, bool bold) {
  if (!g_renderer) return;
  std::string s = normalizeAscii(text);
#ifdef NSTV_USE_SDL_TTF
  TTF_Font *font = fontForScale(scale, bold);
  if (font) {
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, s.c_str(), toSDL(color));
    if (surface) {
      SDL_Texture *texture = SDL_CreateTextureFromSurface(g_renderer, surface);
      SDL_Rect dst{x, y, surface->w, surface->h};
      SDL_RenderCopy(g_renderer, texture, nullptr, &dst);
      SDL_DestroyTexture(texture);
      SDL_FreeSurface(surface);
    }
    return;
  }
#endif
  // Built-in bitmap fallback. This prevents the UI from disappearing if SDL_ttf
  // cannot load the TTF font on the Switch SD card.
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
  std::string s = normalizeAscii(text);
#ifdef NSTV_USE_SDL_TTF
  TTF_Font *font = fontForScale(scale, false);
  if (font) {
    int w=0,h=0;
    TTF_SizeUTF8(font, s.c_str(), &w, &h);
    return w;
  }
#endif
  return int(s.size()) * (6*scale);
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

  if (!cached.texture || cached.width != frame.width || cached.height != frame.height) {
    if (cached.texture) {
      SDL_DestroyTexture(cached.texture);
      cached.texture = nullptr;
    }

    cached.texture = SDL_CreateTexture(
      g_renderer,
      SDL_PIXELFORMAT_IYUV,
      SDL_TEXTUREACCESS_STREAMING,
      frame.width,
      frame.height
    );

    cached.width = frame.width;
    cached.height = frame.height;
  }

  if (!cached.texture) {
    return;
  }

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
