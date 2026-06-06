#include "nstv/app.hpp"
#include "nstv/log.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <limits>

namespace nstv {

namespace {
const char *ANSI_RESET = "\x1b[0m";
const char *ANSI_BOLD = "\x1b[1m";
const char *FG_WHITE = "\x1b[38;5;255m";
const char *FG_MUTED = "\x1b[38;5;146m";
const char *FG_BLUE = "\x1b[38;5;39m";
const char *FG_GREEN = "\x1b[38;5;46m";
const char *FG_YELLOW = "\x1b[38;5;220m";
const char *BG_APP = "\x1b[48;5;16m";
const char *BG_PANEL = "\x1b[48;5;17m";
const char *BG_PANEL_DARK = "\x1b[48;5;234m";
const char *BG_SELECTED = "\x1b[48;5;27m";
const char *BG_CARD = "\x1b[48;5;235m";

std::string repeat(char ch, int count) {
  if (count <= 0) return {};
  return std::string(static_cast<std::size_t>(count), ch);
}

long long nowMs() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    clock::now().time_since_epoch()
  ).count();
}

std::string formatPlaybackTime(int64_t ms) {
  if (ms < 0) {
    ms = 0;
  }

  const int64_t totalSeconds = ms / 1000;
  const int64_t hours = totalSeconds / 3600;
  const int64_t minutes = (totalSeconds / 60) % 60;
  const int64_t seconds = totalSeconds % 60;

  char buffer[32] = {};

  if (hours > 0) {
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", hours, minutes, seconds);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", minutes, seconds);
  }

  return buffer;
}

bool isVodType(StreamType type) {
  return type == StreamType::Movies || type == StreamType::Series;
}

std::string formatSystemClockTime() {
  std::time_t rawTime = std::time(nullptr);

  if (rawTime <= 0) {
    return "--:--";
  }

  std::tm localTime{};

#if defined(_WIN32)
  localtime_s(&localTime, &rawTime);
#else
  std::tm *result = std::localtime(&rawTime);

  if (!result) {
    return "--:--";
  }

  localTime = *result;
#endif

  char buffer[8] = {};
  std::snprintf(
    buffer,
    sizeof(buffer),
    "%02d:%02d",
    localTime.tm_hour,
    localTime.tm_min
  );

  return buffer;
}

int parseFixedInt(const std::string &value, std::size_t pos, std::size_t len, int fallback = 0) {
  if (pos + len > value.size()) {
    return fallback;
  }

  int out = 0;
  for (std::size_t i = pos; i < pos + len; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
      return fallback;
    }
    out = out * 10 + (value[i] - '0');
  }

  return out;
}

long long daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

long long epochFromUtcParts(int year, int month, int day, int hour, int minute, int second) {
  return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400LL +
    hour * 3600LL +
    minute * 60LL +
    second;
}

bool parseZoneOffsetSeconds(const std::string &zone, int &offsetSeconds) {
  if (zone == "Z" || zone == "z") {
    offsetSeconds = 0;
    return true;
  }

  if (zone.size() < 5 || (zone[0] != '+' && zone[0] != '-')) {
    return false;
  }

  const int sign = zone[0] == '-' ? -1 : 1;
  const int hour = parseFixedInt(zone, 1, 2, -1);
  const std::size_t minutePos = zone.size() > 3 && zone[3] == ':' ? 4 : 3;
  const int minute = parseFixedInt(zone, minutePos, 2, -1);

  if (hour < 0 || minute < 0) {
    return false;
  }

  offsetSeconds = sign * (hour * 3600 + minute * 60);
  return true;
}

std::string trimEpgValue(const std::string &value) {
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }

  std::size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }

  return value.substr(first, last - first);
}

enum class EpgMeridiem {
  None,
  AM,
  PM
};

EpgMeridiem detectMeridiemInRange(const std::string &text, std::size_t start, std::size_t end) {
  if (start >= text.size()) {
    return EpgMeridiem::None;
  }

  end = std::min(end, text.size());

  for (std::size_t i = start; i < end; ++i) {
    const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    if (first != 'a' && first != 'p') {
      continue;
    }

    std::size_t j = i + 1;
    while (j < end && (std::isspace(static_cast<unsigned char>(text[j])) || text[j] == '.')) {
      ++j;
    }

    if (j >= end || static_cast<char>(std::tolower(static_cast<unsigned char>(text[j]))) != 'm') {
      continue;
    }

    std::size_t k = j + 1;
    while (k < end && text[k] == '.') {
      ++k;
    }

    // Reject words such as "America" while still accepting compact values like
    // "10:25PM". EPG time values must use 24h internally; AM/PM is only parsed
    // so PM programmes are not accidentally matched as morning programmes.
    if (k < end && std::isalpha(static_cast<unsigned char>(text[k]))) {
      continue;
    }

    return first == 'p' ? EpgMeridiem::PM : EpgMeridiem::AM;
  }

  return EpgMeridiem::None;
}

bool normalizeHourWithMeridiem(int &hour, EpgMeridiem meridiem) {
  if (meridiem == EpgMeridiem::None) {
    return hour >= 0 && hour <= 23;
  }

  // Some providers incorrectly append AM/PM to an already-24h value. Keep the
  // 24h value instead of rejecting the programme completely.
  if (hour == 0 || hour > 12) {
    return hour >= 0 && hour <= 23;
  }

  if (meridiem == EpgMeridiem::AM) {
    if (hour == 12) {
      hour = 0;
    }
  } else if (hour != 12) {
    hour += 12;
  }

  return hour >= 0 && hour <= 23;
}

struct ParsedClockEpgTime {
  int minutesOfDay = -1;
  int seconds = 0;
  int rawHour = -1;
  int rawMinute = -1;
  bool hasMeridiem = false;
  bool isPm = false;
};

int normalize12HourCandidateMinutes(int rawHour, int rawMinute, bool pm) {
  int hour = rawHour;
  if (hour == 12) {
    hour = pm ? 12 : 0;
  } else if (pm) {
    hour += 12;
  }
  return hour * 60 + rawMinute;
}

int positiveClockDurationMinutes(int startMinutes, int stopMinutes) {
  int duration = stopMinutes - startMinutes;
  if (duration <= 0) {
    duration += 24 * 60;
  }
  return duration;
}

bool canInferMissingMeridiem(const ParsedClockEpgTime &clock) {
  return !clock.hasMeridiem && clock.rawHour >= 1 && clock.rawHour <= 12;
}

void inferMissingMeridiemForClockRange(ParsedClockEpgTime &start, bool hasStart, ParsedClockEpgTime &stop, bool hasStop) {
  if (!hasStart || !hasStop) {
    return;
  }

  if (canInferMissingMeridiem(start) && stop.hasMeridiem) {
    const int amStart = normalize12HourCandidateMinutes(start.rawHour, start.rawMinute, false);
    const int pmStart = normalize12HourCandidateMinutes(start.rawHour, start.rawMinute, true);
    const int amDuration = positiveClockDurationMinutes(amStart, stop.minutesOfDay);
    const int pmDuration = positiveClockDurationMinutes(pmStart, stop.minutesOfDay);
    start.minutesOfDay = pmDuration < amDuration ? pmStart : amStart;
    start.hasMeridiem = true;
    start.isPm = pmDuration < amDuration;
  }

  if (canInferMissingMeridiem(stop) && start.hasMeridiem) {
    const int amStop = normalize12HourCandidateMinutes(stop.rawHour, stop.rawMinute, false);
    const int pmStop = normalize12HourCandidateMinutes(stop.rawHour, stop.rawMinute, true);
    const int amDuration = positiveClockDurationMinutes(start.minutesOfDay, amStop);
    const int pmDuration = positiveClockDurationMinutes(start.minutesOfDay, pmStop);
    stop.minutesOfDay = pmDuration < amDuration ? pmStop : amStop;
    stop.hasMeridiem = true;
    stop.isPm = pmDuration < amDuration;
  }
}

bool parseEpgTime(const std::string &value, std::time_t &timestamp) {
  const std::string text = trimEpgValue(value);
  if (text.empty()) {
    return false;
  }

  const bool allDigits = std::all_of(text.begin(), text.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch));
  });

  if (allDigits && text.size() >= 10 && text.size() <= 13) {
    long long raw = 0;

    try {
      raw = std::stoll(text);
    } catch (...) {
      return false;
    }

    if (text.size() == 13) raw /= 1000;
    if (raw <= 0) return false;

    timestamp = static_cast<std::time_t>(raw);
    return true;
  }

  std::size_t dateTimePos = text.find('T');
  if (dateTimePos == std::string::npos && text.size() >= 16 && text[4] == '-' && text[7] == '-' && text[10] == ' ') {
    dateTimePos = 10;
  }

  if (dateTimePos != std::string::npos && text.size() >= dateTimePos + 6) {
    const int year = parseFixedInt(text, 0, 4, -1);
    const int month = parseFixedInt(text, 5, 2, -1);
    const int day = parseFixedInt(text, 8, 2, -1);
    int hour = parseFixedInt(text, dateTimePos + 1, 2, -1);
    const int minute = parseFixedInt(text, dateTimePos + 4, 2, -1);
    const int second = text.size() >= dateTimePos + 9 ? parseFixedInt(text, dateTimePos + 7, 2, 0) : 0;

    std::size_t zonePos = text.find_first_of("Zz+-", dateTimePos + 6);
    const std::size_t meridiemEnd = zonePos == std::string::npos ? text.size() : zonePos;
    const EpgMeridiem meridiem = detectMeridiemInRange(text, dateTimePos + 6, meridiemEnd);

    if (year < 0 || month < 1 || day < 1 || hour < 0 || minute < 0 || minute > 59 || second < 0 || second > 59 ||
        !normalizeHourWithMeridiem(hour, meridiem)) {
      return false;
    }

    if (zonePos != std::string::npos) {
      int offsetSeconds = 0;
      if (parseZoneOffsetSeconds(text.substr(zonePos), offsetSeconds)) {
        timestamp = static_cast<std::time_t>(
          epochFromUtcParts(year, month, day, hour, minute, second) - offsetSeconds
        );
        return true;
      }
    }

    std::tm localTime{};
    localTime.tm_year = year - 1900;
    localTime.tm_mon = month - 1;
    localTime.tm_mday = day;
    localTime.tm_hour = hour;
    localTime.tm_min = minute;
    localTime.tm_sec = second;
    localTime.tm_isdst = -1;
    timestamp = std::mktime(&localTime);
    return timestamp != static_cast<std::time_t>(-1);
  }

  // XMLTV common format: YYYYMMDDHHMMSS +/-ZZZZ
  if (text.size() >= 14 && std::all_of(text.begin(), text.begin() + 14, [](char ch) {
        return std::isdigit(static_cast<unsigned char>(ch));
      })) {
    const int year = parseFixedInt(text, 0, 4, -1);
    const int month = parseFixedInt(text, 4, 2, -1);
    const int day = parseFixedInt(text, 6, 2, -1);
    const int hour = parseFixedInt(text, 8, 2, -1);
    const int minute = parseFixedInt(text, 10, 2, -1);
    const int second = parseFixedInt(text, 12, 2, 0);

    if (year < 0 || month < 1 || day < 1 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
      return false;
    }

    std::size_t zonePos = text.find_first_of("+-", 14);
    if (zonePos != std::string::npos) {
      int offsetSeconds = 0;
      if (parseZoneOffsetSeconds(text.substr(zonePos), offsetSeconds)) {
        timestamp = static_cast<std::time_t>(
          epochFromUtcParts(year, month, day, hour, minute, second) - offsetSeconds
        );
        return true;
      }
    }

    std::tm localTime{};
    localTime.tm_year = year - 1900;
    localTime.tm_mon = month - 1;
    localTime.tm_mday = day;
    localTime.tm_hour = hour;
    localTime.tm_min = minute;
    localTime.tm_sec = second;
    localTime.tm_isdst = -1;
    timestamp = std::mktime(&localTime);
    return timestamp != static_cast<std::time_t>(-1);
  }

  return false;
}

bool parseClockOnlyEpgTimeDetailed(const std::string &value, ParsedClockEpgTime &clock) {
  const std::string text = trimEpgValue(value);
  if (text.size() < 4) {
    return false;
  }

  std::size_t colon = text.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 2 || colon + 2 >= text.size()) {
    return false;
  }

  int hour = 0;
  for (std::size_t i = 0; i < colon; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
      return false;
    }
    hour = hour * 10 + (text[i] - '0');
  }

  if (!std::isdigit(static_cast<unsigned char>(text[colon + 1])) ||
      !std::isdigit(static_cast<unsigned char>(text[colon + 2]))) {
    return false;
  }

  const int minute = (text[colon + 1] - '0') * 10 + (text[colon + 2] - '0');
  int parsedSeconds = 0;
  std::size_t timeEnd = colon + 3;

  if (text.size() >= colon + 6 && text[colon + 3] == ':' &&
      std::isdigit(static_cast<unsigned char>(text[colon + 4])) &&
      std::isdigit(static_cast<unsigned char>(text[colon + 5]))) {
    parsedSeconds = (text[colon + 4] - '0') * 10 + (text[colon + 5] - '0');
    timeEnd = colon + 6;
  }

  const EpgMeridiem meridiem = detectMeridiemInRange(text, timeEnd, text.size());
  const int rawHour = hour;

  if (minute < 0 || minute > 59 || parsedSeconds < 0 || parsedSeconds > 59 ||
      !normalizeHourWithMeridiem(hour, meridiem)) {
    return false;
  }

  clock.minutesOfDay = hour * 60 + minute;
  clock.seconds = parsedSeconds;
  clock.rawHour = rawHour;
  clock.rawMinute = minute;
  clock.hasMeridiem = meridiem != EpgMeridiem::None;
  clock.isPm = meridiem == EpgMeridiem::PM;
  return true;
}

bool parseClockOnlyEpgTime(const std::string &value, int &minutesOfDay, int &seconds) {
  ParsedClockEpgTime clock;
  if (!parseClockOnlyEpgTimeDetailed(value, clock)) {
    return false;
  }

  minutesOfDay = clock.minutesOfDay;
  seconds = clock.seconds;
  return true;
}

std::time_t localDateWithClock(std::time_t anchor, int minutesOfDay, int seconds, int dayOffset = 0) {
  std::tm localDate{};

#if defined(_WIN32)
  localtime_s(&localDate, &anchor);
#else
  std::tm *result = std::localtime(&anchor);
  if (!result) {
    return static_cast<std::time_t>(-1);
  }
  localDate = *result;
#endif

  localDate.tm_mday += dayOffset;
  localDate.tm_hour = minutesOfDay / 60;
  localDate.tm_min = minutesOfDay % 60;
  localDate.tm_sec = seconds;
  localDate.tm_isdst = -1;
  return std::mktime(&localDate);
}

struct ResolvedEpgRange {
  bool hasAnyTime = false;
  bool hasStartTime = false;
  bool hasDatedTime = false;
  bool validRange = false;
  std::time_t start = 0;
  std::time_t stop = 0;
};

ResolvedEpgRange resolveEpgRangeForNow(const EpgProgram &program, std::time_t now, const EpgPage *page = nullptr, std::size_t index = 0) {
  ResolvedEpgRange range;

  std::time_t start{};
  std::time_t stop{};
  const bool hasDatedStart = parseEpgTime(program.start, start);
  bool hasDatedStop = parseEpgTime(program.stop, stop);

  ParsedClockEpgTime startClockParsed;
  ParsedClockEpgTime stopClockParsed;
  const bool hasClockStart = !hasDatedStart && parseClockOnlyEpgTimeDetailed(program.start, startClockParsed);
  const bool hasClockStop = !hasDatedStop && parseClockOnlyEpgTimeDetailed(program.stop, stopClockParsed);

  inferMissingMeridiemForClockRange(startClockParsed, hasClockStart, stopClockParsed, hasClockStop);

  int startClock = startClockParsed.minutesOfDay;
  int stopClock = stopClockParsed.minutesOfDay;
  int startSecond = startClockParsed.seconds;
  int stopSecond = stopClockParsed.seconds;

  range.hasAnyTime = hasDatedStart || hasDatedStop || hasClockStart || hasClockStop;
  range.hasStartTime = hasDatedStart || hasClockStart;
  range.hasDatedTime = hasDatedStart || hasDatedStop;

  if (hasDatedStart) {
    range.start = start;
  } else if (hasClockStart) {
    int dayOffset = 0;
    if (hasClockStop && stopClock <= startClock) {
      std::tm nowLocal{};
#if defined(_WIN32)
      localtime_s(&nowLocal, &now);
#else
      std::tm *nowResult = std::localtime(&now);
      if (nowResult) {
        nowLocal = *nowResult;
      }
#endif
      const int nowClock = nowLocal.tm_hour * 60 + nowLocal.tm_min;
      if (nowClock < stopClock) {
        dayOffset = -1;
      }
    }

    range.start = localDateWithClock(now, startClock, startSecond, dayOffset);
  }

  if (hasDatedStop) {
    range.stop = stop;
  } else if (hasClockStop) {
    int dayOffset = 0;
    if (hasClockStart && stopClock <= startClock) {
      std::tm nowLocal{};
#if defined(_WIN32)
      localtime_s(&nowLocal, &now);
#else
      std::tm *nowResult = std::localtime(&now);
      if (nowResult) {
        nowLocal = *nowResult;
      }
#endif
      const int nowClock = nowLocal.tm_hour * 60 + nowLocal.tm_min;
      dayOffset = nowClock < stopClock ? 0 : 1;
    }

    const std::time_t anchor = hasDatedStart ? range.start : now;
    range.stop = localDateWithClock(anchor, stopClock, stopSecond, dayOffset);
  }

  const bool hasStart = hasDatedStart || hasClockStart;
  bool hasStop = hasDatedStop || hasClockStop;

  if (hasStart && !hasStop && page) {
    for (std::size_t j = index + 1; j < page->programs.size(); ++j) {
      ResolvedEpgRange next = resolveEpgRangeForNow(page->programs[j], now, nullptr, 0);
      if (next.hasStartTime && next.start > range.start) {
        range.stop = next.start;
        hasStop = true;
        break;
      }
    }

    if (!hasStop) {
      range.stop = range.start + 3 * 60 * 60;
      hasStop = true;
    }
  }

  if (hasStart && hasStop && range.start != static_cast<std::time_t>(-1) && range.stop != static_cast<std::time_t>(-1)) {
    if (range.stop <= range.start && (hasClockStart || hasClockStop)) {
      range.stop += 24 * 60 * 60;
    }

    range.validRange = range.stop > range.start;
  }

  return range;
}

