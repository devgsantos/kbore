#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MPV="$ROOT/external/mpv-nx"
PREFIX="$ROOT/external/player-sdk"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="$DEVKITPRO/devkitA64"
export PATH="$DEVKITA64/bin:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$DEVKITPRO/portlibs/switch/lib/pkgconfig"

cd "$MPV"

# Exemplo conceitual; os flags reais dependem da versão do mpv.
meson setup build-switch \
  --cross-file "$ROOT/scripts/switch-meson-cross.txt" \
  --prefix "$PREFIX" \
  -Dlibmpv=true \
  -Dcplayer=false \
  -Dlua=disabled \
  -Djavascript=disabled \
  -Dmanpage-build=disabled \
  -Dtests=false

ninja -C build-switch
ninja -C build-switch install