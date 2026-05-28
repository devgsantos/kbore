#ifndef NSTV_USE_SDL
#include "nstv/graphics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace nstv {

namespace {
static std::vector<uint32_t> g_pixels(Graphics::Width * Graphics::Height);

#ifdef __SWITCH__
static Framebuffer g_framebuffer;
static bool g_framebufferReady = false;
#endif

uint32_t pack(Color c) {
  return (uint32_t(c.a) << 24) | (uint32_t(c.b) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.r);
}

Color unpack(uint32_t p) {
  return Color{
    uint8_t(p & 0xff),
    uint8_t((p >> 8) & 0xff),
    uint8_t((p >> 16) & 0xff),
    uint8_t((p >> 24) & 0xff)
  };
}

Color mix(Color a, Color b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  return Color{
    uint8_t(a.r + (b.r - a.r) * t),
    uint8_t(a.g + (b.g - a.g) * t),
    uint8_t(a.b + (b.b - a.b) * t),
    uint8_t(a.a + (b.a - a.a) * t)
  };
}

uint32_t hashString(const std::string &s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}

std::string normalizeAscii(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(input[i]);
    if (c < 128) {
      out.push_back(static_cast<char>(c));
      continue;
    }
    // Skip UTF-8 continuation bytes and approximate accented letters.
    if ((c & 0xC0) == 0x80) continue;
    out.push_back(' ');
  }
  return out;
}

std::array<uint8_t, 7> glyph(char ch) {
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
#ifdef __SWITCH__
  if (!g_framebufferReady) {
    framebufferCreate(&g_framebuffer, nwindowGetDefault(), Width, Height, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&g_framebuffer);
    g_framebufferReady = true;
  }
#endif
}

void Graphics::beginFrame(Color color) {
  std::fill(g_pixels.begin(), g_pixels.end(), pack(color));
  // Subtle dark radial-ish bands.
  fillVerticalGradient(0, 0, Width, Height, rgb(6, 9, 18), rgb(2, 5, 11));
  fillHorizontalGradient(0, 0, Width, 120, rgba(11, 19, 42, 160), rgba(2, 5, 16, 0));
}

void Graphics::present() {
#ifdef __SWITCH__
  if (!g_framebufferReady) return;
  u32 stride = 0;
  uint32_t *fb = static_cast<uint32_t *>(framebufferBegin(&g_framebuffer, &stride));
  if (fb) {
    for (int y = 0; y < Height; ++y) {
      std::memcpy(fb + y * stride / sizeof(uint32_t), g_pixels.data() + y * Width, Width * sizeof(uint32_t));
    }
    framebufferEnd(&g_framebuffer);
  }
#else
  // Host preview: write a frame image that can be opened after each render.
  std::ofstream out("nstv-frame.ppm", std::ios::binary);
  out << "P6\n" << Width << " " << Height << "\n255\n";
  for (uint32_t p : g_pixels) {
    Color c = unpack(p);
    out.put(char(c.r)); out.put(char(c.g)); out.put(char(c.b));
  }
  out.close();
  std::printf("Rendered graphical frame to nstv-frame.ppm\n");
#endif
}

void Graphics::putPixel(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= Width || y >= Height) return;
  g_pixels[y * Width + x] = pack(color);
}

void Graphics::blendPixel(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= Width || y >= Height) return;
  if (color.a == 255) { putPixel(x, y, color); return; }
  Color dst = unpack(g_pixels[y * Width + x]);
  float a = color.a / 255.0f;
  Color out{
    uint8_t(dst.r * (1-a) + color.r * a),
    uint8_t(dst.g * (1-a) + color.g * a),
    uint8_t(dst.b * (1-a) + color.b * a),
    255
  };
  g_pixels[y * Width + x] = pack(out);
}

void Graphics::fillRect(int x, int y, int w, int h, Color color) {
  int x0 = std::max(0, x), y0 = std::max(0, y);
  int x1 = std::min(Width, x+w), y1 = std::min(Height, y+h);
  for (int yy=y0; yy<y1; ++yy) for (int xx=x0; xx<x1; ++xx) blendPixel(xx, yy, color);
}

void Graphics::strokeRect(int x, int y, int w, int h, Color color, int thickness) {
  fillRect(x, y, w, thickness, color); fillRect(x, y+h-thickness, w, thickness, color);
  fillRect(x, y, thickness, h, color); fillRect(x+w-thickness, y, thickness, h, color);
}

