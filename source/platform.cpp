#include "nstv/platform.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>
#include <chrono>

#ifdef __SWITCH__
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <switch.h>
#endif

#include <curl/curl.h>


namespace nstv {

#ifdef __SWITCH__
static PadState pad;
#endif

void platformInit() {
#ifdef __SWITCH__
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  socketInitializeDefault();
  curl_global_init(CURL_GLOBAL_DEFAULT);
#else
  curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}

void platformExit() {
#ifdef __SWITCH__
  socketExit();
#endif
  curl_global_cleanup();
}

Button pollButtonBlocking() {
#ifdef __SWITCH__
  while (appletMainLoop()) {
    padUpdate(&pad);
    u64 down = padGetButtonsDown(&pad);
    if (down & HidNpadButton_Up) return Button::Up;
    if (down & HidNpadButton_Down) return Button::Down;
    if (down & HidNpadButton_Left) return Button::Left;
    if (down & HidNpadButton_Right) return Button::Right;
    if (down & HidNpadButton_A) return Button::Select;
    if (down & HidNpadButton_B) return Button::Back;
    if (down & HidNpadButton_X) return Button::Favorite;
    if (down & HidNpadButton_Plus) return Button::Menu;
    if (down & HidNpadButton_Minus) return Button::Quit;
    sleepMs(8);
  }
  return Button::Quit;
#else
  char ch;
  std::cin >> ch;
  switch (ch) {
    case 'w': return Button::Up;
    case 's': return Button::Down;
    case 'a': return Button::Left;
    case 'd': return Button::Right;
    case 'e': return Button::Select;
    case 'b': return Button::Back;
    case 'x': return Button::Favorite;
    case 'm': return Button::Menu;
    case 'q': return Button::Quit;
    default: return Button::None;
  }
#endif
}

void clearScreen() {
#ifdef __SWITCH__
  std::printf("\x1b[2J\x1b[H");
#else
  std::cout << "\x1b[2J\x1b[H";
#endif
}

void presentScreen() {
#ifdef __SWITCH__
  consoleUpdate(nullptr);
#else
  std::cout << std::flush;
#endif
}

void sleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace nstv
