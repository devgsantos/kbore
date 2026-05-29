#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"
TCP_FILE="$FFMPEG/libavformat/tcp.c"

if [ ! -f "$TCP_FILE" ]; then
  echo "ERROR: tcp.c not found at:"
  echo "  $TCP_FILE"
  exit 1
fi

if grep -q "NSTV_SWITCH_TCP_MAXSEG_PATCH" "$TCP_FILE"; then
  echo "FFmpeg tcp.c already patched for Switch TCP_MAXSEG."
  exit 0
fi

python3 - <<PY
from pathlib import Path

path = Path("$TCP_FILE")
text = path.read_text()

marker = '#include "network.h"'

patch = '''
/*
 * NSTV_SWITCH_TCP_MAXSEG_PATCH
 *
 * devkitPro/libnx headers may not expose TCP_MAXSEG, but FFmpeg tcp.c
 * references it when compiling TCP support. Define the common TCP_MAXSEG
 * socket option value so the cross-build can compile.
 *
 * The option is only used when tcp_mss is explicitly set.
 */
#ifndef TCP_MAXSEG
#define TCP_MAXSEG 2
#endif
'''

if "NSTV_SWITCH_TCP_MAXSEG_PATCH" in text:
    print("Already patched.")
else:
    if marker not in text:
        raise SystemExit(f"Marker not found in {path}: {marker}")

    text = text.replace(marker, marker + patch, 1)
    path.write_text(text)
    print(f"Patched {path}")
PY