void Graphics::fillRoundRect(int x, int y, int w, int h, int r, Color color) {
  if (color.a == 0 || w <= 0 || h <= 0) return;

  r = std::max(0, std::min(r, std::min(w, h) / 2));

  fillRect(x + r, y, std::max(0, w - 2 * r), h, color);
  fillRect(x, y + r, w, std::max(0, h - 2 * r), color);

  for (int yy = -r; yy <= r; ++yy) {
    for (int xx = -r; xx <= r; ++xx) {
      if (xx * xx + yy * yy > r * r) continue;

      if (xx <= 0 && yy <= 0) blendPixel(x + r + xx, y + r + yy, color);
      if (xx >= 0 && yy <= 0) blendPixel(x + w - r - 1 + xx, y + r + yy, color);
      if (xx <= 0 && yy >= 0) blendPixel(x + r + xx, y + h - r - 1 + yy, color);
      if (xx >= 0 && yy >= 0) blendPixel(x + w - r - 1 + xx, y + h - r - 1 + yy, color);
    }
  }
}


void Graphics::strokeRoundRect(int x, int y, int w, int h, int r, Color color, int thickness) {
  if (w <= 0 || h <= 0) return;
  r = std::max(0, std::min(r, std::min(w, h) / 2));

  for (int t = 0; t < thickness; ++t) {
    const int ix = x + t;
    const int iy = y + t;
    const int iw = w - 2 * t;
    const int ih = h - 2 * t;
    const int ir = std::max(0, r - t);

    if (iw <= 0 || ih <= 0) break;

    fillRect(ix + ir, iy, std::max(0, iw - 2 * ir), 1, color);
    fillRect(ix + ir, iy + ih - 1, std::max(0, iw - 2 * ir), 1, color);
    fillRect(ix, iy + ir, 1, std::max(0, ih - 2 * ir), color);
    fillRect(ix + iw - 1, iy + ir, 1, std::max(0, ih - 2 * ir), color);

    for (int yy = -ir; yy <= ir; ++yy) {
      for (int xx = -ir; xx <= ir; ++xx) {
        int d = xx * xx + yy * yy;
        int outer = ir * ir;
        int innerR = std::max(0, ir - 1);
        int inner = innerR * innerR;

        if (d > outer || d < inner) continue;

        if (xx <= 0 && yy <= 0) blendPixel(ix + ir + xx, iy + ir + yy, color);
        if (xx >= 0 && yy <= 0) blendPixel(ix + iw - ir - 1 + xx, iy + ir + yy, color);
        if (xx <= 0 && yy >= 0) blendPixel(ix + ir + xx, iy + ih - ir - 1 + yy, color);
        if (xx >= 0 && yy >= 0) blendPixel(ix + iw - ir - 1 + xx, iy + ih - ir - 1 + yy, color);
      }
    }
  }
}


void Graphics::fillVerticalGradient(int x, int y, int w, int h, Color top, Color bottom) {
  for (int yy=0; yy<h; ++yy) {
    Color c = mix(top, bottom, h <= 1 ? 0.0f : float(yy) / float(h-1));
    fillRect(x, y+yy, w, 1, c);
  }
}

void Graphics::fillHorizontalGradient(int x, int y, int w, int h, Color left, Color right) {
  for (int xx=0; xx<w; ++xx) {
    Color c = mix(left, right, w <= 1 ? 0.0f : float(xx) / float(w-1));
    fillRect(x+xx, y, 1, h, c);
  }
}

void Graphics::fillCircle(int cx, int cy, int radius, Color color) {
  for (int y=-radius; y<=radius; ++y) for (int x=-radius; x<=radius; ++x)
    if (x*x+y*y <= radius*radius) blendPixel(cx+x, cy+y, color);
}
void Graphics::strokeCircle(int cx, int cy, int radius, Color color, int thickness) {
  for (int y=-radius; y<=radius; ++y) for (int x=-radius; x<=radius; ++x) {
    int d=x*x+y*y; if (d <= radius*radius && d >= (radius-thickness)*(radius-thickness)) blendPixel(cx+x, cy+y, color);
  }
}
void Graphics::drawLine(int x0, int y0, int x1, int y1, Color color, int thickness) {
  int dx=std::abs(x1-x0), sx=x0<x1?1:-1; int dy=-std::abs(y1-y0), sy=y0<y1?1:-1; int err=dx+dy;
  while (true) { fillRect(x0-thickness/2, y0-thickness/2, thickness, thickness, color); if (x0==x1&&y0==y1) break; int e2=2*err; if (e2>=dy){err+=dy;x0+=sx;} if(e2<=dx){err+=dx;y0+=sy;} }
}

