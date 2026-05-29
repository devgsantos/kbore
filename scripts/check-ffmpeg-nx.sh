#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/external/player-sdk"
FFMPEG="$ROOT/external/ffmpeg-nx"

echo "Checking player SDK..."
echo

if [ ! -d "$SDK" ]; then
  echo "ERROR: SDK folder does not exist:"
  echo "  $SDK"
  exit 1
fi

required_headers=(
  "$SDK/include/libavformat/avformat.h"
  "$SDK/include/libavcodec/avcodec.h"
  "$SDK/include/libavutil/hwcontext.h"
  "$SDK/include/libswscale/swscale.h"
  "$SDK/include/libswresample/swresample.h"
)

required_libs=(
  "$SDK/lib/libavformat.a"
  "$SDK/lib/libavcodec.a"
  "$SDK/lib/libavutil.a"
  "$SDK/lib/libswscale.a"
  "$SDK/lib/libswresample.a"
)

ok=1

for file in "${required_headers[@]}"; do
  if [ -f "$file" ]; then
    echo "OK: $file"
  else
    echo "MISSING: $file"
    ok=0
  fi
done

for file in "${required_libs[@]}"; do
  if [ -f "$file" ]; then
    echo "OK: $file"
  else
    echo "MISSING: $file"
    ok=0
  fi
done

echo

if [ -d "$FFMPEG" ]; then
  echo "FFmpeg configure hwaccels currently available:"
  cd "$FFMPEG"
  ./configure --list-hwaccels 2>/dev/null | grep -Ei 'nvtegra|tegra|h264|hevc|vp8|vp9|mjpeg|mpeg' || true
else
  echo "FFmpeg source folder not found:"
  echo "  $FFMPEG"
fi

echo

if [ "$ok" = "1" ]; then
  echo "SDK check OK."
else
  echo "SDK check failed."
  exit 1
fi

echo
echo "Looking for nvtegra symbols in installed libs..."
aarch64-none-elf-nm "$SDK/lib/libavcodec.a" 2>/dev/null | grep -Ei 'nvtegra|h264_nvtegra|hevc_nvtegra|vp8_nvtegra|vp9_nvtegra|mjpeg_nvtegra' | head -60 || true
aarch64-none-elf-nm "$SDK/lib/libavutil.a" 2>/dev/null | grep -Ei 'nvtegra|hwcontext_nvtegra' | head -60 || true