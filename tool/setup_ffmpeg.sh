#!/usr/bin/env bash
# Copyright 2026 The surround_encoder authors. MIT license.
#
# Builds a MINIMAL, LGPL-configuration FFmpeg from source (audio-only: AC-3
# encoder, MPEG-TS muxer, file/udp protocols, swresample) for Linux or macOS.
# The result is a few MB instead of the ~60 MB of a full build, and contains
# no video codecs at all.
#
# Usage:
#   ./tool/setup_ffmpeg.sh            # triplet auto-detected for this host
#   ./tool/setup_ffmpeg.sh x64-linux  # or arm64-linux / arm64-osx / x64-darwin
#
# Output lands in native/ffmpeg/<triplet>/ where the plugin's CMake finds it
# automatically. FFmpeg is never committed or redistributed (see NOTICE.md):
# this script builds your own copy from source, which also satisfies the
# LGPL requirement to provide the corresponding source.
#
# Requirements: git, make, a C compiler (gcc/clang), nasm on x86 hosts,
# yasm not required. Set http_proxy/https_proxy if your network needs a
# proxy for GitHub.
set -euo pipefail

FFMPEG_TAG="${FFMPEG_TAG:-n8.1}"
TRIPLET="${1:-}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_ROOT="$REPO_ROOT/native/ffmpeg"
SRC_DIR="$OUTPUT_ROOT/ffmpeg-src"

if [ -z "$TRIPLET" ]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)  TRIPLET="arm64-osx" ;;
    Darwin-*)      TRIPLET="x64-darwin" ;;
    Linux-aarch64) TRIPLET="arm64-linux" ;;
    Linux-*)       TRIPLET="x64-linux" ;;
    *) echo "Unsupported host: $(uname -s) $(uname -m)"; exit 1 ;;
  esac
fi

# --- source ---------------------------------------------------------------
mkdir -p "$OUTPUT_ROOT"
if [ ! -f "$SRC_DIR/RELEASE" ] && [ ! -f "$SRC_DIR/FFMPEG_VERSION" ]; then
  echo "Cloning FFmpeg $FFMPEG_TAG (shallow)..."
  rm -rf "$SRC_DIR"
  git clone --depth 1 --branch "$FFMPEG_TAG" \
    https://github.com/FFmpeg/FFmpeg.git "$SRC_DIR"
fi

# --- configure ------------------------------------------------------------
BUILD_DIR="$SRC_DIR/build-$TRIPLET"
PREFIX="$OUTPUT_ROOT/$TRIPLET"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

case "$TRIPLET" in
  x64-*|i686-*) ASM_FLAGS="" ;;          # nasm available on x86 hosts
  *)            ASM_FLAGS="--disable-x86asm" ;;
esac

echo "Configuring minimal FFmpeg for $TRIPLET -> $PREFIX"
"$SRC_DIR/configure" \
  --prefix="$PREFIX" \
  --enable-shared \
  --disable-static \
  --disable-everything \
  --enable-encoder=ac3 \
  --enable-muxer=mpegts \
  --enable-protocol=file,udp \
  --disable-avdevice \
  --disable-avfilter \
  --disable-swscale \
  --disable-programs \
  --disable-doc \
  --disable-debug \
  --disable-autodetect \
  --enable-small \
  $ASM_FLAGS

# --- build & install ------------------------------------------------------
NPROC=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
make -j"$NPROC"
make install

echo ""
echo "Minimal FFmpeg installed to $PREFIX"
du -sh "$PREFIX/bin" 2>/dev/null || du -sh "$PREFIX/lib"
echo "The plugin's CMake discovers this layout automatically; or set"
echo "SURROUND_ENCODER_FFMPEG_ROOT=$PREFIX explicitly."