bool epgPageHasDatedTimes(const EpgPage &page) {
  for (const EpgProgram &program : page.programs) {
    std::time_t ignored{};
    if (parseEpgTime(program.start, ignored) || parseEpgTime(program.stop, ignored)) {
      return true;
    }
  }

  return false;
}

std::string formatLocalClock(std::time_t timestamp) {
  std::tm localTime{};

#if defined(_WIN32)
  localtime_s(&localTime, &timestamp);
#else
  std::tm *result = std::localtime(&timestamp);
  if (!result) {
    return "--:--";
  }
  localTime = *result;
#endif

  char buffer[8] = {};
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  return buffer;
}

std::string formatEpgClock(const std::string &value) {
  std::time_t timestamp{};
  if (parseEpgTime(value, timestamp)) {
    return formatLocalClock(timestamp);
  }

  ParsedClockEpgTime clock;
  if (parseClockOnlyEpgTimeDetailed(value, clock)) {
    char buffer[8] = {};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", clock.minutesOfDay / 60, clock.minutesOfDay % 60);
    return buffer;
  }

  return value.size() > 5 ? value.substr(0, 5) : value;
}

int currentProgramIndex(const EpgPage &page) {
  if (page.programs.empty()) {
    return -1;
  }

  const std::time_t now = std::time(nullptr);
  bool anyTimed = false;

  for (std::size_t i = 0; i < page.programs.size(); ++i) {
    const ResolvedEpgRange range = resolveEpgRangeForNow(page.programs[i], now, &page, i);
    anyTimed = anyTimed || range.hasAnyTime;

    // Match against the resolved full timestamp, not just HH:mm. Full dated EPG
    // values must overlap the current date/time. Clock-only ranges are anchored
    // to the current local service date, with midnight-crossing handled.
    if (range.validRange && now >= range.start && now < range.stop) {
      return static_cast<int>(i);
    }
  }

  // Do not fall back to the first/nearest programme when the EPG page has times.
  // Example: at 10:46, a clock-only 03:00-05:00 programme is a valid timed range
  // from today, but it is not current, so the UI must show unavailable instead
  // of presenting it as NOW.
  return anyTimed ? -1 : 0;
}

std::string formatEpgRange(const EpgProgram &program) {
  const std::string start = formatEpgClock(program.start);
  const std::string stop = formatEpgClock(program.stop);

  if (start == "--:--" && stop == "--:--") {
    return "";
  }

  if (stop == "--:--") {
    return start;
  }

  if (start == "--:--") {
    return stop;
  }

  return start + "-" + stop;
}


std::vector<std::string> wrapText(const std::string &text, std::size_t maxCharsPerLine) {
  std::vector<std::string> lines;

  if (text.empty()) {
    return lines;
  }

  std::string current;
  std::string word;

  auto flushWord = [&]() {
    if (word.empty()) {
      return;
    }

    if (word.size() > maxCharsPerLine) {
      if (!current.empty()) {
        lines.push_back(current);
        current.clear();
      }

      for (std::size_t i = 0; i < word.size(); i += maxCharsPerLine) {
        lines.push_back(word.substr(i, maxCharsPerLine));
      }

      word.clear();
      return;
    }

    if (current.empty()) {
      current = word;
    } else if (current.size() + 1 + word.size() <= maxCharsPerLine) {
      current += " ";
      current += word;
    } else {
      lines.push_back(current);
      current = word;
    }

    word.clear();
  };

  for (char ch : text) {
    if (ch == ' ' || ch == '\n' || ch == '\t') {
      flushWord();

      if (ch == '\n' && !current.empty()) {
        lines.push_back(current);
        current.clear();
      }

      continue;
    }

    word.push_back(ch);
  }

  flushWord();

  if (!current.empty()) {
    lines.push_back(current);
  }

  return lines;
}

void drawWrappedText(
  Graphics &gfx,
  const std::string &text,
  int x,
  int y,
  int maxLines,
  std::size_t maxCharsPerLine,
  int scale,
  Color color,
  bool bold = false
) {
  const std::vector<std::string> lines = wrapText(text, maxCharsPerLine);

  const int lineHeight = scale <= 1 ? 14 : 18;

  for (int i = 0; i < maxLines && i < static_cast<int>(lines.size()); ++i) {
    std::string line = lines[static_cast<std::size_t>(i)];

    if (i == maxLines - 1 && static_cast<int>(lines.size()) > maxLines) {
      if (line.size() > 3) {
        line = line.substr(0, line.size() - 3) + "...";
      } else {
        line += "...";
      }
    }

    gfx.drawText(line, x, y + i * lineHeight, scale, color, bold);
  }
}

std::string safePlaylistId(const std::string &name, Provider provider) {
  std::string out = toString(provider);

  for (char ch : name) {
    unsigned char c = static_cast<unsigned char>(ch);

    if (std::isalnum(c)) {
      out.push_back('-');
      out.push_back(static_cast<char>(std::tolower(c)));
    } else if ((ch == '-' || ch == '_') && !out.empty() && out.back() != '-') {
      out.push_back('-');
    }
  }

  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }

  return out + "-" + std::to_string(nowMs());
}

std::string trimText(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }

  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }

  return value;
}

std::string trimTrailingSlashLocal(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }

  return value;
}

int nodeCount(const MediaNode &node) {
  if (node.totalItems > 0) return node.totalItems;
  if (node.totalChannels > 0) return node.totalChannels;
  if (node.childCount > 0) return node.childCount;
  return static_cast<int>(node.children.size());
}

bool nodeCanHaveChildren(const MediaNode &node) {
  return node.hasChildren || node.childCount > 0 || !node.children.empty();
}

bool nodeChildrenAreItems(const MediaNode &node) {
  if (node.children.empty()) {
    return false;
  }

  bool hasPlayableItem = false;

  for (const auto &child : node.children) {
    if (nodeCanHaveChildren(child)) {
      return false;
    }

    if (child.playable || !child.url.empty()) {
      hasPlayableItem = true;
    }
  }

  return hasPlayableItem;
}

}

static int windowStart(int selected, int size, int maxRows);
static int gridWindowStart(int selected, int size, int columns, int rows);

App::App() : api_(loadConfig()), player_(createPlayerBackend()) {
  splashStartedAtMs_ = nowMs();
  splashVisible_ = true;

  state_.config = loadConfig();
  api_ = ParserApiClient(state_.config);

  const PlaylistConfig *playlist = activePlaylist();

  if (playlist) {
    startPlaylistLoad(*playlist);
  } else if (loadManifest(state_.manifest) && (!state_.manifest.nodes.empty() || !state_.manifest.types.empty())) {
    state_.hasManifest = true;
    state_.message =
      "Loaded cached manifest: " +
      std::to_string(state_.manifest.totalChannels) +
      " channels";
  } else {
    state_.message = "Press + to add an M3U or Xtream playlist";
  }

  startChannelWorker();
  startEpgWorker();
}

App::~App() {
  setMediaPlaybackActive(false);

  if (player_) {
    player_->close();
    player_.reset();
  }

  stopChannelWorker();
  stopEpgWorker();
}

int App::run() {
  render();

  while (state_.running) {
    const bool hadPlaylistLoad = playlistLoadActive();
    updatePlaylistLoad();
    updateCacheSave();

    if (hadPlaylistLoad && !playlistLoadActive()) {
      render();
      sleepMs(16);
      continue;
    }

    if (splashVisible_) {
      Button button = pollButton();

      if (button == Button::Quit) {
        state_.running = false;
        break;
      }

      render();
      sleepMs(16);
      continue;
    }

    if (playlistLoadActive()) {
      Button button = pollButton();

      if (button == Button::Quit) {
        state_.running = false;
        break;
      }

      render();
      sleepMs(16);
      continue;
    }

    if (state_.screen == ScreenId::Player) {
      Button button = pollButton();

      if (button != Button::None) {
        handle(button);
      }

      render();
      sleepMs(16);
      continue;
    }

    Button button = pollButton();
    if (button != Button::None) {
      handle(button);
    }
    render();
    sleepMs(16);
  }

  setMediaPlaybackActive(false);
  return 0;
}

void App::handle(Button button) {
  if (button == Button::Quit) {
    setMediaPlaybackActive(false);

    if (player_) {
      logLine("[KBORE][PLAYBACK][LIFECYCLE] closing player on Quit");
      player_->close();
      player_.reset();
      gfx_.resumeAfterNativeVideo();
    }

    state_.running = false;
    return;
  }

  switch (state_.screen) {
    case ScreenId::Dashboard: handleDashboard(button); break;
    case ScreenId::AddPlaylist: handleAddPlaylist(button); break;
    case ScreenId::Player: {
      const long long now = nowMs();
      const bool isOpen = player_ && player_->isOpen();
      const bool isPaused = player_ && player_->isPaused();
      const bool overlayWasVisible =
        !state_.playerFrameSeen ||
        now < state_.playerOverlayUntilMs ||
        isPaused ||
        !isOpen;

      if (button == Button::Back) {
        setMediaPlaybackActive(false);

        if (player_) {
          logLine("[KBORE][PLAYBACK][LIFECYCLE] closing player on Back");
          player_->close();
          player_.reset();
        }

        gfx_.resumeAfterNativeVideo();

        state_.screen = ScreenId::Dashboard;
        state_.message = "Playback stopped";
        state_.playerStarted = false;
        state_.playerFrameSeen = false;
        state_.playerLoading = false;
        state_.playerLoadFailed = false;
        state_.playerErrorMessage.clear();
        state_.hasPlaybackChannel = false;
        break;
      }

      if (button != Button::None) {
        state_.playerOverlayUntilMs = now + 5000;
      }

      // Playback commands are intentional only while the overlay is visible.
      // The first button press wakes the overlay; B remains the immediate exit.
      if (!overlayWasVisible) {
        break;
      }

      const bool isVod =
        state_.hasPlaybackChannel &&
        isVodType(state_.playbackChannel.type) &&
        player_ &&
        player_->canSeek();

      if (button == Button::Select) {
        if (player_) {
          player_->togglePause();
          state_.message = player_->isPaused() ? "Playback paused" : "Playback resumed";
        }
      } else if (isVod && button == Button::ShoulderLeft) {
        if (player_->seekByMs(-30000)) {
          state_.message = "Rewind 30 seconds";
        }
      } else if (isVod && button == Button::ShoulderRight) {
        if (player_->seekByMs(30000)) {
          state_.message = "Forward 30 seconds";
        }
      } else if (isVod && button == Button::Left) {
        if (player_->seekByMs(-10000)) {
          state_.message = "Rewind 10 seconds";
        }
      } else if (isVod && button == Button::Right) {
        if (player_->seekByMs(10000)) {
          state_.message = "Forward 10 seconds";
        }
      }
      break;
    }
    case ScreenId::Settings:
      if (button == Button::Back || button == Button::Select) state_.screen = ScreenId::Dashboard;
      break;
    case ScreenId::Playlists:
      state_.screen = ScreenId::AddPlaylist;
      break;
  }
  normalizeIndexes();
}

