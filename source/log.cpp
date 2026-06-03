#include "nstv/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <sys/stat.h>

namespace nstv {

namespace {

std::mutex &logMutex() {
  static std::mutex mutex;
  return mutex;
}

const char *logPath() {
#ifdef __SWITCH__
  return "sdmc:/switch/kbore/kbore.log";
#else
  return "kbore.log";
#endif
}

void ensureLogDir() {
#ifdef __SWITCH__
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/kbore", 0777);
#endif
}

void writeLineToFile(const char *line) {
  ensureLogDir();

  FILE *file = std::fopen(logPath(), "ab");
  if (!file) {
    return;
  }

  std::fputs(line, file);
  std::fputc('\n', file);
  std::fflush(file);
  std::fclose(file);
}

bool isLogFileEnabled() {
#ifdef NSTV_PRODUCTION_BUILD
  return false;
#else
  return true;
#endif
}

} // namespace

void logLine(const std::string &message) {
  std::lock_guard<std::mutex> lock(logMutex());

  std::printf("%s\n", message.c_str());
  std::fflush(stdout);
  if (isLogFileEnabled()) {
    writeLineToFile(message.c_str());
  }
}

void logLinef(const char *format, ...) {
  if (!format) {
    return;
  }

  char buffer[2048];

  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  logLine(buffer);
}

} // namespace nstv
