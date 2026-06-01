#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nstv {

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct Rect {
  int x;
  int y;
  int w;
  int h;
};


struct YuvFrame {
  enum class Format {
    IYUV,
    NV12
  };

  int width = 0;
  int height = 0;
  Format format = Format::IYUV;
  std::vector<uint8_t> y;
  std::vector<uint8_t> u;
  std::vector<uint8_t> v;
  int yPitch = 0;
  int uPitch = 0;
  int vPitch = 0;

  bool valid() const {
    if (format == Format::NV12) {
      return width > 0 && height > 0 &&
             yPitch > 0 && uPitch > 0 &&
             y.size() >= static_cast<std::size_t>(yPitch * height) &&
             u.size() >= static_cast<std::size_t>(uPitch * ((height + 1) / 2));
    }

    return width > 0 && height > 0 &&
           yPitch > 0 && uPitch > 0 && vPitch > 0 &&
           y.size() >= static_cast<std::size_t>(yPitch * height) &&
           u.size() >= static_cast<std::size_t>(uPitch * ((height + 1) / 2)) &&
           v.size() >= static_cast<std::size_t>(vPitch * ((height + 1) / 2));
  }
};


struct Bitmap {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;

  bool valid() const {
    return width > 0 && height > 0 && rgba.size() >= static_cast<std::size_t>(width * height * 4);
  }
};

class Graphics {
public:
  static constexpr int Width = 1280;
  static constexpr int Height = 720;

  Graphics();
  void beginFrame(Color color = {8, 11, 18, 255});
  void present();
  void suspendForNativeVideo();
  void resumeAfterNativeVideo();
  bool isSuspendedForNativeVideo() const;

  void fillRect(int x, int y, int w, int h, Color color);
  void strokeRect(int x, int y, int w, int h, Color color, int thickness = 1);
  void fillRoundRect(int x, int y, int w, int h, int radius, Color color);
  void strokeRoundRect(int x, int y, int w, int h, int radius, Color color, int thickness = 1);
  void fillVerticalGradient(int x, int y, int w, int h, Color top, Color bottom);
  void fillHorizontalGradient(int x, int y, int w, int h, Color left, Color right);
  void fillCircle(int cx, int cy, int radius, Color color);
  void strokeCircle(int cx, int cy, int radius, Color color, int thickness = 1);
  void drawLine(int x0, int y0, int x1, int y1, Color color, int thickness = 1);
  void drawText(const std::string &text, int x, int y, int scale, Color color, bool bold = false);
  void drawTextRight(const std::string &text, int rightX, int y, int scale, Color color, bool bold = false);
  void drawBadge(const std::string &text, int x, int y, int w, int h, Color bg, Color fg);
  void drawIconBox(const std::string &kind, int x, int y, int size, Color bg1, Color bg2, Color fg);
  void drawLogoPlaceholder(const std::string &name, const std::string &logoUrl, int x, int y, int w, int h);
  void drawLogoFallback(const std::string &name, int x, int y, int w, int h, int scale = 2);
  void drawImage(const Bitmap &bitmap, int x, int y, int w, int h);
  void drawImageFile(const std::string &path, int x, int y, int w, int h, bool cover = false);
  void drawImageFileCentered(const std::string &path, int x, int y, int w, int h);
  void drawYuvFrame(const YuvFrame &frame, int x, int y, int w, int h);
  void drawHeaderIcon(const std::string &name, int x, int y, int size, Color color);

  int textWidth(const std::string &text, int scale) const;
  static std::string fitText(const std::string &text, int maxChars);

private:
  void putPixel(int x, int y, Color color);
  void blendPixel(int x, int y, Color color);
  void drawGlyph(char ch, int x, int y, int scale, Color color, bool bold);
};

Color rgb(uint8_t r, uint8_t g, uint8_t b);
Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
Color lighten(Color color, int amount);
Color darken(Color color, int amount);
Color typeColor(const std::string &type);

} // namespace nstv