void App::handleDashboard(Button button) {
  if (button == Button::Menu) {
    state_.screen = ScreenId::AddPlaylist;
    return;
  }

  if (button == Button::Back && !usingNodeTree()) {
    if (state_.focus == FocusColumn::Playlist) {
      state_.focus = FocusColumn::Types;
    } else if (state_.focus == FocusColumn::Channels) {
      state_.focus = FocusColumn::Categories;
    } else if (state_.focus == FocusColumn::Categories) {
      state_.focus = FocusColumn::Types;
    }
    return;
  }


  if (button == Button::Favorite) {
    state_.channelGridView = !state_.channelGridView;
    state_.message = state_.channelGridView
      ? "Grid view enabled (X)"
      : "List view enabled (X)";
    std::printf("[KBORE][INPUT] X toggled dashboard item view: %s\n", state_.channelGridView ? "grid" : "list");
    normalizeIndexes();
    loadVisibleEpgForChannelList();
    loadSelectedEpg(false, false);
    return;
  }

  if (button == Button::ShoulderLeft || button == Button::ShoulderRight) {
    const int delta = button == Button::ShoulderRight ? 10 : -10;
    int count = 0;
    int *selected = nullptr;

    if (state_.focus == FocusColumn::Types) {
      count = static_cast<int>(visibleTypes().size());
      selected = &state_.selectedType;
    } else if (state_.focus == FocusColumn::Categories) {
      count = usingNodeTree()
        ? static_cast<int>(currentNodeChildren().size())
        : (selectedTypeGroup() ? static_cast<int>(selectedTypeGroup()->categories.size()) : 0);
      selected = &state_.selectedCategory;
    } else if (state_.focus == FocusColumn::Channels) {
      count = usingNodeTree()
        ? static_cast<int>(previewNodeChildren().size())
        : static_cast<int>(state_.loadedChannels.size());
      selected = &state_.selectedChannel;
    }

    if (selected && count > 0) {
      *selected = std::clamp(*selected + delta, 0, count - 1);

      if (state_.focus == FocusColumn::Types) {
        state_.nodePath.clear();
        state_.selectedCategory = 0;
        state_.selectedChannel = 0;
        if (!usingNodeTree()) resetLoadedChannels();
      } else if (state_.focus == FocusColumn::Categories) {
        state_.selectedChannel = 0;
        if (!usingNodeTree()) resetLoadedChannels();
      } else if (state_.focus == FocusColumn::Channels && !usingNodeTree()) {
        maybePreloadNextPage();
      }

      std::printf(
        "[KBORE][INPUT] %s skip 10 focus=%d selected=%d count=%d\n",
        button == Button::ShoulderRight ? "R" : "L",
        static_cast<int>(state_.focus),
        *selected,
        count
      );
      state_.message = std::string(button == Button::ShoulderRight ? "Skipped +10" : "Skipped -10");
    }

    normalizeIndexes();
    loadVisibleEpgForChannelList();
    loadSelectedEpg(false, false);
    return;
  }

  if (usingNodeTree()) {
    if (button == Button::Back) {
      if (state_.focus == FocusColumn::Channels) {
        state_.focus = FocusColumn::Categories;
      } else if (state_.focus == FocusColumn::Categories) {
        if (!state_.nodePath.empty()) {
          state_.nodePath.pop_back();
          state_.selectedCategory = 0;
          state_.selectedChannel = 0;
        } else {
          state_.focus = FocusColumn::Types;
        }
      } else if (state_.focus == FocusColumn::Types) {
        state_.focus = FocusColumn::Playlist;
      }
      return;
    }

    if (button == Button::Left) {
      if (state_.focus == FocusColumn::Playlist) {
        const int count = static_cast<int>(state_.config.playlists.size());
        if (count > 1) {
          int activeIndex = 0;

          for (int i = 0; i < count; ++i) {
            if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
              activeIndex = i;
              break;
            }
          }

          activeIndex = (activeIndex + count - 1) % count;
          activatePlaylist(activeIndex);
        }
        return;
      }

      if (state_.focus == FocusColumn::Channels) {
        if (state_.channelGridView && state_.selectedChannel % 2 == 1) {
          state_.selectedChannel--;
          loadVisibleEpgForChannelList();
          loadSelectedEpg(false, false);
        } else {
          state_.focus = FocusColumn::Categories;
        }
      } else if (state_.focus == FocusColumn::Categories) {
        state_.focus = FocusColumn::Types;
      }
      return;
    }

    if (button == Button::Right) {
      if (state_.focus == FocusColumn::Playlist) {
        const int count = static_cast<int>(state_.config.playlists.size());
        if (count > 1) {
          int activeIndex = 0;

          for (int i = 0; i < count; ++i) {
            if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
              activeIndex = i;
              break;
            }
          }

          activeIndex = (activeIndex + 1) % count;
          activatePlaylist(activeIndex);
        }
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        state_.focus = FocusColumn::Categories;
      } else if (state_.focus == FocusColumn::Categories) {
        state_.focus = FocusColumn::Channels;
      } else if (state_.focus == FocusColumn::Channels && state_.channelGridView) {
        const int count = static_cast<int>(previewNodeChildren().size());
        if (state_.selectedChannel % 2 == 0 && state_.selectedChannel + 1 < count) {
          state_.selectedChannel++;
          loadVisibleEpgForChannelList();
          loadSelectedEpg(false, false);
        }
      }
      return;
    }

    if (button == Button::Up) {
      if (state_.focus == FocusColumn::Playlist) {
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        if (state_.selectedType <= 0) {
          state_.focus = FocusColumn::Playlist;
        } else {
          state_.selectedType--;
          state_.nodePath.clear();
          state_.selectedCategory = 0;
          state_.selectedChannel = 0;
          loadVisibleEpgForChannelList();
          loadSelectedEpg(false, false);
        }
        return;
      }

      if (state_.focus == FocusColumn::Categories) {
        if (state_.selectedCategory <= 0) {
          state_.focus = FocusColumn::Playlist;
        } else {
          state_.selectedCategory--;
          state_.selectedChannel = 0;
          loadVisibleEpgForChannelList();
          loadSelectedEpg(false, false);
        }
        return;
      }

      if (state_.focus == FocusColumn::Channels) {
        const int step = state_.channelGridView ? 2 : 1;
        if (state_.selectedChannel <= 0) {
          state_.focus = FocusColumn::Categories;
        } else {
          state_.selectedChannel = std::max(0, state_.selectedChannel - step);
          loadVisibleEpgForChannelList();
          loadSelectedEpg(false, false);
        }
        return;
      }
    }

    if (button == Button::Down) {
      if (state_.focus == FocusColumn::Playlist) {
        state_.focus = FocusColumn::Types;
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        state_.selectedType++;
        state_.nodePath.clear();
        state_.selectedCategory = 0;
        state_.selectedChannel = 0;
      } else if (state_.focus == FocusColumn::Categories) {
        state_.selectedCategory++;
        state_.selectedChannel = 0;
      } else if (state_.focus == FocusColumn::Channels) {
        state_.selectedChannel += state_.channelGridView ? 2 : 1;
      }

      normalizeIndexes();
      loadVisibleEpgForChannelList();
      loadSelectedEpg(false, false);
      return;
    }

    if (button == Button::Favorite) {
      const MediaNode *node = state_.focus == FocusColumn::Channels
        ? selectedPreviewNode()
        : selectedCurrentNode();

      if (!node || node->url.empty()) {
        return;
      }

      if (state_.favorites.count(node->id)) {
        state_.favorites.erase(node->id);
        state_.message = "Removed favorite: " + (node->title.empty() ? node->name : node->title);
      } else {
        state_.favorites.insert(node->id);
        state_.message = "Favorite: " + (node->title.empty() ? node->name : node->title);
      }
      return;
    }

    if (button == Button::Select) {
      if (state_.focus == FocusColumn::Playlist) {
        state_.screen = ScreenId::AddPlaylist;
        return;
      }

      if (state_.focus == FocusColumn::Types) {
        state_.nodePath.clear();
        state_.selectedCategory = 0;
        state_.selectedChannel = 0;
        state_.focus = FocusColumn::Categories;
        loadVisibleEpgForChannelList();
        loadSelectedEpg(false, false);
        return;
      }

      if (state_.focus == FocusColumn::Categories) {
        MediaNode *node = selectedCurrentNode();

        if (!node) {
          return;
        }

        if (nodeCanHaveChildren(*node)) {
          if (!ensureNodeChildrenLoaded(*node)) {
            return;
          }

          if (nodeChildrenAreItems(*node)) {
            state_.focus = FocusColumn::Channels;
            state_.selectedChannel = 0;
            normalizeIndexes();
            loadVisibleEpgForChannelList();
            loadSelectedEpg(false, false);
            return;
          }

          enterNode(*node, state_.selectedCategory);
          loadVisibleEpgForChannelList();
          loadSelectedEpg(false, false);
          return;
        }

        if (node->playable || !node->url.empty()) {
          playNode(*node);
          return;
        }

        state_.message = "Empty folder";
        return;
      }

      if (state_.focus == FocusColumn::Channels) {
        MediaNode *preview = selectedPreviewNode();

        if (!preview) {
          // When the item list is focused but still empty, pressing A loads it
          // from the parent/category currently in focus instead of doing
          // nothing. This preserves the dynamic tree behavior for empty lists.
          MediaNode *node = selectedCurrentNode();
          if (node && nodeCanHaveChildren(*node)) {
            if (!ensureNodeChildrenLoaded(*node)) {
              return;
            }
            state_.selectedChannel = 0;
            state_.focus = FocusColumn::Channels;
            normalizeIndexes();
            loadVisibleEpgForChannelList();
            loadSelectedEpg(false, false);
            return;
          }
          return;
        }

        if (nodeCanHaveChildren(*preview)) {
          if (!ensureNodeChildrenLoaded(*preview)) {
            return;
          }

          const bool previewContainsItems = nodeChildrenAreItems(*preview) || preview->children.empty();

          // Preview nodes are children of the currently selected category.
          state_.nodePath.push_back(state_.selectedCategory);
          state_.nodePath.push_back(state_.selectedChannel);
          state_.selectedCategory = 0;
          state_.selectedChannel = 0;
          state_.focus = previewContainsItems ? FocusColumn::Channels : FocusColumn::Categories;
          normalizeIndexes();
          loadVisibleEpgForChannelList();
          loadSelectedEpg(false, false);
          return;
        }

        if (preview->playable || !preview->url.empty()) {
          playNode(*preview);
          return;
        }

        state_.message = "Empty folder";
        return;
      }
    }
  }

  if (button == Button::Left) {
    if (state_.focus == FocusColumn::Playlist) {
      const int count = static_cast<int>(state_.config.playlists.size());
      if (count > 1) {
        int activeIndex = 0;

        for (int i = 0; i < count; ++i) {
          if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
            activeIndex = i;
            break;
          }
        }

        activeIndex = (activeIndex + count - 1) % count;
        activatePlaylist(activeIndex);
      }
      return;
    }

    if (state_.focus == FocusColumn::Channels) {
      if (state_.channelGridView && state_.selectedChannel % 2 == 1) {
        state_.selectedChannel--;
        loadVisibleEpgForChannelList();
        loadSelectedEpg(false, false);
      } else {
        state_.focus = FocusColumn::Categories;
      }
    } else if (state_.focus == FocusColumn::Categories) {
      state_.focus = FocusColumn::Types;
    }
    return;
  }

  if (button == Button::Right) {
    if (state_.focus == FocusColumn::Playlist) {
      const int count = static_cast<int>(state_.config.playlists.size());
      if (count > 1) {
        int activeIndex = 0;

        for (int i = 0; i < count; ++i) {
          if (state_.config.playlists[static_cast<std::size_t>(i)].id == state_.config.activePlaylistId) {
            activeIndex = i;
            break;
          }
        }

        activeIndex = (activeIndex + 1) % count;
        activatePlaylist(activeIndex);
      }
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      state_.focus = FocusColumn::Categories;
    } else if (state_.focus == FocusColumn::Categories) {
      state_.focus = FocusColumn::Channels;
    } else if (state_.focus == FocusColumn::Channels && state_.channelGridView) {
      const int count = static_cast<int>(state_.loadedChannels.size());
      if (state_.selectedChannel % 2 == 0 && state_.selectedChannel + 1 < count) {
        state_.selectedChannel++;
        maybePreloadNextPage();
        loadVisibleEpgForChannelList();
        loadSelectedEpg(false, false);
      }
    }
    return;
  }

  if (button == Button::Up) {
    if (state_.focus == FocusColumn::Playlist) {
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      if (state_.selectedType <= 0) {
        state_.focus = FocusColumn::Playlist;
      } else {
        state_.selectedType--;
        resetLoadedChannels();
      }
      return;
    }

    if (state_.focus == FocusColumn::Categories) {
      if (state_.selectedCategory <= 0) {
        state_.focus = FocusColumn::Playlist;
      } else {
        state_.selectedCategory--;
        resetLoadedChannels();
      }
      return;
    }

    if (state_.focus == FocusColumn::Channels) {
      const int step = state_.channelGridView ? 2 : 1;
      if (state_.selectedChannel <= 0) {
        state_.focus = FocusColumn::Playlist;
      } else {
        state_.selectedChannel = std::max(0, state_.selectedChannel - step);
        loadVisibleEpgForChannelList();
        loadSelectedEpg(false, false);
      }
      return;
    }
  }

  if (button == Button::Down) {
    if (state_.focus == FocusColumn::Playlist) {
      state_.focus = FocusColumn::Types;
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      state_.selectedType++;
      resetLoadedChannels();
    } else if (state_.focus == FocusColumn::Categories) {
      state_.selectedCategory++;
      resetLoadedChannels();
    } else {
      state_.selectedChannel += state_.channelGridView ? 2 : 1;
      normalizeIndexes();
      maybePreloadNextPage();
      loadVisibleEpgForChannelList();
      loadSelectedEpg(false, false);
    }
    return;
  }

  if (button == Button::Favorite) {
    const Channel *channel = selectedChannelPtr();
    if (!channel) return;
    if (state_.favorites.count(channel->id)) {
      state_.favorites.erase(channel->id);
      state_.message = "Removed favorite: " + channel->name;
    } else {
      state_.favorites.insert(channel->id);
      state_.message = "Favorite: " + channel->name;
    }
    return;
  }

  if (button == Button::Select) {
    if (state_.focus == FocusColumn::Playlist) {
      state_.screen = ScreenId::AddPlaylist;
      return;
    }

    if (state_.focus == FocusColumn::Types) {
      state_.focus = FocusColumn::Categories;
      return;
    }

    if (state_.focus == FocusColumn::Categories) {
      loadCategory(false);
      state_.focus = FocusColumn::Channels;
      loadVisibleEpgForChannelList();
      loadSelectedEpg(false, false);
      return;
    }

    if (state_.loadedChannels.empty()) {
      loadCategory(false);
      return;
    }

    playSelectedChannel();
  }
}

void App::handleAddPlaylist(Button button) {
  const int playlistCount = static_cast<int>(state_.config.playlists.size());
  const int addM3uIndex = playlistCount;
  const int addXtreamIndex = playlistCount + 1;
  const int settingsIndex = playlistCount + 2;
  const int backIndex = playlistCount + 3;
  const int maxIndex = backIndex;

  if (button == Button::Back) {
    state_.screen = ScreenId::Dashboard;
    return;
  }

  if (button == Button::Up) {
    state_.selectedAddOption--;
  } else if (button == Button::Down) {
    state_.selectedAddOption++;
  }

  if (state_.selectedAddOption < 0) {
    state_.selectedAddOption = 0;
  }

  if (state_.selectedAddOption > maxIndex) {
    state_.selectedAddOption = maxIndex;
  }

  if (button == Button::Favorite) {
    if (state_.selectedAddOption < playlistCount) {
      deletePlaylist(state_.selectedAddOption);
    }
    return;
  }

  if (button == Button::Select) {
    if (state_.selectedAddOption < playlistCount) {
      activatePlaylist(state_.selectedAddOption);
      return;
    }


    if (state_.selectedAddOption == addM3uIndex) {
      addM3uPlaylist();
      return;
    }

    if (state_.selectedAddOption == addXtreamIndex) {
      addXtreamPlaylist();
      return;
    }

    if (state_.selectedAddOption == settingsIndex) {
      state_.screen = ScreenId::Settings;
      return;
    }

    if (state_.selectedAddOption == backIndex) {
      state_.screen = ScreenId::Dashboard;
      return;
    }
  }
}


const PlaylistConfig *App::activePlaylist() const {
  return state_.config.activePlaylist();
}

std::string App::activePlaylistName() const {
  const PlaylistConfig *playlist = activePlaylist();

  if (!playlist) {
    return "ADD LIST";
  }

  if (!playlist->name.empty()) {
    return playlist->name;
  }

  return playlist->provider == Provider::Xtream ? "Xtream" : "M3U";
}

bool App::loadCachedPlaylist(const PlaylistConfig &playlist) {
  Manifest cached;

  if (!loadManifestForPlaylist(cached, playlist.id) || (cached.nodes.empty() && cached.types.empty())) {
    return false;
  }

  cached.id = playlist.id;
  cached.name = playlist.name.empty() ? cached.name : playlist.name;
  cached.provider = playlist.provider;
  if (cached.source.empty()) {
    cached.source = playlist.sourceUrl();
  }

  state_.manifest = cached;
  state_.hasManifest = true;
  state_.screen = ScreenId::Dashboard;
  state_.focus = FocusColumn::Types;
  state_.selectedType = 0;
  state_.selectedCategory = 0;
  state_.selectedChannel = 0;
  resetLoadedChannels();
  normalizeIndexes();

  state_.message =
    "Loaded cached manifest: " +
    std::to_string(state_.manifest.totalChannels > 0 ? state_.manifest.totalChannels : state_.manifest.totalItems) +
    " items";

  return true;
}

void App::activatePlaylist(int index) {
  if (index < 0 || index >= static_cast<int>(state_.config.playlists.size())) {
    return;
  }

  PlaylistConfig playlist = state_.config.playlists[static_cast<std::size_t>(index)];
  state_.config.activePlaylistId = playlist.id;
  saveConfig(state_.config);

  /*
    Manifest loading can be heavy for dynamic node manifests (tens of MB).
    Always load it through the background worker, even when it is already
    cached locally, so the UI keeps rendering the Loading spinner.
  */
  startPlaylistLoad(playlist, false);
}

void App::importPlaylist(const PlaylistConfig &playlist) {
  startPlaylistLoad(playlist, false);
}

bool App::playlistLoadActive() const {
  std::lock_guard<std::mutex> lock(playlistLoadMutex_);
  return playlistLoadActive_;
}

void App::startPlaylistLoad(const PlaylistConfig &playlist, bool forceRefresh) {
  updatePlaylistLoad();

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);

    if (playlistLoadActive_) {
      state_.message = "Playlist is already loading. Please wait.";
      return;
    }
  }

  if (playlistLoadThread_.joinable()) {
    playlistLoadThread_.join();
  }

  state_.loading = true;
  state_.loadingMessage = "Loading playlist...";
  state_.message = "Loading " + playlist.name + "...";

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);
    playlistLoadActive_ = true;
    playlistLoadDone_ = false;
    playlistLoadSuccess_ = false;
    playlistLoadForceRefresh_ = forceRefresh;
    playlistLoadPlaylist_ = playlist;
    playlistLoadManifest_ = Manifest{};
    playlistLoadMessage_ = "Loading playlist...";
    playlistLoadError_.clear();
  }

  Config config = state_.config;

  playlistLoadThread_ = std::thread([this, playlist, config, forceRefresh]() {
    auto setStatus = [this](const std::string &message) {
      std::lock_guard<std::mutex> lock(playlistLoadMutex_);
      playlistLoadMessage_ = message;
    };

    try {
      Manifest manifest;

      if (!forceRefresh) {
        setStatus("Loading cached manifest...");

        if (loadManifestForPlaylist(manifest, playlist.id) &&
            manifest.id == playlist.id &&
            (!manifest.nodes.empty() || !manifest.types.empty())) {
          manifest.id = playlist.id;
          manifest.name = playlist.name.empty() ? manifest.name : playlist.name;
          manifest.source = playlist.sourceUrl().empty() ? manifest.source : playlist.sourceUrl();
          manifest.provider = playlist.provider;

          std::lock_guard<std::mutex> lock(playlistLoadMutex_);
          playlistLoadManifest_ = std::move(manifest);
          playlistLoadSuccess_ = true;
          playlistLoadDone_ = true;
          playlistLoadMessage_ = "Preparing interface...";
          return;
        }
      }

      const std::string sourceUrl = playlist.sourceUrl();

      if (config.parserApiBaseUrl.empty()) {
        throw std::runtime_error("Internal parser API base URL is empty");
      }

      if (sourceUrl.empty()) {
        throw std::runtime_error("Playlist URL/source is empty");
      }

      setStatus("Downloading manifest...");

      ParserApiClient api(config, setStatus);
      ManifestLoadResult loadResult;

      if (playlist.provider == Provider::Xtream) {
        loadResult = api.loadXtreamManifestWithCacheText(sourceUrl);
      } else {
        loadResult = api.loadM3uManifestWithCacheText(sourceUrl);
      }

      setStatus("Preparing manifest...");

      Manifest imported = std::move(loadResult.manifest);
      imported.id = playlist.id;
      imported.name = playlist.name.empty() ? imported.name : playlist.name;
      imported.source = sourceUrl;
      imported.provider = playlist.provider;

      if (imported.nodes.empty() && imported.types.empty()) {
        throw std::runtime_error("Parser returned an empty manifest");
      }

      setStatus("Saving manifest...");
      saveManifestForPlaylist(imported, playlist.id);

      setStatus("Preparing interface...");

      std::lock_guard<std::mutex> lock(playlistLoadMutex_);
      playlistLoadManifest_ = std::move(imported);
      playlistLoadSuccess_ = true;
      playlistLoadDone_ = true;
      playlistLoadMessage_ = "Preparing interface...";
    } catch (const std::exception &ex) {
      std::lock_guard<std::mutex> lock(playlistLoadMutex_);
      playlistLoadSuccess_ = false;
      playlistLoadDone_ = true;
      playlistLoadError_ = ex.what();
      playlistLoadMessage_ = "Failed";
    }
  });
}

void App::updatePlaylistLoad() {
  PlaylistConfig playlist;
  Manifest manifest;
  std::string message;
  std::string error;
  bool active = false;
  bool done = false;
  bool success = false;

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);
    active = playlistLoadActive_;

    if (!active) {
      return;
    }

    done = playlistLoadDone_;
    success = playlistLoadSuccess_;
    playlist = playlistLoadPlaylist_;
    message = playlistLoadMessage_;
    error = playlistLoadError_;

    if (done && success) {
      /*
        The dynamic nodes manifest can be very large. Do not copy it from
        the worker result into the UI thread local variable; copying the
        whole tree here freezes the UI right after cache saving completes.
        Move it once, then reset the worker state below.
      */
      manifest = std::move(playlistLoadManifest_);
    }
  }

  if (!done) {
    state_.loading = true;
    state_.loadingMessage = message.empty() ? "Loading playlist..." : message;
    state_.message = state_.loadingMessage;
    return;
  }

  if (playlistLoadThread_.joinable()) {
    playlistLoadThread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(playlistLoadMutex_);
    playlistLoadActive_ = false;
    playlistLoadDone_ = false;
    playlistLoadSuccess_ = false;
    playlistLoadManifest_ = Manifest{};
    playlistLoadMessage_.clear();
    playlistLoadError_.clear();
  }

  state_.loading = false;
  state_.loadingMessage.clear();


  if (!success) {
    state_.hasManifest = false;
    resetLoadedChannels();
    state_.screen = ScreenId::AddPlaylist;
    state_.message = "Failed to load stream data. Check the playlist info and add it again.";

    if (!error.empty()) {
      std::printf("[KBORE] playlist load failed: %s\n", error.c_str());
    }

    return;
  }

  state_.manifest = std::move(manifest);
  state_.hasManifest = true;
  state_.screen = ScreenId::Dashboard;
  state_.focus = FocusColumn::Types;
  state_.selectedType = 0;
  state_.selectedCategory = 0;
  state_.selectedChannel = 0;
  state_.nodePath.clear();

  resetLoadedChannels();
  normalizeIndexes();

  // Keep the active manifest path compatible without forcing re-downloads.
  // The per-playlist cache is the source of truth for large dynamic manifests.
  // Avoid doing another large write here.
  state_.message =
    "Loaded " +
    playlist.name +
    ": " +
    std::to_string(state_.manifest.totalChannels > 0 ? state_.manifest.totalChannels : state_.manifest.totalItems) +
    " items";
}

