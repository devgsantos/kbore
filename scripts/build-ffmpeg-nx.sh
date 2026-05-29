#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"
PREFIX="$ROOT/external/player-sdk"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="$DEVKITPRO/devkitA64"
export PATH="$DEVKITA64/bin:$PATH"

cd "$FFMPEG"

make distclean || true

./configure \
  --prefix="$PREFIX" \
  --target-os=none \
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
  --disable-programs \
  --disable-doc \
  --disable-debug \
  --disable-avdevice \
  --disable-postproc \
  --enable-avformat \
  --enable-avcodec \
  --enable-avutil \
  --enable-swresample \
  --enable-swscale \
  --enable-network \
  --enable-pthreads \
  --enable-protocol=http,https,tcp,tls,udp,file,crypto \
  --enable-demuxer=hls,mpegts,mov,matroska,aac,mp3 \
  --enable-parser=h264,hevc,aac,mpeg4video,mpegaudio,vp8,vp9 \
  --enable-decoder=h264,hevc,aac,mp3,mpeg2video,mpeg4,vp8,vp9 \
  --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc \
  --extra-cflags="-I$DEVKITPRO/libnx/include -I$DEVKITPRO/portlibs/switch/include -O3 -DNX" \
  --extra-ldflags="-L$DEVKITPRO/libnx/lib -L$DEVKITPRO/portlibs/switch/lib" \
  --extra-libs="-lnx -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lbz2 -llzma"

make -j"$(nproc)"
make install