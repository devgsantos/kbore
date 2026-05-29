#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"
PATCH_DIR="$ROOT/patches/ffmpeg-nvtegra"

if [ ! -d "$FFMPEG" ]; then
  echo "ERROR: FFmpeg folder not found:"
  echo "  $FFMPEG"
  return 1 2>/dev/null || true
fi

if [ ! -d "$PATCH_DIR" ]; then
  echo "ERROR: Patch folder not found:"
  echo "  $PATCH_DIR"
  return 1 2>/dev/null || true
fi

cd "$FFMPEG" || {
  echo "ERROR: could not enter FFmpeg folder"
  return 1 2>/dev/null || true
}

echo "FFmpeg folder:"
pwd
echo

echo "Current git state:"
git status --short
echo

PATCHES="$(find "$PATCH_DIR" -maxdepth 1 -type f -name '*.patch' | sort | tail -n +3)"

if [ -z "$PATCHES" ]; then
  echo "ERROR: no patches found after 0002."
  return 1 2>/dev/null || true
fi

for patch in $PATCHES; do
  echo "============================================================"
  echo "Checking: $patch"
  echo "============================================================"

  git apply --check --verbose "$patch"
  CHECK_RESULT=$?

  if [ "$CHECK_RESULT" -ne 0 ]; then
    echo
    echo "ERROR: patch check failed:"
    echo "  $patch"
    echo
    echo "First 60 lines:"
    head -60 "$patch"
    echo
    echo "Terminal was NOT closed. Fix this patch before continuing."
    break
  fi

  echo
  echo "Applying: $patch"
  git apply --verbose "$patch"
  APPLY_RESULT=$?

  if [ "$APPLY_RESULT" -ne 0 ]; then
    echo
    echo "ERROR: patch apply failed:"
    echo "  $patch"
    echo
    echo "Terminal was NOT closed. Fix this patch before continuing."
    break
  fi

  echo "OK"
  echo
done

echo
echo "Done or stopped on first error."
echo
echo "Current git state:"
git status --short