void App::updateCacheSave() {
  bool active = false;
  bool done = false;
  bool success = false;
  std::string playlistId;
  std::string error;
  std::size_t written = 0;
  std::size_t total = 0;

  {
    std::lock_guard<std::mutex> lock(cacheSaveMutex_);
    active = cacheSaveActive_;

    if (!active) {
      return;
    }

    done = cacheSaveDone_;
    success = cacheSaveSuccess_;
    playlistId = cacheSavePlaylistId_;
    error = cacheSaveError_;
    written = cacheSaveWritten_;
    total = cacheSaveTotal_;
  }

  if (!done) {
    /*
      Keep this non-invasive. The cache save happens after the playlist is
      already usable, so do not show a blocking overlay. Leave breadcrumbs in
      the status message only when the app is otherwise idle.
    */
    if (!state_.loading && state_.screen == ScreenId::Dashboard && total > 0) {
      std::ostringstream message;
      message << "Saving playlist cache... " << (written / 1024) << " / " << (total / 1024) << " KB";
      state_.message = message.str();
    }

    return;
  }

  if (cacheSaveThread_.joinable()) {
    cacheSaveThread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(cacheSaveMutex_);
    cacheSaveActive_ = false;
    cacheSaveDone_ = false;
    cacheSaveSuccess_ = false;
    cacheSavePlaylistId_.clear();
    cacheSaveError_.clear();
    cacheSaveWritten_ = 0;
    cacheSaveTotal_ = 0;
  }

  if (!state_.loading && state_.screen == ScreenId::Dashboard) {
    state_.message = success
      ? "Playlist cache saved"
      : "Playlist cache save failed";
  }

  if (!success && !error.empty()) {
    std::printf("[KBORE][CACHE] cache save failed for %s: %s\n", playlistId.c_str(), error.c_str());
  }
}

bool App::cacheSaveActive() const {
  std::lock_guard<std::mutex> lock(cacheSaveMutex_);
  return cacheSaveActive_;
}




void App::addM3uPlaylist() {
  std::string name = trimText(requestTextInput("Playlist name", "M3U", 80));

  if (name.empty()) {
    state_.message = "Playlist name is required";
    return;
  }

  std::string url = trimText(requestTextInput("M3U URL", "", 900));

  if (url.empty()) {
    state_.message = "M3U URL is required";
    return;
  }

  std::string epgUrl = trimText(requestTextInput("Optional EPG URL", "", 900));

  PlaylistConfig playlist;
  playlist.id = safePlaylistId(name, Provider::M3u);
  playlist.name = name;
  playlist.provider = Provider::M3u;
  playlist.m3uUrl = url;
  playlist.epgUrl = epgUrl;

  state_.config.playlists.push_back(playlist);
  state_.config.activePlaylistId = playlist.id;
  state_.config.defaultPlaylistUrl = playlist.m3uUrl;

  if (!saveConfig(state_.config)) {
    state_.message = "Could not save config.json";
    return;
  }

  importPlaylist(playlist);
}

void App::addXtreamPlaylist() {
  std::string name = trimText(requestTextInput("Playlist name", "Xtream", 80));

  if (name.empty()) {
    state_.message = "Playlist name is required";
    return;
  }

  std::string server = trimTrailingSlashLocal(trimText(requestTextInput("Xtream server URL", "http://", 500)));

  if (server.empty() || server == "http://" || server == "https://") {
    state_.message = "Xtream server URL is required";
    return;
  }

  std::string username = trimText(requestTextInput("Xtream username", "", 180));

  if (username.empty()) {
    state_.message = "Xtream username is required";
    return;
  }

  std::string password = trimText(requestTextInput("Xtream password", "", 180));

  if (password.empty()) {
    state_.message = "Xtream password is required";
    return;
  }

  PlaylistConfig playlist;
  playlist.id = safePlaylistId(name, Provider::Xtream);
  playlist.name = name;
  playlist.provider = Provider::Xtream;
  playlist.serverUrl = server;
  playlist.username = username;
  playlist.password = password;

  state_.config.playlists.push_back(playlist);
  state_.config.activePlaylistId = playlist.id;
  state_.config.defaultXtreamUrl = playlist.sourceUrl();

  if (!saveConfig(state_.config)) {
    state_.message = "Could not save config.json";
    return;
  }

  importPlaylist(playlist);
}


void App::deletePlaylist(int index) {
  if (index < 0 || index >= static_cast<int>(state_.config.playlists.size())) {
    return;
  }

  const PlaylistConfig removed = state_.config.playlists[static_cast<std::size_t>(index)];
  state_.config.playlists.erase(state_.config.playlists.begin() + index);

  if (state_.config.activePlaylistId == removed.id) {
    if (!state_.config.playlists.empty()) {
      const int nextIndex = std::min(index, static_cast<int>(state_.config.playlists.size()) - 1);
      state_.config.activePlaylistId = state_.config.playlists[static_cast<std::size_t>(nextIndex)].id;
    } else {
      state_.config.activePlaylistId.clear();
    }
  }

  if (state_.selectedAddOption >= static_cast<int>(state_.config.playlists.size())) {
    state_.selectedAddOption = std::max(0, static_cast<int>(state_.config.playlists.size()) - 1);
  }

  saveConfig(state_.config);

  if (state_.config.playlists.empty()) {
    state_.hasManifest = false;
    state_.manifest = Manifest{};
    resetLoadedChannels();
    state_.message = "Playlist deleted. Add a new list to continue.";
    return;
  }

  state_.message = "Playlist deleted: " + removed.name;

  const PlaylistConfig *playlist = activePlaylist();

  if (playlist) {
    startPlaylistLoad(*playlist);
  }
}

void App::importM3u() {
  addM3uPlaylist();
}

void App::importXtream() {
  addXtreamPlaylist();
}

void App::loadCategory(bool append) {
  try {
    const Category *category = selectedCategoryPtr();

    if (!category) {
      state_.message = "No category selected";
      return;
    }

    if (!state_.hasManifest) {
      state_.message = "No playlist loaded";
      return;
    }

    int page = append ? state_.loadedPage + 1 : 1;
    std::string key =
      toString(state_.manifest.provider) + ":" +
      toString(category->type) + ":" +
      category->id;

    if (!append && key == state_.loadedCategoryKey && !state_.loadedChannels.empty()) {
      return;
    }

    ChannelPage result;
    bool fromCache = loadChannelPage(
      state_.manifest.id,
      state_.manifest.provider,
      category->type,
      category->id,
      page,
      result
    );

    if (!fromCache) {
      const std::string queuedKey = state_.manifest.id + ":" + key + ":" + std::to_string(page);
      bool queued = false;

      {
        std::lock_guard<std::mutex> lock(channelMutex_);
        if (!append) {
          for (const ChannelLoadJob &pending : channelQueue_) {
            channelQueuedKeys_.erase(pending.manifestId + ":" + pending.categoryKey + ":" + std::to_string(pending.page));
          }
          channelQueue_.clear();
        }

        if (!channelQueuedKeys_.count(queuedKey)) {
          channelQueuedKeys_.insert(queuedKey);
          ChannelLoadJob job{
            state_.config,
            state_.manifest.id,
            state_.manifest.source,
            state_.manifest.provider,
            category->type,
            category->id,
            key,
            page,
            append
          };

          if (append) {
            channelQueue_.push_back(job);
          } else {
            channelQueue_.insert(channelQueue_.begin(), job);
          }
          queued = true;
        }
      }

      if (!append) {
        resetLoadedChannels();
        state_.loadedCategoryKey = key;
      }
      state_.channelListLoading = true;
      state_.channelListLoadingKey = key;

      state_.message = append
        ? "Loading more channels in background..."
        : "Loading channels in background...";

      if (queued) {
        channelCv_.notify_one();
      }
      return;
    } else {
      state_.message = append
        ? "Loading more channels from cache..."
        : "Loading channels from cache...";
    }

    if (append) {
      state_.loadedChannels.insert(
        state_.loadedChannels.end(),
        result.channels.begin(),
        result.channels.end()
      );
    } else {
      state_.loadedChannels = result.channels;
      state_.selectedChannel = 0;
    }

    state_.loadedPage = result.page;
    state_.loadedTotal = result.totalChannels;
    state_.loadedTotalPages = result.totalPages;
    state_.loadedCategoryKey = key;
    state_.channelListLoading = false;
    state_.channelListLoadingKey.clear();
    state_.loading = false;
    state_.loadingMessage.clear();

    loadVisibleEpgForChannelList();
    loadSelectedEpg(false, false);

    const std::string origin = fromCache ? "cache" : "API";

    state_.message =
      std::to_string(state_.loadedChannels.size()) + "/" +
      std::to_string(result.totalChannels) +
      " channels loaded from " + origin +
      ", page " + std::to_string(result.page) +
      "/" + std::to_string(result.totalPages);
    } catch (const std::exception &ex) {
      std::printf("[KBORE] loadCategory failed: %s\n", ex.what());

      state_.loading = false;
      state_.loadingMessage.clear();

      /*
        Não apague o manifest aqui.

        Se o manifest já carregou, types/categories devem continuar visíveis.
        Uma falha ao carregar channels pode ser:
          - categoria vazia
          - endpoint de channels com formato diferente
          - erro temporário de rede
          - cache antigo/inválido
          - categoria não suportada pela API

        Mas isso não significa que a playlist inteira é inválida.
      */
      resetLoadedChannels();

      state_.screen = ScreenId::Dashboard;
      state_.focus = FocusColumn::Categories;
      state_.message = "Failed to load channels for this category.";
    }
}

void App::maybePreloadNextPage() {
  if (state_.focus != FocusColumn::Channels) {
    return;
  }

  if (state_.loadedChannels.empty()) {
    return;
  }

  if (state_.loadedPage >= state_.loadedTotalPages) {
    return;
  }

  const int preloadThreshold = std::max(1, state_.config.preloadThreshold);
  const int remaining =
    static_cast<int>(state_.loadedChannels.size()) - 1 - state_.selectedChannel;

  if (remaining <= preloadThreshold) {
    loadCategory(true);
  }
}

void App::startChannelWorker() {
  channelStop_ = false;
  channelWorker_ = std::thread(&App::channelWorkerLoop, this);
}

void App::stopChannelWorker() {
  channelStop_ = true;
  channelCv_.notify_all();

  if (channelWorker_.joinable()) {
    channelWorker_.join();
  }
}

void App::channelWorkerLoop() {
  while (!channelStop_) {
    ChannelLoadJob job;

    {
      std::unique_lock<std::mutex> lock(channelMutex_);
      channelCv_.wait(lock, [this]() {
        return channelStop_ || !channelQueue_.empty();
      });

      if (channelStop_) {
        break;
      }

      job = channelQueue_.front();
      channelQueue_.erase(channelQueue_.begin());
    }

    const std::string queuedKey = job.manifestId + ":" + job.categoryKey + ":" + std::to_string(job.page);
    ChannelLoadResult result;
    result.manifestId = job.manifestId;
    result.categoryKey = job.categoryKey;
    result.type = job.type;
    result.categoryId = job.categoryId;
    result.page = job.page;
    result.append = job.append;

    try {
      ParserApiClient channelApi(job.config);
      result.pageData = channelApi.loadChannels(
        job.source,
        job.provider,
        job.type,
        job.categoryId,
        job.page
      );
      saveChannelPage(job.manifestId, job.provider, job.type, job.categoryId, result.pageData);
      result.ok = true;
    } catch (const std::exception &ex) {
      std::printf("[KBORE] background channel load failed: %s\n", ex.what());
      result.error = ex.what();
    }

    {
      std::lock_guard<std::mutex> lock(channelMutex_);
      channelFinished_.push_back(result);
      channelQueuedKeys_.erase(queuedKey);
    }
  }
}

void App::drainFinishedChannelLoads() {
  std::vector<ChannelLoadResult> finished;

  {
    std::lock_guard<std::mutex> lock(channelMutex_);
    finished.swap(channelFinished_);
  }

  for (const ChannelLoadResult &result : finished) {
    if (!state_.hasManifest || result.manifestId != state_.manifest.id) {
      continue;
    }

    if (result.categoryKey != state_.loadedCategoryKey) {
      continue;
    }

    if (result.categoryKey == state_.channelListLoadingKey) {
      state_.channelListLoading = false;
      state_.channelListLoadingKey.clear();
    }

    if (!result.ok) {
      if (!result.append && state_.loadedChannels.empty()) {
        resetLoadedChannels();
        state_.focus = FocusColumn::Categories;
      }
      state_.message = result.append
        ? "Failed to load more channels."
        : "Failed to load channels for this category.";
      continue;
    }

    if (result.append) {
      if (result.page <= state_.loadedPage) {
        continue;
      }

      state_.loadedChannels.insert(
        state_.loadedChannels.end(),
        result.pageData.channels.begin(),
        result.pageData.channels.end()
      );
    } else {
      state_.loadedChannels = result.pageData.channels;
      state_.selectedChannel = 0;
    }

    state_.loadedPage = result.pageData.page;
    state_.loadedTotal = result.pageData.totalChannels;
    state_.loadedTotalPages = result.pageData.totalPages;
    state_.loadedCategoryKey = result.categoryKey;

    loadVisibleEpgForChannelList();
    loadSelectedEpg(false, false);

    state_.message =
      std::to_string(state_.loadedChannels.size()) + "/" +
      std::to_string(result.pageData.totalChannels) +
      " channels loaded from API, page " +
      std::to_string(result.pageData.page) +
      "/" + std::to_string(result.pageData.totalPages);
  }
}


std::string App::channelEpgKey(const Channel &channel) const {
  const std::vector<std::string> keys = channelEpgKeys(channel);
  return keys.empty() ? "" : keys.front();
}

std::vector<std::string> App::channelEpgKeys(const Channel &channel) const {
  std::vector<std::string> keys;
  auto addKey = [&keys](const std::string &key) {
    if (key.empty()) {
      return;
    }
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
      keys.push_back(key);
    }
  };

  addKey(channel.tvgId);
  addKey(channel.tvgName);
  addKey(channel.streamId);
  addKey(channel.id);
  addKey(channel.name);
  return keys;
}

const EpgPage *App::cachedEpgForChannel(const Channel &channel) const {
  for (const std::string &key : channelEpgKeys(channel)) {
    auto it = state_.epgByChannel.find(key);
    if (it != state_.epgByChannel.end()) {
      return &it->second;
    }
  }

  return nullptr;
}

void App::loadSelectedEpg(bool force, bool fetchRemote) {
  Channel nodeChannel;
  const Channel *channel = nullptr;

  if (usingNodeTree()) {
    const MediaNode *node = selectedPreviewNode();
    if (node && (node->playable || !node->url.empty())) {
      nodeChannel = channelFromNode(*node);
      channel = &nodeChannel;
    }
  } else {
    channel = selectedChannelPtr();
  }

  if (!channel || !state_.hasManifest || channel->type != StreamType::Live) {
    state_.currentEpgKey.clear();
    state_.currentEpgAvailable = false;
    return;
  }

  const std::string key = channelEpgKey(*channel);

  state_.currentEpgKey = key;

  const EpgPage *memoryPage = cachedEpgForChannel(*channel);
  if (!force && memoryPage) {
    state_.currentEpgAvailable = currentProgramIndex(*memoryPage) >= 0;
    if ((state_.currentEpgAvailable && epgPageHasDatedTimes(*memoryPage)) || !fetchRemote) {
      return;
    }
  }

  EpgPage cached;
  if (!force && loadEpgPage(state_.manifest.id, *channel, cached)) {
    for (const std::string &alias : channelEpgKeys(*channel)) {
      state_.epgByChannel[alias] = cached;
    }
    state_.currentEpgAvailable = currentProgramIndex(cached) >= 0;
    if ((state_.currentEpgAvailable && epgPageHasDatedTimes(cached)) || !fetchRemote) {
      return;
    }
  }

  if (!fetchRemote) {
    state_.currentEpgAvailable = false;
    return;
  }

  try {
    const PlaylistConfig *playlist = activePlaylist();
    const std::string manualEpgUrl = playlist && playlist->provider == Provider::M3u ? playlist->epgUrl : "";
    std::printf("[KBORE] loading selected EPG channel='%s' provider='%s' key='%s' remote=%s\n", channel->name.c_str(), toString(state_.manifest.provider).c_str(), key.c_str(), fetchRemote ? "yes" : "no");
    EpgPage epg = api_.loadEpgPrograms(state_.manifest.source, state_.manifest.provider, *channel, 1, 48, manualEpgUrl);
    for (const std::string &alias : channelEpgKeys(*channel)) {
      state_.epgByChannel[alias] = epg;
    }
    state_.currentEpgAvailable = currentProgramIndex(epg) >= 0;
    saveEpgPage(state_.manifest.id, *channel, epg);
  } catch (const std::exception &ex) {
    std::printf("[KBORE] loadSelectedEpg failed: %s\n", ex.what());
    for (const std::string &alias : channelEpgKeys(*channel)) {
      state_.epgByChannel[alias] = EpgPage{};
    }
    state_.currentEpgAvailable = false;
  }
}

