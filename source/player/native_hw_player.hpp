#include <mpv/client.h>

class NativeHwPlayer {
  mpv_handle* mpv = nullptr;

  bool open(const std::string & url) {
    mpv = mpv_create();

    mpv_set_option_string(mpv, "hwdec", "auto");
    mpv_set_option_string(mpv, "vo", "deko3d");
    mpv_set_option_string(mpv, "ao", "switch");

    mpv_initialize(mpv);

    const char* cmd[] = {"loadfile", url.c_str(), nullptr};
    mpv_command_async(mpv, 0, cmd);

    return true;
  }
};