void Graphics::drawGlyph(char ch, int x, int y, int scale, Color color, bool bold) {
  auto rows = glyph(ch);
  int extra = bold ? std::max(1, scale/3) : 0;
  for (int row=0; row<7; ++row) {
    for (int col=0; col<5; ++col) {
      if (rows[row] & (1 << (4-col))) fillRect(x + col*scale, y + row*scale, scale + extra, scale, color);
    }
  }
}

void Graphics::drawText(const std::string &text, int x, int y, int scale, Color color, bool bold) {
  std::string s = normalizeAscii(text);
  int cx=x;
  for (char ch : s) { drawGlyph(ch, cx, y, scale, color, bold); cx += 6*scale + (bold ? 1 : 0); }
}

void Graphics::drawTextRight(const std::string &text, int rightX, int y, int scale, Color color, bool bold) {
  drawText(text, rightX - textWidth(text, scale), y, scale, color, bold);
}

int Graphics::textWidth(const std::string &text, int scale) const { return int(normalizeAscii(text).size()) * (6*scale); }

std::string Graphics::fitText(const std::string &text, int maxChars) {
  std::string s = normalizeAscii(text);
  if (int(s.size()) <= maxChars) return s;
  if (maxChars <= 3) return s.substr(0, std::max(0, maxChars));
  return s.substr(0, maxChars-3) + "...";
}

void Graphics::drawBadge(const std::string &text, int x, int y, int w, int h, Color bg, Color fg) {
  fillRoundRect(x,y,w,h,h/2,bg); strokeRoundRect(x,y,w,h,h/2,lighten(bg,35),1);
  drawText(text, x + std::max(4, (w - textWidth(text, 2))/2), y + 7, 2, fg, true);
}

void Graphics::drawIconBox(const std::string &kind, int x, int y, int size, Color bg1, Color bg2, Color fg) {
  fillVerticalGradient(x,y,size,size,bg1,bg2); fillRoundRect(x,y,size,size,10,rgba(0,0,0,0));
  strokeRoundRect(x,y,size,size,10,rgba(255,255,255,30),1);
  if (kind == "live") {
    strokeRoundRect(x+13,y+16,size-26,size-30,4,fg,3); drawLine(x+size/2,y+16,x+size/2-9,y+7,fg,3); drawLine(x+size/2,y+16,x+size/2+9,y+7,fg,3);
  } else if (kind == "movies") {
    fillCircle(x+size/2,y+size/2,16,fg); fillCircle(x+size/2,y+size/2,5,bg1); for(int i=0;i<5;i++){ float a=i*6.28318f/5; fillCircle(x+size/2+int(std::cos(a)*10), y+size/2+int(std::sin(a)*10), 3, bg1);}  
  } else if (kind == "series") {
    strokeRect(x+16,y+20,size-32,size-24,fg,3); drawLine(x+20,y+16,x+30,y+8,fg,3); drawLine(x+42,y+16,x+53,y+8,fg,3); fillRect(x+20,y+28,size-40,5,fg);
  } else if (kind == "radio") {
    strokeRoundRect(x+14,y+25,size-28,size-20,5,fg,3); drawLine(x+20,y+25,x+50,y+12,fg,3); fillCircle(x+25,y+42,5,fg); fillCircle(x+43,y+42,5,fg);
  } else {
    drawText(kind.substr(0,3), x+9, y+size/2-7, 2, fg, true);
  }
}

void Graphics::drawLogoPlaceholder(const std::string &name, const std::string &logoUrl, int x, int y, int w, int h) {
  (void)logoUrl;
  drawLogoFallback(name, x, y, w, h, w <= 52 ? 2 : 3);
}

void Graphics::drawLogoFallback(const std::string &name, int x, int y, int w, int h, int scale) {
  (void)name;
  (void)scale;

  fillVerticalGradient(x, y, w, h, rgb(28,38,67), rgb(12,18,32));
  fillRoundRect(x, y, w, h, 9, rgba(0,0,0,0));
  strokeRoundRect(x, y, w, h, 9, rgba(255,255,255,55), 1);

  // Generic media placeholder. Never draw numeric codes as fallback.
  Color fg = rgb(165, 190, 230);
  int t = std::max(2, std::min(w, h) / 14);
  int ix = x + w / 4;
  int iy = y + h / 3;
  int iw = w / 2;
  int ih = h / 3;

  strokeRoundRect(ix, iy, iw, ih, 4, fg, t);
  fillCircle(ix + iw / 2, iy + ih / 2, std::max(2, ih / 6), fg);
  drawLine(ix + iw / 2, iy, ix + iw / 2 - iw / 5, y + h / 6, fg, t);
  drawLine(ix + iw / 2, iy, ix + iw / 2 + iw / 5, y + h / 6, fg, t);
}

