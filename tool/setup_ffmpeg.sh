#!/usr/bin/env bash
# Copyright 2026 The surround_encoder authors. MIT license.
#
# Builds an FFmpeg development layout (include/ + lib/) for Linux or macOS
# using vcpkg, with an LGPL-only configuration.
#
# Usage:
#   ./tool/setup_ffmpeg.sh x64-linux          # or arm64-linux
#   ./tool/setup_ffmpeg.sh x64-darwin         # or arm64-osx
#   ./tool/setup_ffmpeg.sh                    # triplet for the host platform
#
# Output lands in native/ffmpeg/<triplet>/, picked up by the platform
# CMakeLists via SURROUND_ENCODER_FFMPEG_ROOT. FFmpeg is NOT redistributed in
# this repository; every consumer builds their own (see NOTICE.md).
set -euo pipefail

TRIPLET="${1:-}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_ROOT="$REPO_ROOT/native/ffmpeg"

case "$TRIPLET" in
  "") ;;
  *) ;;
esac

# vcpkg location: VCPKG_ROOT env, or install a local copy on first use.
VCPKG_ROOT="${VCPKG_ROOT:-$REPO_ROOT/native/.vcpkg}"
if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
  echo "Cloning vcpkg into $VCPKG_ROOT ..."
  git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
  "$VCPKG_ROOT/bootstrap-vcpkg.sh"
fi
VCPKG="$VCPKG_ROOT/vcpkg"

if [ -z "$TRIPLET" ]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) TRIPLET="arm64-osx" ;;
    Darwin-*)     TRIPLET="x64-darwin" ;;
    Linux-aarch64) TRIPLET="arm64-linux" ;;
    Linux-*)       TRIPLET="x64-linux" ;;
    *) echo "Unsupported host: $(uname -s) $(uname -m)"; exit 1 ;;
  esac
fi

BUILD_DIR="$OUTPUT_ROOT/vcpkg-build-$TRIPLET"
mkdir -p "$BUILD_DIR"

echo "Building FFmpeg for $TRIPLET (this takes a while)..."
(cd "$BUILD_DIR" && "$VCPKG" install \
  --triplet "$TRIPLET" \
  --x-manifest-root="$REPO_ROOT/tool" \
  --x-install-root="$BUILD_DIR/vcpkg_installed")

INSTALLED="$BUILD_DIR/vcpkg_installed/$TRIPLET"
OUT="$OUTPUT_ROOT/$TRIPLET"
mkdir -p "$OUT"
cp -R "$INSTALLED/include" "$OUT/include"
cp -R "$INSTALLED/lib" "$OUT/lib"
[ -d "$INSTALLED/bin" ] && cp -R "$INSTALLED/bin" "$OUT/bin"

echo "FFmpeg for $TRIPLET ready: $OUT"
echo "Set SURROUND_ENCODER_FFMPEG_ROOT=$OUT before building, or keep it under"
echo "native/ffmpeg/<triplet> where the platform CMake finds it."