void App::loadEpgForChannels(const std::vector<Channel> &channels) {
  if (!state_.hasManifest || channels.empty()) {
    return;
  }

  std::vector<Channel> missing;

  for (const Channel &channel : channels) {
    if (channel.type != StreamType::Live) {
      continue;
    }

    const std::string key = channelEpgKey(channel);
    const EpgPage *memoryPage = cachedEpgForChannel(channel);
    if (memoryPage && currentProgramIndex(*memoryPage) >= 0 && epgPageHasDatedTimes(*memoryPage)) {
      continue;
    }

    EpgPage cached;
    if (loadEpgPage(state_.manifest.id, channel, cached)) {
      for (const std::string &alias : channelEpgKeys(channel)) {
        state_.epgByChannel[alias] = cached;
      }
      if (currentProgramIndex(cached) >= 0 && epgPageHasDatedTimes(cached)) {
        continue;
      }
    }

    missing.push_back(channel);
  }

  if (missing.empty()) {
    return;
  }

  const PlaylistConfig *playlist = activePlaylist();
  const std::string manualEpgUrl = playlist && playlist->provider == Provider::M3u ? playlist->epgUrl : "";
  std::vector<EpgJob> jobs;

  for (const Channel &channel : missing) {
    if (jobs.size() >= 8) {
      break;
    }

    const std::string key = channelEpgKey(channel);
    const std::string queuedKey = state_.manifest.id + ":" + key;

    {
      std::lock_guard<std::mutex> lock(epgMutex_);
      if (epgQueuedKeys_.count(queuedKey)) {
        continue;
      }
      epgQueuedKeys_.insert(queuedKey);
    }

    jobs.push_back(EpgJob{
      state_.config,
      channel,
      key,
      state_.manifest.id,
      state_.manifest.source,
      state_.manifest.provider,
      manualEpgUrl,
      48
    });
  }

  if (jobs.empty()) {
    return;
  }

  std::printf("[KBORE] queueing %zu EPG request(s) for visible live channels\n", jobs.size());

  {
    std::lock_guard<std::mutex> lock(epgMutex_);
    epgQueue_.insert(epgQueue_.begin(), jobs.begin(), jobs.end());

    constexpr std::size_t maxPendingEpgJobs = 24;
    while (epgQueue_.size() > maxPendingEpgJobs) {
      const EpgJob &stale = epgQueue_.back();
      epgQueuedKeys_.erase(stale.manifestId + ":" + stale.key);
      epgQueue_.pop_back();
    }
  }

  epgCv_.notify_one();
}

void App::loadVisibleEpgForChannelList() {
  const int channelRows = state_.channelGridView ? 6 : 7;

  if (usingNodeTree()) {
    std::vector<const MediaNode *> nodes = previewNodeChildren();
    if (nodes.empty()) {
      return;
    }

    const int size = static_cast<int>(nodes.size());
    const int start = state_.channelGridView
      ? gridWindowStart(state_.selectedChannel, size, 2, 3)
      : windowStart(state_.selectedChannel, size, channelRows);

    std::vector<Channel> visible;
    for (int i = 0; i < channelRows; ++i) {
      const int index = start + i;
      if (index >= static_cast<int>(nodes.size())) {
        break;
      }

      const MediaNode *node = nodes[static_cast<std::size_t>(index)];
      if (!node || !(node->playable || !node->url.empty())) {
        continue;
      }

      Channel channel = channelFromNode(*node);
      if (channel.type == StreamType::Live) {
        visible.push_back(channel);
      }
    }

    loadEpgForChannels(visible);
    return;
  }

  if (state_.loadedChannels.empty()) {
    return;
  }

  const int size = static_cast<int>(state_.loadedChannels.size());
  const int start = state_.channelGridView
    ? gridWindowStart(state_.selectedChannel, size, 2, 3)
    : windowStart(state_.selectedChannel, size, channelRows);
  std::vector<Channel> visible;

  for (int i = 0; i < channelRows; ++i) {
    const int index = start + i;
    if (index >= static_cast<int>(state_.loadedChannels.size())) {
      break;
    }
    visible.push_back(state_.loadedChannels[static_cast<std::size_t>(index)]);
  }

  loadEpgForChannels(visible);
}

void App::startEpgWorker() {
  epgStop_ = false;
  epgWorker_ = std::thread(&App::epgWorkerLoop, this);
}

void App::stopEpgWorker() {
  epgStop_ = true;
  epgCv_.notify_all();

  if (epgWorker_.joinable()) {
    epgWorker_.join();
  }
}

void App::epgWorkerLoop() {
  while (!epgStop_) {
    EpgJob job;

    {
      std::unique_lock<std::mutex> lock(epgMutex_);
      epgCv_.wait(lock, [this]() {
        return epgStop_ || !epgQueue_.empty();
      });

      if (epgStop_) {
        break;
      }

      job = epgQueue_.front();
      epgQueue_.erase(epgQueue_.begin());
    }

    const std::string queuedKey = job.manifestId + ":" + job.key;

    try {
      std::printf("[KBORE] loading EPG for channel='%s' provider='%s' key='%s'\n", job.channel.name.c_str(), toString(job.provider).c_str(), job.key.c_str());
      ParserApiClient epgApi(job.config);
      EpgPage epg = epgApi.loadEpgPrograms(
        job.source,
        job.provider,
        job.channel,
        1,
        job.pageSize,
        job.manualEpgUrl
      );
      saveEpgPage(job.manifestId, job.channel, epg);
      std::printf("[KBORE] EPG loaded for channel='%s': %zu program(s)\n", job.channel.name.c_str(), epg.programs.size());

      std::lock_guard<std::mutex> lock(epgMutex_);
      epgFinished_.push_back(EpgResult{job.manifestId, job.key, job.channel, epg});
      epgQueuedKeys_.erase(queuedKey);
    } catch (const std::exception &ex) {
      std::printf("[KBORE] background EPG failed for %s: %s\n", job.channel.name.c_str(), ex.what());
      std::lock_guard<std::mutex> lock(epgMutex_);
      epgQueuedKeys_.erase(queuedKey);
    }
  }
}

void App::drainFinishedEpg() {
  std::vector<EpgResult> finished;

  {
    std::lock_guard<std::mutex> lock(epgMutex_);
    finished.swap(epgFinished_);
  }

  for (const EpgResult &result : finished) {
    if (!state_.hasManifest || result.manifestId != state_.manifest.id) {
      continue;
    }

    for (const std::string &alias : channelEpgKeys(result.channel)) {
      state_.epgByChannel[alias] = result.page;
    }

    if (result.key == state_.currentEpgKey) {
      state_.currentEpgAvailable = currentProgramIndex(result.page) >= 0;
    }
  }
}

std::string App::epgLineForChannel(const Channel &channel) const {
  if (channel.type != StreamType::Live) {
    return "EPG not applicable";
  }

  const std::string key = channelEpgKey(channel);
  const EpgPage *page = cachedEpgForChannel(channel);

  if (!page || page->programs.empty()) {
    return key == state_.currentEpgKey ? "EPG unavailable" : "EPG not loaded";
  }

  const int index = currentProgramIndex(*page);
  if (index < 0) {
    return "EPG unavailable";
  }

  const EpgProgram &program = page->programs[static_cast<std::size_t>(index)];
  const std::string timeRange = formatEpgRange(program);

  if (!timeRange.empty()) {
    return timeRange + "  " + program.title;
  }

  return program.title;
}

std::string App::epgNowNextLine(const Channel &channel) const {
  if (channel.type != StreamType::Live) {
    return "EPG is available only for live channels";
  }

  const EpgPage *cachedPage = cachedEpgForChannel(channel);

  if (!cachedPage || cachedPage->programs.empty()) {
    return "EPG unavailable for this channel";
  }

  const EpgPage &page = *cachedPage;
  const int nowIndex = currentProgramIndex(page);
  if (nowIndex < 0) {
    return "EPG unavailable for this channel";
  }

  const EpgProgram &now = page.programs[static_cast<std::size_t>(nowIndex)];

  std::string line = "NOW ";
  const std::string nowRange = formatEpgRange(now);
  if (!nowRange.empty()) {
    line += nowRange + " ";
  }
  line += now.title;

  const std::size_t nextIndex = static_cast<std::size_t>(nowIndex + 1);
  if (nextIndex < page.programs.size()) {
    const EpgProgram &next = page.programs[nextIndex];
    line += "\nNEXT ";
    const std::string nextRange = formatEpgRange(next);
    if (!nextRange.empty()) {
      line += nextRange + " ";
    }
    line += next.title;
  }

  return line;
}


void App::playSelectedChannel() {
  const Channel *channel = selectedChannelPtr();

  if (!channel) {
    return;
  }

  openChannel(*channel);
}

void App::openChannel(const Channel &channel) {
  /*
    Troca de stream no Switch precisa ser tratada como troca de sessão.
    Reutilizar o mesmo backend depois de Deko3D/NVTEGRA ativo pode deixar
    frames, audio device ou wrappers de memória da sessão anterior vivos por
    tempo suficiente para quebrar a próxima inicialização.
  */
  if (player_) {
    logLine("[KBORE][PLAYBACK][LIFECYCLE] closing previous player before opening another stream");
    player_->close();
    player_.reset();
    gfx_.resumeAfterNativeVideo();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }

  state_.hasPlaybackChannel = true;
  state_.playbackChannel = channel;

  loadSelectedEpg(false, true);
  loadEpgForChannels(std::vector<Channel>{channel});

  state_.screen = ScreenId::Player;
  state_.message = "Loading stream";
  state_.playerStarted = false;
  state_.playerFrameSeen = false;
  state_.playerLoading = true;
  state_.playerLoadFailed = false;
  state_.playerErrorMessage.clear();
  state_.playerOverlayUntilMs = nowMs() + 5000;

  render();

  player_ = createPlayerBackend();

  gfx_.suspendForNativeVideo();
  player_->setNativeVideoAllowed(true);
  player_->setOverlayVisible(true);

  if (player_->open(channel.url)) {
    setMediaPlaybackActive(true);
    logLine("[KBORE][PLAYBACK][LIFECYCLE] media playback state enabled");

    if (!player_->nativeVideoActive() && gfx_.isSuspendedForNativeVideo()) {
      gfx_.resumeAfterNativeVideo();
    }

    state_.message = "Playing: " + channel.name;
    state_.playerStarted = true;
    state_.playerLoading = false;
    state_.playerLoadFailed = false;
    state_.playerErrorMessage.clear();
  } else {
    const std::string openError = player_ ? player_->error() : "Player backend not available";

    logLinef(
      "[KBORE][PLAYBACK] failed to load %s stream name='%s' error='%s' url=%s",
      toString(channel.type).c_str(),
      channel.name.c_str(),
      openError.c_str(),
      channel.url.c_str()
    );
    state_.message = "Failed to load stream";
    state_.playerStarted = false;
    state_.playerLoading = false;
    state_.playerLoadFailed = true;
    state_.playerErrorMessage = openError;
    setMediaPlaybackActive(false);
    gfx_.resumeAfterNativeVideo();

    if (player_) {
      player_->close();
      player_.reset();
    }
  }
}

void App::resetLoadedChannels() {
  state_.loadedChannels.clear();
  state_.loadedCategoryKey.clear();
  state_.loadedPage = 1;
  state_.loadedTotal = 0;
  state_.loadedTotalPages = 1;
  state_.selectedChannel = 0;
  state_.currentEpgKey.clear();
  state_.currentEpgAvailable = false;
  state_.channelListLoading = false;
  state_.channelListLoadingKey.clear();
}

std::vector<TypeGroup> App::visibleTypes() const {
  if (usingNodeTree()) {
    std::vector<TypeGroup> groups;

    for (const auto &node : state_.manifest.nodes) {
      TypeGroup group;
      group.id = streamTypeFromString(node.type);
      group.label = node.title.empty() ? node.name : node.title;
      group.totalChannels = nodeCount(node);
      groups.push_back(group);
    }

    return groups;
  }

  if (state_.hasManifest) return state_.manifest.types;
  return {
    {StreamType::Live, "Live TV", 0, {}},
    {StreamType::Movies, "Movies", 0, {}},
    {StreamType::Series, "Series", 0, {}},
    {StreamType::Radio, "Radio", 0, {}}
  };
}

const TypeGroup *App::selectedTypeGroup() const {
  auto types = visibleTypes();
  if (types.empty()) return nullptr;
  int index = std::clamp(state_.selectedType, 0, static_cast<int>(types.size()) - 1);
  // Safe because render/use is immediate; avoid returning pointer to temp? Use manifest direct below.
  if (!state_.hasManifest) return nullptr;
  if (index >= 0 && index < static_cast<int>(state_.manifest.types.size())) return &state_.manifest.types[index];
  return nullptr;
}

const Category *App::selectedCategoryPtr() const {
  if (usingNodeTree()) {
    return nullptr;
  }

  const TypeGroup *type = selectedTypeGroup();
  if (!type || type->categories.empty()) return nullptr;
  int index = std::clamp(state_.selectedCategory, 0, static_cast<int>(type->categories.size()) - 1);
  return &type->categories[index];
}

const Channel *App::selectedChannelPtr() const {
  if (state_.loadedChannels.empty()) return nullptr;
  int index = std::clamp(state_.selectedChannel, 0, static_cast<int>(state_.loadedChannels.size()) - 1);
  return &state_.loadedChannels[index];
}


bool App::usingNodeTree() const {
  return state_.hasManifest && !state_.manifest.nodes.empty();
}

const MediaNode *App::selectedRootNode() const {
  if (!usingNodeTree()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedType,
    0,
    std::max(0, static_cast<int>(state_.manifest.nodes.size()) - 1)
  );

  if (index < 0 || index >= static_cast<int>(state_.manifest.nodes.size())) {
    return nullptr;
  }

  return &state_.manifest.nodes[static_cast<std::size_t>(index)];
}

MediaNode *App::selectedRootNode() {
  if (!usingNodeTree()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedType,
    0,
    std::max(0, static_cast<int>(state_.manifest.nodes.size()) - 1)
  );

  if (index < 0 || index >= static_cast<int>(state_.manifest.nodes.size())) {
    return nullptr;
  }

  return &state_.manifest.nodes[static_cast<std::size_t>(index)];
}

const MediaNode *App::nodeAtPath(const MediaNode *root, const std::vector<int> &path) const {
  const MediaNode *node = root;

  for (int index : path) {
    if (!node || node->children.empty()) {
      return node;
    }

    int safeIndex = std::clamp(
      index,
      0,
      std::max(0, static_cast<int>(node->children.size()) - 1)
    );

    node = &node->children[static_cast<std::size_t>(safeIndex)];
  }

  return node;
}

MediaNode *App::nodeAtPath(MediaNode *root, const std::vector<int> &path) {
  MediaNode *node = root;

  for (int index : path) {
    if (!node || node->children.empty()) {
      return node;
    }

    int safeIndex = std::clamp(
      index,
      0,
      std::max(0, static_cast<int>(node->children.size()) - 1)
    );

    node = &node->children[static_cast<std::size_t>(safeIndex)];
  }

  return node;
}

const MediaNode *App::currentNodeParent() const {
  return nodeAtPath(selectedRootNode(), state_.nodePath);
}

MediaNode *App::currentNodeParent() {
  return nodeAtPath(selectedRootNode(), state_.nodePath);
}

std::vector<const MediaNode *> App::currentNodeChildren() const {
  std::vector<const MediaNode *> nodes;
  const MediaNode *parent = currentNodeParent();

  if (!parent) {
    return nodes;
  }

  for (const auto &child : parent->children) {
    nodes.push_back(&child);
  }

  return nodes;
}

bool App::currentNodeChildrenAreItems() const {
  const MediaNode *parent = currentNodeParent();

  if (!parent || parent->children.empty()) {
    return false;
  }

  bool hasPlayableItem = false;

  for (const auto &child : parent->children) {
    if (nodeCanHaveChildren(child)) {
      return false;
    }

    if (child.playable || !child.url.empty()) {
      hasPlayableItem = true;
    }
  }

  return hasPlayableItem;
}

const MediaNode *App::selectedCurrentNode() const {
  std::vector<const MediaNode *> children = currentNodeChildren();

  if (children.empty()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedCategory,
    0,
    std::max(0, static_cast<int>(children.size()) - 1)
  );

  return children[static_cast<std::size_t>(index)];
}

MediaNode *App::selectedCurrentNode() {
  MediaNode *parent = currentNodeParent();

  if (!parent || parent->children.empty()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedCategory,
    0,
    std::max(0, static_cast<int>(parent->children.size()) - 1)
  );

  return &parent->children[static_cast<std::size_t>(index)];
}

std::vector<const MediaNode *> App::previewNodeChildren() const {
  std::vector<const MediaNode *> nodes;

  if (currentNodeChildrenAreItems()) {
    const MediaNode *parent = currentNodeParent();

    if (!parent) {
      return nodes;
    }

    for (const auto &child : parent->children) {
      nodes.push_back(&child);
    }

    return nodes;
  }

  const MediaNode *selected = selectedCurrentNode();

  if (!selected) {
    return nodes;
  }

  if (!selected->children.empty()) {
    for (const auto &child : selected->children) {
      nodes.push_back(&child);
    }
    return nodes;
  }

  if (selected->playable || !selected->url.empty()) {
    nodes.push_back(selected);
  }

  return nodes;
}

const MediaNode *App::selectedPreviewNode() const {
  std::vector<const MediaNode *> nodes = previewNodeChildren();

  if (nodes.empty()) {
    return nullptr;
  }

  int index = std::clamp(
    state_.selectedChannel,
    0,
    std::max(0, static_cast<int>(nodes.size()) - 1)
  );

  return nodes[static_cast<std::size_t>(index)];
}

