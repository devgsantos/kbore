#include "nstv/platform.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
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

#ifdef NSTV_USE_SDL
#include <SDL2/SDL.h>
#endif


namespace nstv {

#ifdef __SWITCH__
static PadState pad;
static bool mediaPlaybackActive = false;
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
  if (mediaPlaybackActive) {
    appletSetMediaPlaybackState(false);
    mediaPlaybackActive = false;
  }
  socketExit();
#endif
  curl_global_cleanup();
}

Button pollButton() {
#ifdef __SWITCH__
  if (!appletMainLoop()) {
    return Button::Quit;
  }

  padUpdate(&pad);
  u64 down = padGetButtonsDown(&pad);

  if (down & HidNpadButton_Up) return Button::Up;
  if (down & HidNpadButton_Down) return Button::Down;
  if (down & HidNpadButton_Left) return Button::Left;
  if (down & HidNpadButton_Right) return Button::Right;
  if (down & HidNpadButton_A) return Button::Select;
  if (down & HidNpadButton_B) return Button::Back;
  if (down & HidNpadButton_L) return Button::ShoulderLeft;
  if (down & HidNpadButton_R) return Button::ShoulderRight;
  if (down & HidNpadButton_X) return Button::Favorite;
  if (down & HidNpadButton_Y) return Button::FavoriteToggle;
  if (down & HidNpadButton_Plus) return Button::Menu;
  if (down & HidNpadButton_Minus) return Button::Quit;

  return Button::None;
#else
#ifdef NSTV_USE_SDL
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) return Button::Quit;

    if (event.type == SDL_KEYDOWN) {
      switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_w:
          return Button::Up;
        case SDLK_DOWN:
        case SDLK_s:
          return Button::Down;
        case SDLK_LEFT:
        case SDLK_a:
          return Button::Left;
        case SDLK_RIGHT:
        case SDLK_d:
          return Button::Right;
        case SDLK_RETURN:
        case SDLK_SPACE:
        case SDLK_e:
          return Button::Select;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:
        case SDLK_b:
          return Button::Back;
        case SDLK_x:
          return Button::Favorite;
        case SDLK_y:
          return Button::FavoriteToggle;
        case SDLK_z:
        case SDLK_LEFTBRACKET:
          return Button::ShoulderLeft;
        case SDLK_c:
        case SDLK_RIGHTBRACKET:
          return Button::ShoulderRight;
        case SDLK_m:
        case SDLK_PLUS:
        case SDLK_EQUALS:
          return Button::Menu;
        case SDLK_q:
          return Button::Quit;
        default:
          break;
      }
    }
  }

  return Button::None;
#else
  return Button::None;
#endif
#endif
}

Button pollButtonBlocking() {
#ifdef __SWITCH__
  while (appletMainLoop()) {
    Button button = pollButton();
    if (button != Button::None) {
      return button;
    }

    sleepMs(8);
  }

  return Button::Quit;
#else
#ifdef NSTV_USE_SDL
  while (true) {
    Button button = pollButton();

    if (button != Button::None) {
      return button;
    }

    sleepMs(8);
  }
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
    case 'y': return Button::FavoriteToggle;
    case 'z': return Button::ShoulderLeft;
    case 'c': return Button::ShoulderRight;
    case 'm': return Button::Menu;
    case 'q': return Button::Quit;
    default: return Button::None;
  }
#endif
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

void setMediaPlaybackActive(bool active) {
#ifdef __SWITCH__
  if (mediaPlaybackActive == active) {
    return;
  }

  appletSetMediaPlaybackState(active);
  mediaPlaybackActive = active;
#else
  (void)active;
#endif
}


std::string requestTextInput(const std::string &title, const std::string &initialValue, int maxLength) {
  if (maxLength < 1) {
    maxLength = 1;
  }

#ifdef __SWITCH__
  SwkbdConfig swkbd;
  char buffer[1024] = {};

  std::string initial = initialValue;
  if (static_cast<int>(initial.size()) >= static_cast<int>(sizeof(buffer))) {
    initial = initial.substr(0, sizeof(buffer) - 1);
  }

  std::strncpy(buffer, initial.c_str(), sizeof(buffer) - 1);

  if (R_FAILED(swkbdCreate(&swkbd, 0))) {
    return initialValue;
  }

  swkbdConfigMakePresetDefault(&swkbd);
  swkbdConfigSetHeaderText(&swkbd, title.c_str());
  swkbdConfigSetGuideText(&swkbd, title.c_str());
  swkbdConfigSetInitialText(&swkbd, initial.c_str());
  swkbdConfigSetStringLenMax(&swkbd, static_cast<u32>(std::min(maxLength, 1023)));

  Result rc = swkbdShow(&swkbd, buffer, sizeof(buffer));

  swkbdClose(&swkbd);

  if (R_FAILED(rc)) {
    return "";
  }

  return std::string(buffer);
#else
#ifdef NSTV_USE_SDL
  // Host preview fallback. Input is typed in the terminal running the app.
  std::cout << title;

  if (!initialValue.empty()) {
    std::cout << " [" << initialValue << "]";
  }

  std::cout << ": " << std::flush;

  std::string value;
  std::getline(std::cin, value);

  if (value.empty()) {
    return initialValue;
  }

  if (static_cast<int>(value.size()) > maxLength) {
    value = value.substr(0, static_cast<std::size_t>(maxLength));
  }

  return value;
#else
  std::cout << title << ": " << std::flush;
  std::string value;
  std::getline(std::cin, value);
  return value.empty() ? initialValue : value;
#endif
#endif
}


} // namespace nstv
