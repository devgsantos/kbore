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
  Quit
};

void platformInit();
void platformExit();
Button pollButtonBlocking();
void clearScreen();
void presentScreen();
void sleepMs(int ms);

} // namespace nstv