MediaNode *App::selectedPreviewNode() {
  if (currentNodeChildrenAreItems()) {
    MediaNode *parent = currentNodeParent();

    if (!parent || parent->children.empty()) {
      return nullptr;
    }

    int index = std::clamp(
      state_.selectedChannel,
      0,
      std::max(0, static_cast<int>(parent->children.size()) - 1)
    );

    return &parent->children[static_cast<std::size_t>(index)];
  }

  MediaNode *selected = selectedCurrentNode();

  if (!selected) {
    return nullptr;
  }

  if (!selected->children.empty()) {
    int index = std::clamp(
      state_.selectedChannel,
      0,
      std::max(0, static_cast<int>(selected->children.size()) - 1)
    );

    return &selected->children[static_cast<std::size_t>(index)];
  }

  if (selected->playable || !selected->url.empty()) {
    return selected;
  }

  return nullptr;
}

bool App::ensureNodeChildrenLoaded(MediaNode &node) {
  if (!node.children.empty()) {
    return true;
  }

  if (!nodeCanHaveChildren(node)) {
    state_.message = "Empty folder";
    return false;
  }

  const PlaylistConfig *playlist = activePlaylist();
  if (!playlist) {
    state_.message = "No playlist loaded";
    return false;
  }

  NodeChildrenPage firstPage;
  std::vector<MediaNode> items;
  bool usedOnlyCache = true;

  const std::string loadingKey = "node:" + node.id;

  try {
    state_.loading = false;
    state_.loadingMessage.clear();
    state_.channelListLoading = true;
    state_.channelListLoadingKey = loadingKey;
    state_.message = "Loading " + (node.title.empty() ? node.name : node.title) + "...";
    render();

    api_ = ParserApiClient(state_.config);

    const std::string sourceUrl = state_.manifest.source.empty()
      ? playlist->sourceUrl()
      : state_.manifest.source;
    const StreamType streamType = streamTypeFromString(node.type);
    const int pageSize = 250;
    int pageNumber = 1;
    int totalItems = 0;
    int totalPages = 1;

    while (pageNumber <= totalPages && pageNumber <= 200) {
      NodeChildrenPage page;
      const bool fromCache = loadNodeChildrenPage(playlist->id, node.id, pageNumber, page);

      if (!fromCache) {
        usedOnlyCache = false;
        page = api_.loadNodeChildren(
          sourceUrl,
          state_.manifest.provider,
          streamType,
          node.id,
          pageNumber,
          pageSize
        );

        saveNodeChildrenPage(playlist->id, node.id, page);
      }

      if (pageNumber == 1) {
        firstPage = page;
      }

      if (page.totalItems > totalItems) {
        totalItems = page.totalItems;
      }

      totalPages = std::max(1, page.totalPages);

      items.insert(
        items.end(),
        std::make_move_iterator(page.items.begin()),
        std::make_move_iterator(page.items.end())
      );

      if (!page.hasNextPage) {
        break;
      }

      ++pageNumber;
    }
  } catch (const std::exception &ex) {
    state_.loading = false;
    state_.loadingMessage.clear();
    if (state_.channelListLoadingKey == loadingKey) {
      state_.channelListLoading = false;
      state_.channelListLoadingKey.clear();
    }
    state_.message = "Failed to load folder items.";
    std::printf("[KBORE] node children load failed: %s\n", ex.what());
    return false;
  }

  state_.loading = false;
  state_.loadingMessage.clear();
  if (state_.channelListLoadingKey == loadingKey) {
    state_.channelListLoading = false;
    state_.channelListLoadingKey.clear();
  }

  node.children = std::move(items);
  node.childCount = std::max(node.childCount, static_cast<int>(node.children.size()));
  if (firstPage.totalItems > 0) {
    node.totalItems = firstPage.totalItems;
    if (node.totalChannels <= 0) {
      node.totalChannels = firstPage.totalItems;
    }
  }
  // Mark the node as loaded even when the API legitimately returns an empty
  // list. The dashboard must be able to enter/render this empty child list
  // based on the parent currently in focus instead of treating the A press as
  // a failed load.
  node.hasChildren = !node.children.empty() || node.childCount > 0;

  if (node.children.empty()) {
    state_.message = usedOnlyCache ? "Folder cache is empty" : "Folder has no items";
    return true;
  }

  state_.message = usedOnlyCache ? "Loaded folder from cache" : "Loaded folder from API";
  return true;
}

Channel App::channelFromNode(const MediaNode &node) const {
  Channel channel;
  channel.id = node.id;
  channel.name = node.title.empty() ? node.name : node.title;
  channel.url = node.url;
  channel.logo = node.logo;
  channel.group = node.group;
  channel.groupId = node.groupId;
  channel.tvgId = node.tvgId;
  channel.tvgName = node.tvgName;
  channel.streamId = node.streamId;
  channel.type = streamTypeFromString(node.type);
  return channel;
}

void App::enterNode(const MediaNode &node, int childIndex) {
  if (!usingNodeTree()) {
    return;
  }

  // Empty children are valid after pressing A: the selected parent may have
  // been loaded from the API and legitimately returned zero items. Enter it so
  // the UI can render the empty list for that parent.
  const bool enteredNodeContainsItems = nodeChildrenAreItems(node) || node.children.empty();

  state_.nodePath.push_back(childIndex);
  state_.selectedCategory = 0;
  state_.selectedChannel = 0;
  resetLoadedChannels();
  state_.focus = enteredNodeContainsItems ? FocusColumn::Channels : FocusColumn::Categories;
}

void App::playNode(const MediaNode &node) {
  if (node.url.empty()) {
    state_.message = "This item has no playback URL";
    return;
  }

  // Do not rewrite loadedChannels/selectedChannel here. In the dynamic tree
  // browser the selected item lives in nodePreview, and replacing the channel
  // list would make the cursor return to the top after playback.
  openChannel(channelFromNode(node));
}

std::string App::breadcrumbText() const {
  if (!usingNodeTree()) {
    return "";
  }

  std::string out = activePlaylistName();
  const MediaNode *root = selectedRootNode();

  if (!root) {
    return out;
  }

  out += " > ";
  out += root->title.empty() ? root->name : root->title;

  const MediaNode *node = root;
  for (int index : state_.nodePath) {
    if (!node || node->children.empty()) {
      break;
    }

    int safeIndex = std::clamp(
      index,
      0,
      std::max(0, static_cast<int>(node->children.size()) - 1)
    );

    node = &node->children[static_cast<std::size_t>(safeIndex)];
    out += " > ";
    out += node->title.empty() ? node->name : node->title;
  }

  return out;
}

void App::normalizeIndexes() {
  auto types = visibleTypes();
  state_.selectedType = std::clamp(state_.selectedType, 0, std::max(0, static_cast<int>(types.size()) - 1));

  if (usingNodeTree()) {
    const MediaNode *root = selectedRootNode();

    if (!root) {
      state_.nodePath.clear();
      state_.selectedCategory = 0;
      state_.selectedChannel = 0;
      return;
    }

    // Clamp the stored path defensively, because different playlists may have
    // different tree depths and branch sizes.
    const MediaNode *node = root;
    std::vector<int> safePath;

    for (int index : state_.nodePath) {
      if (!node || node->children.empty()) {
        break;
      }

      int safeIndex = std::clamp(
        index,
        0,
        std::max(0, static_cast<int>(node->children.size()) - 1)
      );

      safePath.push_back(safeIndex);
      node = &node->children[static_cast<std::size_t>(safeIndex)];
    }

    state_.nodePath = safePath;

    const auto children = currentNodeChildren();
    if (currentNodeChildrenAreItems()) {
      state_.selectedCategory = 0;
    } else {
      state_.selectedCategory = std::clamp(
        state_.selectedCategory,
        0,
        std::max(0, static_cast<int>(children.size()) - 1)
      );
    }

    const auto preview = previewNodeChildren();
    state_.selectedChannel = std::clamp(
      state_.selectedChannel,
      0,
      std::max(0, static_cast<int>(preview.size()) - 1)
    );

    return;
  }

  const TypeGroup *type = selectedTypeGroup();
  int categories = type ? static_cast<int>(type->categories.size()) : 0;
  state_.selectedCategory = std::clamp(state_.selectedCategory, 0, std::max(0, categories - 1));
  state_.selectedChannel = std::clamp(state_.selectedChannel, 0, std::max(0, static_cast<int>(state_.loadedChannels.size()) - 1));
}


static int windowStart(int selected, int size, int maxRows) {
  if (size <= maxRows) return 0;
  int half = maxRows / 2;
  return std::max(0, std::min(selected - half, size - maxRows));
}

static int gridWindowStart(int selected, int size, int columns, int rows) {
  if (size <= 0 || columns <= 0 || rows <= 0) {
    return 0;
  }

  const int capacity = columns * rows;
  if (size <= capacity) {
    return 0;
  }

  const int selectedRow = std::max(0, selected) / columns;
  const int totalRows = (size + columns - 1) / columns;
  const int firstRow = std::max(0, std::min(selectedRow - rows / 2, totalRows - rows));
  return firstRow * columns;
}

void App::render() {
  drainFinishedChannelLoads();
  drainFinishedEpg();
  gfx_.beginFrame();

  if (splashVisible_) {
    const long long elapsed = nowMs() - splashStartedAtMs_;

    if (elapsed < splashDurationMs_) {
      renderSplashGraphic();
      gfx_.present();
      return;
    }

    splashVisible_ = false;
  }

  switch (state_.screen) {
    case ScreenId::Dashboard: renderDashboardGraphic(); break;
    case ScreenId::AddPlaylist: renderAddPlaylistGraphic(); break;
    case ScreenId::Player: renderPlayerGraphic(); break;
    case ScreenId::Settings: renderSettingsGraphic(); break;
    case ScreenId::Playlists: renderAddPlaylistGraphic(); break;
  }

  if (state_.screen == ScreenId::Player && player_ && player_->nativeVideoActive()) {
    return;
  }

  gfx_.present();
}

void App::renderSplashGraphic() {
  gfx_.drawImageFile(
    "romfs:/logo/splash.png",
    0,
    0,
    Graphics::Width,
    Graphics::Height,
    true
  );
}

void App::renderDashboard() {
  renderDashboardGraphic();
}

