#pragma once

#include <string>

namespace nstv {

enum class Button {
  None,
  Up,
  Down,
  Left,
  Right,
  Select,
  Back,
  Menu,
  Favorite,
  ShoulderLeft,
  ShoulderRight,
  Quit
};

void platformInit();
void platformExit();
Button pollButton();
Button pollButtonBlocking();
void clearScreen();
void presentScreen();
void sleepMs(int ms);
void setMediaPlaybackActive(bool active);
std::string requestTextInput(const std::string &title, const std::string &initialValue = "", int maxLength = 512);

} // namespace nstv
