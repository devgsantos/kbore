#include "nstv/platform.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
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
static bool touchActive = false;
static u32 touchFingerId = 0;
static int touchX = 0;
static int touchY = 0;
#endif

void platformInit() {
#ifdef __SWITCH__
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  hidInitializeTouchScreen();
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

std::time_t currentUnixTime() {
#ifdef __SWITCH__
  u64 timestamp = 0;
  Result rc = timeGetCurrentTime(TimeType_UserSystemClock, &timestamp);
  if (R_SUCCEEDED(rc)) {
    return static_cast<std::time_t>(timestamp);
  }
  std::printf("[KBORE][TIME] timeGetCurrentTime failed rc=0x%x; falling back to std::time\n", static_cast<unsigned int>(rc));
#endif
  return std::time(nullptr);
}

namespace {
#ifndef __SWITCH__
int portableLocalUtcOffsetSeconds(std::time_t value) {
  std::tm localTime{};
  std::tm utcTime{};

#if defined(_WIN32)
  localtime_s(&localTime, &value);
  gmtime_s(&utcTime, &value);
#else
  std::tm *result = std::localtime(&value);
  if (result) {
    localTime = *result;
  }

  result = std::gmtime(&value);
  if (result) {
    utcTime = *result;
  }
#endif

  return static_cast<int>(std::difftime(std::mktime(&localTime), std::mktime(&utcTime)));
}
#endif
} // namespace

bool localTimeFromUnix(std::time_t timestamp, PlatformLocalTime &out) {
#ifdef __SWITCH__
  TimeCalendarTime cal{};
  TimeCalendarAdditionalInfo info{};
  Result rc = timeToCalendarTimeWithMyRule(static_cast<u64>(timestamp), &cal, &info);
  if (R_SUCCEEDED(rc)) {
    out.year = cal.year;
    out.month = cal.month;
    out.day = cal.day;
    out.hour = cal.hour;
    out.minute = cal.minute;
    out.second = cal.second;
    out.utcOffsetSeconds = info.offset;
    out.timezoneName = info.timezoneName;
    return true;
  }
  std::printf("[KBORE][TIME] timeToCalendarTimeWithMyRule failed rc=0x%x; falling back to std::localtime\n", static_cast<unsigned int>(rc));
#endif

  std::tm localTime{};
#if defined(_WIN32)
  localtime_s(&localTime, &timestamp);
#else
  std::tm *result = std::localtime(&timestamp);
  if (!result) {
    return false;
  }
  localTime = *result;
#endif

  out.year = localTime.tm_year + 1900;
  out.month = localTime.tm_mon + 1;
  out.day = localTime.tm_mday;
  out.hour = localTime.tm_hour;
  out.minute = localTime.tm_min;
  out.second = localTime.tm_sec;
#ifndef __SWITCH__
  out.utcOffsetSeconds = portableLocalUtcOffsetSeconds(timestamp);
#endif
  return true;
}

bool unixTimeFromLocal(int year, int month, int day, int hour, int minute, int second, std::time_t &out) {
#ifdef __SWITCH__
  TimeCalendarTime cal{};
  cal.year = static_cast<u16>(year);
  cal.month = static_cast<u8>(month);
  cal.day = static_cast<u8>(day);
  cal.hour = static_cast<u8>(hour);
  cal.minute = static_cast<u8>(minute);
  cal.second = static_cast<u8>(second);

  u64 timestamps[2] = {};
  s32 timestampCount = 0;
  Result rc = timeToPosixTimeWithMyRule(&cal, timestamps, 2, &timestampCount);
  if (R_SUCCEEDED(rc) && timestampCount > 0) {
    out = static_cast<std::time_t>(timestamps[0]);
    return true;
  }
  std::printf("[KBORE][TIME] timeToPosixTimeWithMyRule failed rc=0x%x count=%d; falling back to std::mktime\n", static_cast<unsigned int>(rc), static_cast<int>(timestampCount));
#endif

  std::tm localTime{};
  localTime.tm_year = year - 1900;
  localTime.tm_mon = month - 1;
  localTime.tm_mday = day;
  localTime.tm_hour = hour;
  localTime.tm_min = minute;
  localTime.tm_sec = second;
  localTime.tm_isdst = -1;
  out = std::mktime(&localTime);
  return out != static_cast<std::time_t>(-1);
}

InputEvent pollInput() {
#ifdef __SWITCH__
  if (!appletMainLoop()) {
    return {InputType::Button, Button::Quit};
  }

  padUpdate(&pad);
  u64 down = padGetButtonsDown(&pad);

  if (down & HidNpadButton_Up) return {InputType::Button, Button::Up};
  if (down & HidNpadButton_Down) return {InputType::Button, Button::Down};
  if (down & HidNpadButton_Left) return {InputType::Button, Button::Left};
  if (down & HidNpadButton_Right) return {InputType::Button, Button::Right};
  if (down & HidNpadButton_A) return {InputType::Button, Button::Select};
  if (down & HidNpadButton_B) return {InputType::Button, Button::Back};
  if (down & HidNpadButton_L) return {InputType::Button, Button::ShoulderLeft};
  if (down & HidNpadButton_R) return {InputType::Button, Button::ShoulderRight};
  if (down & HidNpadButton_X) return {InputType::Button, Button::Favorite};
  if (down & HidNpadButton_Y) return {InputType::Button, Button::FavoriteToggle};
  if (down & HidNpadButton_Plus) return {InputType::Button, Button::Menu};
  if (down & HidNpadButton_Minus) return {InputType::Button, Button::Quit};

  HidTouchScreenState state{};
  const bool hasTouch = hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
  if (hasTouch) {
    const HidTouchState *touch = &state.touches[0];
    if (touchActive) {
      for (int i = 0; i < state.count; ++i) {
        if (state.touches[i].finger_id == touchFingerId) {
          touch = &state.touches[i];
          break;
        }
      }
    }

    touchX = static_cast<int>(touch->x);
    touchY = static_cast<int>(touch->y);
    if (!touchActive) {
      touchActive = true;
      touchFingerId = touch->finger_id;
      return {InputType::TouchDown, Button::None, touchX, touchY, static_cast<int>(touchFingerId)};
    }
    return {InputType::TouchMove, Button::None, touchX, touchY, static_cast<int>(touchFingerId)};
  }

  if (touchActive) {
    touchActive = false;
    return {InputType::TouchUp, Button::None, touchX, touchY, static_cast<int>(touchFingerId)};
  }

  return {};
#else
#ifdef NSTV_USE_SDL
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) return {InputType::Button, Button::Quit};

    int windowW = 1280;
    int windowH = 720;
    SDL_Window *window = SDL_GetWindowFromID(event.window.windowID);
    if (window) SDL_GetWindowSize(window, &windowW, &windowH);
    auto logicalX = [&](int x) { return windowW > 0 ? x * 1280 / windowW : x; };
    auto logicalY = [&](int y) { return windowH > 0 ? y * 720 / windowH : y; };

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
      return {InputType::TouchDown, Button::None, logicalX(event.button.x), logicalY(event.button.y), 0};
    }
    if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK)) {
      return {InputType::TouchMove, Button::None, logicalX(event.motion.x), logicalY(event.motion.y), 0};
    }
    if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
      return {InputType::TouchUp, Button::None, logicalX(event.button.x), logicalY(event.button.y), 0};
    }

    if (event.type == SDL_KEYDOWN) {
      switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_w:
          return {InputType::Button, Button::Up};
        case SDLK_DOWN:
        case SDLK_s:
          return {InputType::Button, Button::Down};
        case SDLK_LEFT:
        case SDLK_a:
          return {InputType::Button, Button::Left};
        case SDLK_RIGHT:
        case SDLK_d:
          return {InputType::Button, Button::Right};
        case SDLK_RETURN:
        case SDLK_SPACE:
        case SDLK_e:
          return {InputType::Button, Button::Select};
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:
        case SDLK_b:
          return {InputType::Button, Button::Back};
        case SDLK_x:
          return {InputType::Button, Button::Favorite};
        case SDLK_y:
          return {InputType::Button, Button::FavoriteToggle};
        case SDLK_z:
        case SDLK_LEFTBRACKET:
          return {InputType::Button, Button::ShoulderLeft};
        case SDLK_c:
        case SDLK_RIGHTBRACKET:
          return {InputType::Button, Button::ShoulderRight};
        case SDLK_m:
        case SDLK_PLUS:
        case SDLK_EQUALS:
          return {InputType::Button, Button::Menu};
        case SDLK_q:
          return {InputType::Button, Button::Quit};
        default:
          break;
      }
    }
  }

  return {};
#else
  return {};
#endif
#endif
}

Button pollButton() {
  const InputEvent event = pollInput();
  return event.type == InputType::Button ? event.button : Button::None;
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

bool platformIsDockedMode() {
#ifdef __SWITCH__
  AppletOperationMode mode = appletGetOperationMode();
  return mode == AppletOperationMode_Console;
#else
  return true;
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
