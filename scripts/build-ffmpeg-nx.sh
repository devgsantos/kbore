#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"
PREFIX="$ROOT/external/player-sdk"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="$DEVKITA64/bin:$PATH"

if [ ! -d "$FFMPEG" ]; then
  echo "ERROR: FFmpeg source not found at: $FFMPEG"
  echo "Run:"
  echo "  cd $ROOT/external"
  echo "  git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg-nx"
  exit 1
fi

if [ ! -d "$DEVKITPRO/libnx" ]; then
  echo "ERROR: libnx not found at $DEVKITPRO/libnx"
  exit 1
fi

mkdir -p "$PREFIX"

cd "$ROOT"

./scripts/patch-ffmpeg-nx-switch.sh

cd "$FFMPEG"

make distclean >/dev/null 2>&1 || true

COMMON_CFLAGS="-march=armv8-a -mtune=cortex-a57 -mtp=soft -O3 -fPIC -fPIE -ffunction-sections -fdata-sections -DNX -D__SWITCH__ -I$DEVKITPRO/libnx/include -I$DEVKITPRO/portlibs/switch/include"
COMMON_LDFLAGS="-fPIE -specs=$DEVKITPRO/libnx/switch.specs -L$DEVKITPRO/libnx/lib -L$DEVKITPRO/portlibs/switch/lib"

./configure \
  --prefix="$PREFIX" \
  --target-os=horizon \
  --arch=aarch64 \
  --cpu=cortex-a57 \
  --cross-prefix=aarch64-none-elf- \
  --enable-cross-compile \
  --cc=aarch64-none-elf-gcc \
  --cxx=aarch64-none-elf-g++ \
  --ar=aarch64-none-elf-ar \
  --ranlib=aarch64-none-elf-ranlib \
  --strip=aarch64-none-elf-strip \
  --enable-static \
  --disable-shared \
  --enable-pic \
  --enable-gpl \
  --enable-nvtegra \
  --disable-programs \
  --disable-doc \
  --disable-debug \
  --disable-avdevice \
  --disable-postproc \
  --disable-iconv \
  --disable-symver \
  --disable-stripping \
  --enable-avformat \
  --enable-avcodec \
  --enable-avutil \
  --enable-swresample \
  --enable-swscale \
  --enable-network \
  --enable-pthreads \
  --enable-protocol=file,http,https,tcp,tls,udp,crypto \
  --enable-demuxer=hls,mpegts,mov,matroska,aac,mp3 \
  --enable-muxer=null \
  --enable-parser=h264,hevc,aac,ac3,mpeg4video,mpegaudio,vp8,vp9 \
  --enable-decoder=h264,hevc,aac,mp3,ac3,eac3,mpeg2video,mpeg4,vp8,vp9 \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc \
  --extra-cflags="$COMMON_CFLAGS" \
  --extra-cxxflags="$COMMON_CFLAGS" \
  --extra-ldflags="$COMMON_LDFLAGS" \
  --extra-libs="-lnx -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lbz2 -llzma"

make -j"$(nproc)"
make install

echo
echo "FFmpeg NX build installed at:"
echo "  $PREFIX"
echo
echo "Libraries:"
ls -lh "$PREFIX/lib" | grep -E 'libav|libsw' || true