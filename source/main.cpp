#include "nstv/app.hpp"
#include "nstv/log.hpp"
#include "nstv/platform.hpp"
#include <exception>
#include <iostream>

int main() {
  try {
    nstv::platformInit();
    nstv::logLine("[KBORE] app started; log file: sdmc:/switch/kbore/kbore.log");
    nstv::App app;
    int code = app.run();
    nstv::platformExit();
    return code;
  } catch (const std::exception &ex) {
    std::cerr << "Fatal NSTV error: " << ex.what() << "\n";
    nstv::platformExit();
    return 1;
  }
}