void App::renderDashboardGraphic() {
  normalizeIndexes();

  auto types = visibleTypes();
  std::vector<Category> categories;
  const TypeGroup *type = selectedTypeGroup();

  std::vector<const MediaNode *> nodeChildren;
  std::vector<const MediaNode *> nodePreview;
  Channel selectedNodeChannel;
  bool hasSelectedNodeChannel = false;

  if (usingNodeTree()) {
    nodeChildren = currentNodeChildren();
    nodePreview = previewNodeChildren();

    if (currentNodeChildrenAreItems()) {
      const MediaNode *parent = currentNodeParent();

      if (parent) {
        Category category;
        category.id = parent->id;
        category.name = parent->title.empty() ? parent->name : parent->title;
        category.totalChannels = nodeCount(*parent);
        category.type = streamTypeFromString(parent->type);
        categories.push_back(category);
      }
    } else {
      for (const MediaNode *node : nodeChildren) {
        if (!node) {
          continue;
        }

        Category category;
        category.id = node->id;
        category.name = node->title.empty() ? node->name : node->title;
        category.totalChannels = nodeCount(*node);
        category.type = streamTypeFromString(node->type);
        categories.push_back(category);
      }
    }

    const MediaNode *previewNode = selectedPreviewNode();
    if (previewNode && (previewNode->playable || !previewNode->url.empty())) {
      selectedNodeChannel = channelFromNode(*previewNode);
      hasSelectedNodeChannel = true;
    }
  } else if (type) {
    categories = type->categories;
  }

  const Channel *selectedChannel = hasSelectedNodeChannel
    ? &selectedNodeChannel
    : selectedChannelPtr();

  const Category *selectedCategory = selectedCategoryPtr();
  const std::string provider = providerLabel(state_.hasManifest ? state_.manifest.provider : Provider::Local);

  const int listVisibleRows = 7;
  const int gridColumns = 2;
  const int gridRows = 3;
  const int visibleItemCapacity = state_.channelGridView ? gridColumns * gridRows : listVisibleRows;
  std::string footerUnit = "CHANNELS";
  int dashboardItemLoaded = static_cast<int>(state_.loadedChannels.size());
  int dashboardItemTotal = state_.loadedTotal;
  int dashboardPage = std::max(1, state_.loadedPage);
  int dashboardTotalPages = std::max(1, state_.loadedTotalPages);

  if (usingNodeTree()) {
    dashboardItemLoaded = static_cast<int>(nodePreview.size());
    dashboardItemTotal = dashboardItemLoaded;
    footerUnit = "ITEMS";

    if (currentNodeChildrenAreItems()) {
      const MediaNode *parent = currentNodeParent();
      if (parent) {
        dashboardItemTotal = std::max(dashboardItemTotal, nodeCount(*parent));
      }
    } else if (const MediaNode *selectedNode = selectedCurrentNode()) {
      dashboardItemTotal = std::max(dashboardItemTotal, nodeCount(*selectedNode));
    }

    const int totalForPages = std::max(dashboardItemTotal, dashboardItemLoaded);
    dashboardTotalPages = std::max(1, (totalForPages + visibleItemCapacity - 1) / visibleItemCapacity);
    dashboardPage = totalForPages <= 0
      ? 1
      : std::clamp((state_.selectedChannel / visibleItemCapacity) + 1, 1, dashboardTotalPages);
  } else {
    if (dashboardItemTotal <= 0) {
      if (selectedCategory) {
        dashboardItemTotal = selectedCategory->totalChannels;
      } else if (type) {
        dashboardItemTotal = type->totalChannels;
      }
    }

    if (type && type->id != StreamType::Live) {
      footerUnit = "ITEMS";
    }
  }

  const Color text = rgb(248,250,252);
  const Color textSoft = rgb(218,226,244);
  const Color muted = rgb(150,163,190);
  const Color panelTop = rgb(16,24,45);
  const Color panelBottom = rgb(7,11,22);
  const Color panelBorder = rgb(30,42,68);
  const Color blue = rgb(20,132,255);
  const Color brightBlue = rgb(0,190,255);
  const Color green = rgb(57,220,35);
  const Color card = rgba(13,20,37,220);

  auto drawLogoOrFallback = [&](const Channel &channel, int x, int y, int w, int h) {
    if (!channel.logo.empty()) {
      const Bitmap *bitmap = imageCache_.peek(channel.logo);
      if (bitmap && bitmap->valid()) {
        gfx_.drawImage(*bitmap, x, y, w, h);
        return;
      }

      // Queue logo decoding/download without blocking list navigation.
      imageCache_.request(channel.logo);
    }

    gfx_.drawLogoFallback(channel.name, x, y, w, h, 2);
  };


  // Header -----------------------------------------------------------------
  gfx_.drawImageFileCentered(
    "romfs:/logo/logo-horizontal.png",
    24,
    14,
    280,
    66
  );

  // Active playlist switcher. It intentionally shows only the user-defined
  // playlist name, keeping the header clean.
  Rect playlistSwitch{322, 22, 250, 42};
  gfx_.fillRoundRect(
    playlistSwitch.x,
    playlistSwitch.y,
    playlistSwitch.w,
    playlistSwitch.h,
    13,
    rgba(15, 23, 42, 210)
  );
  const bool playlistFocused = state_.focus == FocusColumn::Playlist;
  gfx_.strokeRoundRect(
    playlistSwitch.x,
    playlistSwitch.y,
    playlistSwitch.w,
    playlistSwitch.h,
    13,
    playlistFocused ? brightBlue : rgba(72, 92, 128, 42),
    playlistFocused ? 3 : 1
  );
  gfx_.drawText(
    Graphics::fitText(activePlaylistName(), 18),
    playlistSwitch.x + 18,
    playlistSwitch.y + 14,
    2,
    text,
    true
  );
  gfx_.drawTextRight(
    "v",
    playlistSwitch.x + playlistSwitch.w - 16,
    playlistSwitch.y + 14,
    2,
    playlistFocused ? brightBlue : muted,
    true
  );

  gfx_.fillCircle(984, 43, 6, green);
  gfx_.drawTextRight("ONLINE", 1080, 36, 3, text, true);
  gfx_.drawTextRight(formatSystemClockTime(), 1190, 30, 5, text, false);
  gfx_.drawHeaderIcon("config", 1216, 20, 38, text);

  auto drawPanel = [&](Rect r, const std::string &title, const std::string &icon, bool focused){
    gfx_.fillVerticalGradient(r.x, r.y, r.w, r.h, panelTop, panelBottom);
    gfx_.strokeRoundRect(r.x, r.y, r.w, r.h, 16, focused ? blue : panelBorder, focused ? 2 : 1);
    gfx_.drawHeaderIcon(icon, r.x + 20, r.y + 17, 34, blue);
    gfx_.drawText(title, r.x + 62, r.y + 23, 3, textSoft, true);
  };

  // Narrower Stream Types + wider channels.
  Rect typesPanel{18, 90, 300, 470};
  Rect categoriesPanel{332, 90, 330, 470};
  Rect channelsPanel{676, 90, 587, 470};

  drawPanel(typesPanel, usingNodeTree() ? "ROOT" : "STREAM TYPES", "layers", state_.focus == FocusColumn::Types);

  std::string categoriesTitle = "CATEGORIES";
  if (usingNodeTree()) {
    const MediaNode *parent = currentNodeParent();
    if (parent) {
      categoriesTitle = Graphics::fitText(parent->title.empty() ? parent->name : parent->title, 18);
    }
  }

  drawPanel(categoriesPanel, categoriesTitle, "categories", state_.focus == FocusColumn::Categories);

  std::string chTitle = usingNodeTree() ? "NEXT / ITEMS" : "CHANNELS";
  if (!usingNodeTree() && type) chTitle += " (" + type->label + ")";
  chTitle += state_.channelGridView ? " - GRID" : " - LIST";
  drawPanel(channelsPanel, chTitle, "channels", state_.focus == FocusColumn::Channels);
  gfx_.drawTextRight(std::to_string(dashboardItemTotal) + " " + footerUnit, channelsPanel.x + channelsPanel.w - 28, channelsPanel.y + 25, 2, muted, false);
  const bool channelPanelLoading =
    state_.channelListLoading &&
    !state_.loadedCategoryKey.empty() &&
    state_.channelListLoadingKey == state_.loadedCategoryKey;

  // Stream type cards -------------------------------------------------------
  int typeY = typesPanel.y + 70;
  for (int i=0; i<std::min(4, (int)types.size()); ++i) {
    const auto &t = types[i];
    const bool selected = i == state_.selectedType;
    Color base = typeColor(toString(t.id));
    int y = typeY + i * 74;
    gfx_.fillHorizontalGradient(typesPanel.x + 14, y, typesPanel.w - 28, 64, rgba(base.r,base.g,base.b, selected?110:55), rgba(12,18,34, selected?245:210));
    gfx_.fillRoundRect(typesPanel.x + 14, y, typesPanel.w - 28, 64, 13, rgba(0,0,0,0));
    gfx_.strokeRoundRect(typesPanel.x + 14, y, typesPanel.w - 28, 64, 13, selected ? brightBlue : rgba(72,92,128,24), selected ? 3 : 1);
    gfx_.drawIconBox(toString(t.id), typesPanel.x + 26, y + 10, 44, lighten(base,25), darken(base,30), text);
    gfx_.drawText(Graphics::fitText(t.label, 12), typesPanel.x + 82, y + 15, 3, text, true);
    // Stream types sub label
    // std::string sub = t.id == StreamType::Live ? "LIVE CHANNELS" : (t.id == StreamType::Movies ? "MOVIES" : (t.id == StreamType::Series ? "SERIES" : "RADIOS"));
    // gfx_.drawText(sub, typesPanel.x + 82, y + 42, 2, muted, false);
    if (t.totalChannels > 0) gfx_.drawBadge(std::to_string(t.totalChannels), typesPanel.x + typesPanel.w - 68, y + 21, 42, 24, rgba(30,41,59,210), text);
  }

  int cy = typesPanel.y + typesPanel.h - 74;
  gfx_.fillRoundRect(typesPanel.x + 14, cy, typesPanel.w - 28, 56, 13, rgba(15,23,42,220));
  gfx_.drawIconBox("OK", typesPanel.x+26, cy+9, 38, rgb(22,101,52), rgb(20,83,45), green);
  gfx_.drawText("CONNECTED", typesPanel.x+78, cy+12, 3, text, true);
  gfx_.drawText((provider + " ONLINE"), typesPanel.x+78, cy+37, 2, green, false);

  if (usingNodeTree()) {
    gfx_.drawText(
      Graphics::fitText(breadcrumbText(), 64),
      categoriesPanel.x + 18,
      categoriesPanel.y + categoriesPanel.h - 28,
      1,
      muted,
      false
    );
  }

  // Categories: no side acronym/icon, smaller text -------------------------
  int catRows = 9;
  int catStart = windowStart(state_.selectedCategory, (int)categories.size(), catRows);
  for (int i=0; i<catRows; ++i) {
    int index = catStart + i;
    int y = categoriesPanel.y + 68 + i * 42;
    if (index >= (int)categories.size()) break;
    const auto &c = categories[index];
    bool selected = index == state_.selectedCategory;
    gfx_.fillRoundRect(categoriesPanel.x + 14, y, categoriesPanel.w - 30, 36, 10, selected ? rgba(18,45,94,230) : rgba(15,23,42,180));
    gfx_.strokeRoundRect(categoriesPanel.x + 14, y, categoriesPanel.w - 30, 36, 10, selected ? brightBlue : rgba(72,92,128,24), selected ? 2 : 1);
    gfx_.drawText(Graphics::fitText(c.name, 24), categoriesPanel.x + 32, y + 10, 2, selected ? text : textSoft, true);
    gfx_.drawBadge(std::to_string(c.totalChannels), categoriesPanel.x + categoriesPanel.w - 72, y + 8, 42, 22, rgba(41,54,82,220), text);
  }

  // Channels/movies/items ----------------------------------------------------
  const int channelRows = state_.channelGridView ? gridRows : listVisibleRows;

  auto drawItemCard = [&](const Channel &ch, const std::string &subtitle, const std::string &indicator, bool selected, int x, int y, int w, int h) {
    gfx_.fillRoundRect(x, y, w, h, 14, selected ? rgba(12,23,52,245) : rgba(10,15,29,215));
    gfx_.strokeRoundRect(x, y, w, h, 14, selected ? brightBlue : rgba(72,92,128,24), selected ? 3 : 1);

    drawLogoOrFallback(ch, x + 12, y + 24, 64, 58);

    const int textX = x + 88;
    gfx_.drawText(Graphics::fitText(ch.name, 17), textX, y + 16, 2, text, true);
    drawWrappedText(
      gfx_,
      Graphics::fitText(subtitle, 66),
      textX,
      y + 42,
      3,
      26,
      1,
      selected ? textSoft : muted,
      false
    );

    gfx_.drawTextRight(indicator, x + w - 14, y + 16, 2, muted, false);
  };

  if (usingNodeTree()) {
    if (state_.channelGridView) {
      const int cardGap = 12;
      const int cardW = (channelsPanel.w - 32 - cardGap) / 2;
      const int cardH = 108;
      const int startX = channelsPanel.x + 14;
      const int startY = channelsPanel.y + 66;
      const int start = gridWindowStart(
        state_.selectedChannel,
        static_cast<int>(nodePreview.size()),
        gridColumns,
        gridRows
      );

      for (int row = 0; row < gridRows; ++row) {
        for (int col = 0; col < gridColumns; ++col) {
          const int index = start + row * gridColumns + col;
          if (index >= static_cast<int>(nodePreview.size())) {
            break;
          }

          const MediaNode *node = nodePreview[static_cast<std::size_t>(index)];
          if (!node) {
            continue;
          }

          const bool selected = index == state_.selectedChannel;
          const bool playable = node->playable || !node->url.empty();
          const bool folder = nodeCanHaveChildren(*node);
          Channel ch = channelFromNode(*node);
          std::string sub = folder
            ? ("FOLDER • " + std::to_string(nodeCount(*node)) + " ITEMS")
            : (playable ? epgLineForChannel(ch) : "EMPTY");
          std::string indicator = state_.favorites.count(node->id) ? "*" : (folder ? ">" : "<3");

          drawItemCard(
            ch,
            sub,
            indicator,
            selected,
            startX + col * (cardW + cardGap),
            startY + row * (cardH + cardGap),
            cardW,
            cardH
          );
        }
      }
    } else {
      int chanStart = windowStart(state_.selectedChannel, static_cast<int>(nodePreview.size()), channelRows);

      for (int i = 0; i < channelRows; ++i) {
        int index = chanStart + i;
        int y = channelsPanel.y + 66 + i * 55;

        if (index >= static_cast<int>(nodePreview.size())) {
          break;
        }

        const MediaNode *node = nodePreview[static_cast<std::size_t>(index)];
        if (!node) {
          continue;
        }

        bool selected = index == state_.selectedChannel;
        const bool playable = node->playable || !node->url.empty();
        const bool folder = nodeCanHaveChildren(*node);

        gfx_.fillRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? rgba(12,23,52,245) : rgba(10,15,29,215));
        gfx_.strokeRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? brightBlue : rgba(72,92,128,24), selected ? 2 : 1);

        Channel ch = channelFromNode(*node);
        drawLogoOrFallback(ch, channelsPanel.x + 30, y + 7, 48, 35);

        const int nameX = channelsPanel.x + 94;
        gfx_.drawText(Graphics::fitText(ch.name, 35), nameX, y + 9, 3, text, true);

        std::string sub = folder
          ? ("FOLDER • " + std::to_string(nodeCount(*node)) + " ITEMS")
          : (playable ? epgLineForChannel(ch) : "EMPTY");

        gfx_.drawText(Graphics::fitText(sub, 42), nameX, y + 32, 2, muted, false);
        gfx_.drawText(state_.favorites.count(node->id) ? "*" : (folder ? ">" : "<3"), channelsPanel.x + channelsPanel.w - 42, y + 15, 2, muted, false);
      }
    }

    if (nodePreview.empty()) {
      gfx_.drawText("SELECT A FOLDER OR ITEM", channelsPanel.x + 40, channelsPanel.y + 178, 3, muted, false);
      gfx_.drawText("PRESS A TO OPEN", channelsPanel.x + 40, channelsPanel.y + 206, 2, blue, true);
    }
  } else {
    if (state_.channelGridView) {
      const int cardGap = 12;
      const int cardW = (channelsPanel.w - 32 - cardGap) / 2;
      const int cardH = 108;
      const int startX = channelsPanel.x + 14;
      const int startY = channelsPanel.y + 66;
      const int start = gridWindowStart(
        state_.selectedChannel,
        static_cast<int>(state_.loadedChannels.size()),
        gridColumns,
        gridRows
      );

      for (int row = 0; row < gridRows; ++row) {
        for (int col = 0; col < gridColumns; ++col) {
          const int index = start + row * gridColumns + col;
          if (index >= static_cast<int>(state_.loadedChannels.size())) {
            break;
          }

          const auto &ch = state_.loadedChannels[static_cast<std::size_t>(index)];
          const bool selected = index == state_.selectedChannel;
          drawItemCard(
            ch,
            epgLineForChannel(ch),
            state_.favorites.count(ch.id) ? "*" : "<3",
            selected,
            startX + col * (cardW + cardGap),
            startY + row * (cardH + cardGap),
            cardW,
            cardH
          );
        }
      }
    } else {
      int chanStart = windowStart(state_.selectedChannel, (int)state_.loadedChannels.size(), channelRows);
      for (int i=0; i<channelRows; ++i) {
        int index = chanStart + i;
        int y = channelsPanel.y + 66 + i * 55;
        if (index >= (int)state_.loadedChannels.size()) break;
        const auto &ch = state_.loadedChannels[index];
        bool selected = index == state_.selectedChannel;
        gfx_.fillRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? rgba(12,23,52,245) : rgba(10,15,29,215));
        gfx_.strokeRoundRect(channelsPanel.x + 14, y, channelsPanel.w - 32, 49, 12, selected ? brightBlue : rgba(72,92,128,24), selected ? 2 : 1);

        // Prefer the real logo from the API. If it is missing or cannot be decoded, use a small acronym fallback.
        drawLogoOrFallback(ch, channelsPanel.x + 30, y + 7, 48, 35);

        const int nameX = channelsPanel.x + 94;
        gfx_.drawText(Graphics::fitText(ch.name, 35), nameX, y + 9, 3, text, true);
        gfx_.drawText(Graphics::fitText(epgLineForChannel(ch), 42), nameX, y + 32, 2, muted, false);
        gfx_.drawText(state_.favorites.count(ch.id) ? "*" : "<3", channelsPanel.x + channelsPanel.w - 42, y + 15, 2, muted, false);
      }
    }

    if (state_.loadedChannels.empty()) {
      gfx_.drawText("SELECT A CATEGORY", channelsPanel.x + 40, channelsPanel.y + 178, 3, muted, false);
      gfx_.drawText("PRESS A TO LOAD", channelsPanel.x + 40, channelsPanel.y + 206, 2, blue, true);
    }
  }

  const bool itemListLoading = state_.channelListLoading && (usingNodeTree() || channelPanelLoading);
  if (itemListLoading) {
    const char spinnerChars[4] = {'|', '/', '-', '\\'};
    const int spinnerIndex = static_cast<int>((nowMs() / 140) % 4);
    std::string spinner(1, spinnerChars[spinnerIndex]);
    const int badgeW = 190;
    const int badgeH = 34;
    const int badgeX = channelsPanel.x + channelsPanel.w - badgeW - 22;
    const int badgeY = channelsPanel.y + channelsPanel.h - badgeH - 18;

    gfx_.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 12, rgba(15, 23, 42, 235));
    gfx_.strokeRoundRect(badgeX, badgeY, badgeW, badgeH, 12, rgba(0, 191, 255, 115), 1);
    gfx_.drawText(spinner, badgeX + 16, badgeY + 8, 2, brightBlue, true);
    gfx_.drawText("Loading items", badgeX + 42, badgeY + 10, 1, text, true);
  }

  // Info panel: smaller footer text ----------------------------------------
  Rect info{18, 575, 1245, 88};
  gfx_.fillVerticalGradient(info.x, info.y, info.w, info.h, rgba(27,35,52,235), rgba(13,18,29,235));
  gfx_.fillRoundRect(info.x, info.y, info.w, info.h, 14, rgba(0,0,0,0));
  gfx_.strokeRoundRect(info.x, info.y, info.w, info.h, 14, rgba(72,92,128,35), 1);
  if (selectedChannel) {
    drawLogoOrFallback(*selectedChannel, info.x + 34, info.y + 13, 154, 62);
    gfx_.drawText(Graphics::fitText(selectedChannel->name, 42), info.x + 210, info.y + 18, 2, text, true);
    drawWrappedText(gfx_, epgNowNextLine(*selectedChannel), info.x + 210, info.y + 46, 2, 86, 1, muted);
  } else {
    gfx_.drawText(state_.hasManifest ? Graphics::fitText(state_.manifest.name, 34) : "NSTV", info.x + 40, info.y + 30, 4, text, true);
  }
  gfx_.drawText("PAGE " + std::to_string(dashboardPage) + " / " + std::to_string(dashboardTotalPages), info.x + 610, info.y + 24, 2, text, true);
  gfx_.drawText("LOADED: " + std::to_string(dashboardItemLoaded) + " / " + std::to_string(dashboardItemTotal) + " " + footerUnit, info.x + 610, info.y + 50, 1, blue, true);
  gfx_.drawText(provider + ": " + Graphics::fitText(state_.hasManifest ? state_.manifest.name : "CONFIGURE", 38), info.x + 940, info.y + 24, 2, text, false);
  gfx_.drawText(std::string("USABILITY 1.0.1 ") + (state_.channelGridView ? "GRID" : "LIST"), info.x + 978, info.y + 54, 1, green, false);

  // Controls footer: smaller text ------------------------------------------
  Rect foot{18, 675, 1245, 36};
  gfx_.fillRoundRect(foot.x, foot.y, foot.w, foot.h, 10, rgba(17,24,39,240));
  gfx_.strokeRoundRect(foot.x, foot.y, foot.w, foot.h, 10, rgba(72,92,128,28), 1);
  gfx_.drawText("UP - PLAYLISTS   |    LEFT/RIGHT - COLUMNS/CARD   |    L/R - SKIP 10   |    A - SELECT   |    B - BACK   |    X - VIEW   |    + - PLAYLISTS", foot.x + 26, foot.y + 13, 1, text, true);

  if (state_.loading) {
    renderLoadingOverlay(state_.loadingMessage.empty() ? "Loading" : state_.loadingMessage);
  }
}


void App::renderLoadingOverlay(const std::string &message) {
  const Color text = rgb(248, 250, 252);
  const Color muted = rgb(148, 163, 184);
  const Color blue = rgb(0, 191, 255);

  gfx_.fillRoundRect(
    0,
    0,
    Graphics::Width,
    Graphics::Height,
    0,
    rgba(2, 6, 18, 180)
  );

  const int cx = Graphics::Width / 2;
  const int cy = Graphics::Height / 2 - 18;

  const char spinnerChars[4] = {'|', '/', '-', '\\'};
  const int index = static_cast<int>((nowMs() / 140) % 4);
  std::string spinner(1, spinnerChars[index]);

  gfx_.fillRoundRect(cx - 170, cy - 74, 340, 170, 22, rgba(15, 23, 42, 235));
  gfx_.strokeRoundRect(cx - 170, cy - 74, 340, 170, 22, rgba(72, 92, 128, 70), 1);

  gfx_.drawText(spinner, cx - 10, cy - 36, 5, blue, true);
  gfx_.drawText(message.empty() ? "Loading" : message, cx - 58, cy + 22, 3, text, true);
  gfx_.drawText("Please wait", cx - 56, cy + 56, 1, muted, false);
}

