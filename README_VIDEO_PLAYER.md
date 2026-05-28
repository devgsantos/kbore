# NSTV video player

This package replaces the player stub with a real video playback module.

## What changed

- Added `include/nstv/video_player.hpp`
- Added `source/video_player.cpp`
- Dashboard selection now opens the selected channel stream.
- Player screen now renders decoded video frames into the existing graphical UI.
- A button pauses/resumes playback.
- B button stops playback and returns to dashboard.
- The app loop now uses non-blocking input while the player is open, so frames continue updating.

## Decoder backend

The player uses FFmpeg/libav when the build finds FFmpeg libraries.

### Host Linux

Install:

```bash
sudo apt install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

Then:

```bash
make clean
make host
```

The Makefile auto-enables `NSTV_USE_FFMPEG` when `pkg-config` finds:

```text
libavformat
libavcodec
libavutil
libswscale
```

### Nintendo Switch

Install the FFmpeg portlib if available in your devkitPro setup:

```bash
sudo dkp-pacman -S switch-ffmpeg
```

Then:

```bash
make clean
make switch
```

`Makefile.switch` auto-enables `NSTV_USE_FFMPEG` when it finds:

```text
/opt/devkitpro/portlibs/switch/lib/libavformat.a
```

## Current limitations

- Video rendering is implemented.
- Audio output is not implemented yet.
- If FFmpeg is not installed, the app still builds, but the player screen shows a clear error instead of playing.
- For some IPTV streams, codec support depends on your FFmpeg build.
