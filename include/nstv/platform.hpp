#pragma once

#include <string>
#include <ctime>

namespace nstv {


struct PlatformLocalTime {
  int year = 1970;
  int month = 1;
  int day = 1;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int utcOffsetSeconds = 0;
  std::string timezoneName;
};

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
  FavoriteToggle,
  ShoulderLeft,
  ShoulderRight,
  Quit
};

enum class InputType {
  None,
  Button,
  TouchDown,
  TouchMove,
  TouchUp
};

struct InputEvent {
  InputType type = InputType::None;
  Button button = Button::None;
  int x = 0;
  int y = 0;
  int fingerId = -1;
};

void platformInit();
void platformExit();
Button pollButton();
Button pollButtonBlocking();
InputEvent pollInput();
void clearScreen();
void presentScreen();
void sleepMs(int ms);
std::time_t currentUnixTime();
bool localTimeFromUnix(std::time_t timestamp, PlatformLocalTime &out);
bool unixTimeFromLocal(int year, int month, int day, int hour, int minute, int second, std::time_t &out);
void setMediaPlaybackActive(bool active);
bool platformIsDockedMode();
std::string requestTextInput(const std::string &title, const std::string &initialValue = "", int maxLength = 512);

} // namespace nstv
