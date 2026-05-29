#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"
PATCH_DIR="$ROOT/patches/ffmpeg-nvtegra"

if [ ! -d "$FFMPEG" ]; then
  echo "ERROR: FFmpeg source folder not found:"
  echo "  $FFMPEG"
  exit 1
fi

if [ ! -d "$PATCH_DIR" ]; then
  echo "ERROR: Patch folder not found:"
  echo "  $PATCH_DIR"
  exit 1
fi

PATCHES="$(find "$PATCH_DIR" -maxdepth 1 -type f -name '*.patch' | sort)"
PATCH_COUNT="$(echo "$PATCHES" | sed '/^$/d' | wc -l)"

if [ "$PATCH_COUNT" -lt 16 ]; then
  echo "ERROR: Expected 16 .patch files in:"
  echo "  $PATCH_DIR"
  echo "Found: $PATCH_COUNT"
  find "$PATCH_DIR" -maxdepth 1 -type f | sort
  exit 1
fi

cd "$FFMPEG"

echo "Current FFmpeg folder:"
pwd
echo

echo "Current git branch:"
git branch --show-current || true
echo

echo "Current git state:"
git status --short
echo

echo "Validating patch files..."
for patch in $PATCHES; do
  echo "Checking file: $patch"

  if grep -qi "<html\|<!DOCTYPE html\|</pre>\|</body>" "$patch"; then
    echo "ERROR: HTML detected in:"
    echo "  $patch"
    exit 1
  fi

  if ! grep -q "^diff --git " "$patch"; then
    echo "ERROR: No raw diff found in:"
    echo "  $patch"
    echo
    echo "First 30 lines:"
    head -30 "$patch"
    exit 1
  fi
done

echo
echo "Applying nvtegra patch series using git apply..."
echo

for patch in $PATCHES; do
  echo "Applying: $patch"

  echo "Running check..."
  if ! git apply --check --verbose "$patch"; then
    echo
    echo "ERROR: git apply check failed for:"
    echo "  $patch"
    echo
    echo "First 40 lines of patch:"
    head -40 "$patch"
    exit 1
  fi

  git apply --verbose "$patch"

  echo "OK"
  echo
done

echo "nvtegra patch series applied."
echo

echo "Checking for nvtegra markers..."
grep -R "enable-nvtegra\|nvtegra\|AV_HWDEVICE_TYPE_NVTEGRA" configure libavcodec libavutil -n | head -80 || true