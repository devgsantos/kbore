#pragma once

#include <cstdint>
#include <string>

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

class Graphics {
public:
  static constexpr int Width = 1280;
  static constexpr int Height = 720;

  Graphics();
  void beginFrame(Color color = {8, 11, 18, 255});
  void present();

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