void Graphics::drawImage(const Bitmap &bitmap, int x, int y, int w, int h) {
  if (!bitmap.valid() || w <= 0 || h <= 0) return;

  fillRoundRect(x, y, w, h, 9, rgba(10, 15, 30, 255));

  const float srcRatio = static_cast<float>(bitmap.width) / static_cast<float>(bitmap.height);
  const float dstRatio = static_cast<float>(w) / static_cast<float>(h);

  int drawW = w;
  int drawH = h;

  if (srcRatio > dstRatio) {
    drawH = std::max(1, static_cast<int>(w / srcRatio));
  } else {
    drawW = std::max(1, static_cast<int>(h * srcRatio));
  }

  int ox = x + (w - drawW) / 2;
  int oy = y + (h - drawH) / 2;

  for (int yy = 0; yy < drawH; ++yy) {
    int sy = std::min(bitmap.height - 1, yy * bitmap.height / drawH);
    for (int xx = 0; xx < drawW; ++xx) {
      int sx = std::min(bitmap.width - 1, xx * bitmap.width / drawW);
      std::size_t idx = static_cast<std::size_t>((sy * bitmap.width + sx) * 4);
      Color c{
        bitmap.rgba[idx + 0],
        bitmap.rgba[idx + 1],
        bitmap.rgba[idx + 2],
        bitmap.rgba[idx + 3]
      };
      blendPixel(ox + xx, oy + yy, c);
    }
  }

  strokeRoundRect(x, y, w, h, 9, rgba(255,255,255,60), 1);
}


void Graphics::drawHeaderIcon(const std::string &name, int x, int y, int size, Color color) {
  if (name == "config") {
    int cx = x + size / 2;
    int cy = y + size / 2;
    strokeCircle(cx, cy, size / 4, color, std::max(2, size / 18));
    for (int i = 0; i < 8; ++i) {
      float a = float(i) * 6.2831853f / 8.0f;
      int x0 = cx + int(std::cos(a) * size * 0.34f);
      int y0 = cy + int(std::sin(a) * size * 0.34f);
      int x1 = cx + int(std::cos(a) * size * 0.45f);
      int y1 = cy + int(std::sin(a) * size * 0.45f);
      drawLine(x0, y0, x1, y1, color, std::max(2, size / 16));
    }
    return;
  }

  if (name == "categories") {
    int t = std::max(2, size / 18);
    for (int i = 0; i < 3; ++i) {
      int yy = y + size / 4 + i * size / 5;
      fillCircle(x + size / 5, yy, t + 2, color);
      drawLine(x + size / 3, yy, x + size - size / 8, yy, color, t);
    }
    return;
  }

  if (name == "channels") {
    int t = std::max(2, size / 18);
    strokeRoundRect(x + size / 7, y + size / 4, size * 5 / 7, size / 2, size / 12, color, t);
    drawLine(x + size / 2, y + size / 4, x + size / 2 - size / 7, y + size / 10, color, t);
    drawLine(x + size / 2, y + size / 4, x + size / 2 + size / 7, y + size / 10, color, t);
    return;
  }

  if (name == "layers") {
    int t = std::max(2, size / 20);
    for (int i = 0; i < 3; ++i) {
      int yy = y + size / 5 + i * size / 5;
      drawLine(x + size / 2, yy, x + size - size / 6, yy + size / 8, color, t);
      drawLine(x + size - size / 6, yy + size / 8, x + size / 2, yy + size / 4, color, t);
      drawLine(x + size / 2, yy + size / 4, x + size / 6, yy + size / 8, color, t);
      drawLine(x + size / 6, yy + size / 8, x + size / 2, yy, color, t);
    }
    return;
  }

  drawIconBox(name, x, y, size, rgba(color.r, color.g, color.b, 80), rgba(color.r, color.g, color.b, 30), color);
}


void Graphics::drawYuvFrame(const YuvFrame &frame, int x, int y, int w, int h) {
  (void)frame;
  fillRect(x, y, w, h, rgb(0, 0, 0));
  drawText("YUV video requires SDL renderer", x + 40, y + h / 2, 2, rgb(248,250,252), true);
}


} // namespace nstv

#endif // NSTV_USE_SDL