void App::renderAddPlaylistGraphic() {
  const Color text = rgb(248, 250, 252);
  const Color muted = rgb(166, 178, 207);
  const Color blue = rgb(37, 99, 235);
  const Color brightBlue = rgb(0, 191, 255);
  const Color panel = rgba(17, 24, 39, 225);

  gfx_.drawImageFileCentered(
    "romfs:/logo/logo-horizontal.png",
    34,
    35,
    320,
    76
  );

  gfx_.drawText("PLAYLISTS", 64, 124, 5, muted, true);
  gfx_.drawText("Select a saved list or add a new source", 64, 166, 2, muted, false);

  const int playlistCount = static_cast<int>(state_.config.playlists.size());
  const int addM3uIndex = playlistCount;
  const int addXtreamIndex = playlistCount + 1;
  const int settingsIndex = playlistCount + 2;
  const int backIndex = playlistCount + 3;
  const int totalOptions = playlistCount + 4;

  const int rows = 6;
  const int start = windowStart(state_.selectedAddOption, totalOptions, rows);

  for (int i = 0; i < rows; ++i) {
    const int optionIndex = start + i;

    if (optionIndex >= totalOptions) {
      break;
    }

    const int y = 210 + i * 66;
    const bool selected = optionIndex == state_.selectedAddOption;

    gfx_.fillRoundRect(
      96,
      y,
      900,
      54,
      14,
      selected ? rgba(37, 99, 235, 225) : panel
    );

    gfx_.strokeRoundRect(
      96,
      y,
      900,
      54,
      14,
      selected ? brightBlue : rgba(72, 92, 128, 30),
      selected ? 3 : 1
    );

    std::string title;
    std::string subtitle;

    if (optionIndex < playlistCount) {
      const PlaylistConfig &playlist = state_.config.playlists[static_cast<std::size_t>(optionIndex)];
      const bool active = playlist.id == state_.config.activePlaylistId;

      title = (active ? "* " : "  ") + playlist.name;
      subtitle =
        std::string(active ? "ACTIVE • " : "") +
        (playlist.provider == Provider::Xtream ? "XTREAM" : "M3U");
    } else if (optionIndex == addM3uIndex) {
      title = "+ ADD M3U URL";
      subtitle = "Add a playlist using an M3U link";
    } else if (optionIndex == addXtreamIndex) {
      title = "+ ADD XTREAM";
      subtitle = "Add server, username and password";
    } else if (optionIndex == settingsIndex) {
      title = "ABOUT / SETTINGS";
      subtitle = "Disclaimer, credits and app information";
    } else {
      title = "BACK";
      subtitle = "Return to dashboard";
    }

    gfx_.drawText(Graphics::fitText(title, 36), 134, y + 11, 3, text, true);
    gfx_.drawText(Graphics::fitText(subtitle, 60), 134, y + 34, 1, selected ? text : muted, false);
  }

  gfx_.drawText("A SELECT    X DELETE SELECTED    B BACK", 96, 632, 2, muted, true);
  gfx_.drawText(Graphics::fitText(state_.message, 86), 96, 666, 2, rgb(0, 145, 255), false);

  if (state_.loading) {
    renderLoadingOverlay(state_.loadingMessage.empty() ? "Loading" : state_.loadingMessage);
  }
}

void App::renderPlayerGraphic() {
  const Channel *channel = state_.hasPlaybackChannel ? &state_.playbackChannel : selectedChannelPtr();

  bool hasFrame = false;
  const bool isOpen = player_ && player_->isOpen();
  const bool isPaused = player_ && player_->isPaused();
  const bool isAudioOnly = isOpen && player_ && player_->isAudioOnly();
  const bool overlayRequested =
    !state_.playerFrameSeen ||
    nowMs() < state_.playerOverlayUntilMs ||
    isPaused ||
    !isOpen;

  if (isOpen) {
    std::string status;

    if (isPaused) {
      status = "PAUSED";
    } else if (isAudioOnly) {
      status = "PLAYING AUDIO";
    } else if (player_ && player_->hasFrame()) {
      status = "PLAYING";
    } else {
      status = "LOADING";
    }

    const bool vodPlayback = channel && isVodType(channel->type) && player_->canSeek();
    const int64_t vodDurationMs = vodPlayback ? player_->durationMs() : 0;
    const int64_t vodPositionMs = vodPlayback ? player_->positionMs() : 0;
    const std::string vodCounter = vodPlayback
      ? ("  " + formatPlaybackTime(vodPositionMs))
      : "";
    const std::string controls = vodPlayback
      ? "A PAUSE/RESUME   L -30s   < -10s   > +10s   R +30s   B BACK"
      : "A PAUSE/RESUME   B BACK";

    player_->setOverlayInfo(
      channel ? Graphics::fitText(channel->name, 58) : "",
      channel && channel->type == StreamType::Live ? Graphics::fitText(epgNowNextLine(*channel), 98) : "",
      status + vodCounter,
      controls
    );
    player_->setOverlayProgress(vodPositionMs, vodDurationMs, vodPlayback && vodDurationMs > 0);

    if (isAudioOnly) {
      if (gfx_.isSuspendedForNativeVideo()) {
        gfx_.resumeAfterNativeVideo();
      }

      player_->setNativeVideoAllowed(false);
      player_->setOverlayVisible(false);
    } else if (player_->nativeVideoActive() || gfx_.isSuspendedForNativeVideo()) {
      player_->setOverlayVisible(overlayRequested);
      player_->setNativeVideoAllowed(true);
    } else {
      gfx_.suspendForNativeVideo();
      player_->setOverlayVisible(overlayRequested);
      player_->setNativeVideoAllowed(true);
    }

    player_->update();

    if (!isAudioOnly && player_->nativeVideoActive()) {
      hasFrame = true;

      if (!state_.playerFrameSeen) {
        state_.playerFrameSeen = true;
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }
      return;
    }

    if (!isAudioOnly && gfx_.isSuspendedForNativeVideo()) {
      gfx_.resumeAfterNativeVideo();
    }
  }

  gfx_.fillRect(0, 0, Graphics::Width, Graphics::Height, rgb(0, 0, 0));

  if (isOpen) {
    if (isAudioOnly) {
      hasFrame = true;

      gfx_.fillVerticalGradient(
        0,
        0,
        Graphics::Width,
        Graphics::Height,
        rgb(6, 12, 28),
        rgb(1, 5, 12)
      );
      gfx_.drawImageFile("romfs:/images/radio-bg.png", 0, 0, Graphics::Width, Graphics::Height, true);

      if (!state_.playerFrameSeen) {
        state_.playerFrameSeen = true;
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }
    } else if (player_->yuvFrame().valid()) {
      hasFrame = true;

      gfx_.drawYuvFrame(
        player_->yuvFrame(),
        0,
        0,
        Graphics::Width,
        Graphics::Height
      );

      if (!state_.playerFrameSeen) {
        state_.playerFrameSeen = true;
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }
    } else if (player_->frame().valid()) {
      hasFrame = true;

      gfx_.drawImage(
        player_->frame(),
        0,
        0,
        Graphics::Width,
        Graphics::Height
      );

      if (!state_.playerFrameSeen) {
        state_.playerFrameSeen = true;
        state_.playerOverlayUntilMs = nowMs() + 5000;
      }
    } else {
      gfx_.drawText("Loading...", 80, 320, 3, rgb(248, 250, 252), true);
    }
  } else {
    gfx_.fillVerticalGradient(
      0,
      0,
      Graphics::Width,
      Graphics::Height,
      rgb(7, 11, 22),
      rgb(2, 5, 11)
    );

    const int boxW = 520;
    const int boxH = 190;
    const int boxX = (Graphics::Width - boxW) / 2;
    const int boxY = (Graphics::Height - boxH) / 2;

    gfx_.fillRoundRect(
      boxX,
      boxY,
      boxW,
      boxH,
      24,
      rgba(15, 23, 42, 232)
    );

    gfx_.strokeRoundRect(
      boxX,
      boxY,
      boxW,
      boxH,
      24,
      state_.playerLoadFailed ? rgba(248, 113, 113, 120) : rgba(72, 92, 128, 70),
      1
    );

    if (state_.playerLoadFailed) {
      gfx_.drawText(
        "Failed to load stream",
        boxX + 82,
        boxY + 58,
        3,
        rgb(248, 113, 113),
        true
      );

      gfx_.drawText(
        "Press B to return",
        boxX + 164,
        boxY + 104,
        1,
        rgb(203, 213, 225),
        false
      );
    } else {
      const char spinnerChars[4] = {'|', '/', '-', '\\'};
      const int index = static_cast<int>((nowMs() / 140) % 4);
      std::string spinner(1, spinnerChars[index]);

      gfx_.drawText(
        spinner,
        boxX + 244,
        boxY + 40,
        5,
        rgb(0, 191, 255),
        true
      );

      gfx_.drawText(
        "Loading stream",
        boxX + 138,
        boxY + 106,
        3,
        rgb(248, 250, 252),
        true
      );
    }
  }

  const bool showOverlay =
    !state_.playerFrameSeen ||
    nowMs() < state_.playerOverlayUntilMs ||
    isPaused ||
    !isOpen;

  if (showOverlay && isOpen) {
    const bool vodPlayback = channel && isVodType(channel->type) && player_ && player_->canSeek();
    const int64_t vodDurationMs = vodPlayback ? player_->durationMs() : 0;
    const int64_t vodPositionMs = vodPlayback ? player_->positionMs() : 0;
    const std::string controls = vodPlayback
      ? "A PAUSE/RESUME   L -30s   < -10s   > +10s   R +30s   B BACK"
      : "A PAUSE/RESUME   B BACK";

    gfx_.fillHorizontalGradient(
      0,
      Graphics::Height - 104,
      Graphics::Width,
      104,
      rgba(0, 0, 0, 80),
      rgba(0, 0, 0, 220)
    );

    if (channel) {
      const int logoX = 28;
      const int logoY = Graphics::Height - 88;
      const int logoW = 76;
      const int logoH = 58;
      if (!channel->logo.empty()) {
        const Bitmap *bitmap = imageCache_.peek(channel->logo);
        if (bitmap && bitmap->valid()) {
          gfx_.drawImage(*bitmap, logoX, logoY, logoW, logoH);
        } else {
          imageCache_.request(channel->logo);
          gfx_.drawLogoFallback(channel->name, logoX, logoY, logoW, logoH, 2);
        }
      } else {
        gfx_.drawLogoFallback(channel->name, logoX, logoY, logoW, logoH, 2);
      }

      gfx_.drawText(
        Graphics::fitText(channel->name, 58),
        122,
        Graphics::Height - 84,
        2,
        rgb(248, 250, 252),
        true
      );

      if (channel->type == StreamType::Live) {
        drawWrappedText(
          gfx_,
          epgNowNextLine(*channel),
          122,
          Graphics::Height - 58,
          2,
          98,
          1,
          rgb(150, 163, 190),
          false
        );
      }

      std::string status;

      if (isPaused) {
        status = "PAUSED";
      } else if (isAudioOnly) {
        status = "PLAYING AUDIO";
      } else if (isOpen && hasFrame) {
        status = "PLAYING";
      } else if (isOpen) {
        status = "LOADING";
      } else {
        status = "ERROR";
      }

      if (vodPlayback && vodDurationMs > 0) {
        status += "  " + formatPlaybackTime(vodPositionMs);
      }

      gfx_.drawText(
        status,
        122,
        Graphics::Height - 32,
        1,
        isOpen ? rgb(57, 220, 35) : rgb(248, 113, 113),
        true
      );

      if (vodPlayback && vodDurationMs > 0) {
        const int barX = 188;
        const int barY = Graphics::Height - 18;
        const int barW = Graphics::Width - 376;
        const int barH = 5;
        const int64_t clamped = std::max<int64_t>(0, std::min<int64_t>(vodPositionMs, vodDurationMs));
        const int filled = static_cast<int>((clamped * barW) / vodDurationMs);

        gfx_.drawText("0:00", 122, Graphics::Height - 24, 1, rgb(203, 213, 225), false);
        gfx_.drawTextRight(formatPlaybackTime(vodDurationMs), Graphics::Width - 28, Graphics::Height - 24, 1, rgb(203, 213, 225), false);
        gfx_.fillRect(barX, barY, barW, barH, rgba(51, 65, 85, 230));

        if (filled > 0) {
          gfx_.fillRect(barX, barY, std::min(barW, filled), barH, rgb(0, 191, 255));
        }
      }
    }

    gfx_.drawTextRight(
      controls,
      Graphics::Width - 28,
      Graphics::Height - 50,
      1,
      rgb(248, 250, 252),
      true
    );
  }
}

void App::renderSettingsGraphic() {
  const Color text = rgb(248, 250, 252);
  const Color muted = rgb(166, 178, 207);
  const Color blue = rgb(0, 191, 255);
  const Color panelTop = rgb(16, 24, 45);
  const Color panelBottom = rgb(7, 11, 22);

  gfx_.fillVerticalGradient(0, 0, Graphics::Width, Graphics::Height, rgb(7, 11, 22), rgb(2, 5, 11));
  gfx_.drawImageFileCentered("romfs:/logo/logo-horizontal.png", 34, 34, 300, 72);
  gfx_.drawText("SETTINGS / ABOUT", 80, 120, 5, text, true);
  gfx_.drawText("CONFIG: " + Graphics::fitText(configPath(), 72), 80, 158, 2, muted, false);

  Rect about{80, 205, 1120, 365};
  gfx_.fillVerticalGradient(about.x, about.y, about.w, about.h, panelTop, panelBottom);
  gfx_.strokeRoundRect(about.x, about.y, about.w, about.h, 18, rgba(72, 92, 128, 55), 1);

  gfx_.drawText("ABOUT KBORE", about.x + 34, about.y + 28, 4, text, true);
  gfx_.drawText("Kboré is an IPTV/VOD player for user-provided playlists.", about.x + 34, about.y + 76, 2, muted, false);
  gfx_.drawText("The app does not provide playlists, channels, movies, series or IPTV servers.", about.x + 34, about.y + 106, 2, muted, false);
  gfx_.drawText("Kboré does not endorse, host, sponsor or verify any IPTV provider/server.", about.x + 34, about.y + 136, 2, muted, false);
  gfx_.drawText("All content, playlist URLs, credentials and playback sources are the user's responsibility.", about.x + 34, about.y + 166, 2, muted, false);

  gfx_.drawText("THIRD-PARTY LIBRARIES", about.x + 34, about.y + 220, 3, blue, true);
  gfx_.drawText("Thanks to the open-source projects and third-party libraries used by this app,", about.x + 34, about.y + 256, 2, muted, false);
  gfx_.drawText("including devkitPro/libnx, FFmpeg, Deko3D, SDL/cURL and related dependencies.", about.x + 34, about.y + 286, 2, muted, false);
  gfx_.drawText("Thank you for using and supporting Kboré.", about.x + 34, about.y + 326, 2, text, true);
  gfx_.drawText("Developed by Gilson Santos / https://github.com/devgsantos", about.x + 34, about.y + 356, 2, text, true);

  gfx_.drawText("A / B  BACK", 80, 632, 3, muted, true);
}


template <typename T, typename LabelFn>
std::vector<std::string> App::buildWindowRows(
  const std::vector<T> &items,
  int selected,
  int width,
  int maxRows,
  LabelFn labelFn
) const {
  std::vector<std::string> rows;

  if (items.empty()) {
    rows.push_back("  No items");
    return rows;
  }

  int half = maxRows / 2;
  int start = std::max(
    0,
    std::min(
      selected - half,
      std::max(0, static_cast<int>(items.size()) - maxRows)
    )
  );

  int end = std::min(static_cast<int>(items.size()), start + maxRows);

  for (int i = start; i < end; ++i) {
    std::string prefix = (i == selected ? "> " : "  ");
    rows.push_back(crop(prefix + labelFn(items[i]), width));
  }

  return rows;
}

template <typename T, typename LabelFn>
void App::printWindow(const std::string &title, const std::vector<T> &items, int selected, int width, LabelFn labelFn) const {
  std::cout << "\n" << title << "\n";
  if (items.empty()) {
    std::cout << "  No items\n";
    return;
  }
  const int maxRows = 10;
  int half = maxRows / 2;
  int start = std::max(0, std::min(selected - half, std::max(0, static_cast<int>(items.size()) - maxRows)));
  int end = std::min(static_cast<int>(items.size()), start + maxRows);
  for (int i = start; i < end; ++i) {
    std::string label = crop(labelFn(items[i]), width);
    std::cout << (i == selected ? "> " : "  ") << label << "\n";
  }
}

std::string App::crop(const std::string &value, std::size_t max) {
  if (max == 0) return "";

  std::string output;
  output.reserve(std::min(value.size(), max));

  std::size_t visible = 0;
  for (std::size_t i = 0; i < value.size();) {
    unsigned char c = static_cast<unsigned char>(value[i]);

    if (c < 0x20 && value[i] != '\n' && value[i] != '\t') {
      ++i;
      continue;
    }

    std::size_t charLen = 1;
    if ((c & 0x80) == 0x00) charLen = 1;
    else if ((c & 0xE0) == 0xC0) charLen = 2;
    else if ((c & 0xF0) == 0xE0) charLen = 3;
    else if ((c & 0xF8) == 0xF0) charLen = 4;

    if (i + charLen > value.size()) break;

    const bool needsEllipsis = visible + 1 > max;
    if (needsEllipsis) break;

    output.append(value, i, charLen);
    i += charLen;
    ++visible;
  }

  if (output.size() < value.size() && max > 3) {
    while (!output.empty()) {
      unsigned char last = static_cast<unsigned char>(output.back());
      if ((last & 0xC0) != 0x80) break;
      output.pop_back();
    }
    return output + "...";
  }

  return output;
}

std::string App::typeIcon(StreamType type, bool unicodeIcons) {
  if (unicodeIcons) {
    switch (type) {
      case StreamType::Live: return "📺";
      case StreamType::Movies: return "🎬";
      case StreamType::Series: return "▣";
      case StreamType::Radio: return "♪";
      case StreamType::Favorites: return "★";
    }
  }

  switch (type) {
    case StreamType::Live: return "[TV]";
    case StreamType::Movies: return "[MOV]";
    case StreamType::Series: return "[SER]";
    case StreamType::Radio: return "[RAD]";
    case StreamType::Favorites: return "[*]";
  }
  return "[ ]";
}

std::string App::categoryIcon(const Category &category, bool unicodeIcons) {
  return typeIcon(category.type, unicodeIcons);
}

std::string App::channelIcon(const Channel &channel, bool unicodeIcons) {
  if (!channel.logo.empty()) {
    return unicodeIcons ? "🖼" : "[IMG]";
  }

  return typeIcon(channel.type, unicodeIcons);
}

std::string App::providerLabel(Provider provider) {
  switch (provider) {
    case Provider::M3u: return "M3U";
    case Provider::Xtream: return "Xtream API";
    case Provider::Local: return "Local";
  }
  return "Local";
}

std::string App::screenTitle(ScreenId screen) {
  switch (screen) {
    case ScreenId::Dashboard: return "Dashboard";
    case ScreenId::Playlists: return "Playlists";
    case ScreenId::AddPlaylist: return "Add Playlist";
    case ScreenId::Player: return "Player";
    case ScreenId::Settings: return "Settings";
  }
  return "NSTV";
}

} // namespace nstv
