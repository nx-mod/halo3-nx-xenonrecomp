#!/usr/bin/env bash
# The Switch build's FFmpeg: what tools/build_ffmpeg.sh builds (FFmpeg 7.1
# with the xmaframes decoder, libavcodec + libavutil only), cross-compiled
# with devkitA64. Installs to build/ffmpeg-switch/, which switch/CMakeLists.txt
# links.
#
# Usage: tools/build_ffmpeg_switch.sh   (needs DEVKITPRO, default /opt/devkitpro)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
VERSION="7.1"
SHA256="40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6"
WORK="$ROOT/build/ffmpeg-src"
PREFIX="$ROOT/build/ffmpeg-switch"
TARBALL="$WORK/ffmpeg-$VERSION.tar.xz"
JOBS="${JOBS:-2}"

mkdir -p "$WORK"
if [[ ! -f "$TARBALL" ]]; then
    echo "== Downloading FFmpeg $VERSION"
    curl -fL -o "$TARBALL.part" "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
    mv "$TARBALL.part" "$TARBALL"
fi
echo "$SHA256  $TARBALL" | sha256sum -c --quiet

SRC="$WORK/ffmpeg-$VERSION-switch"
rm -rf "$SRC"
mkdir -p "$SRC"
tar -xf "$TARBALL" -C "$SRC" --strip-components=1
patch -d "$SRC" -p1 --quiet < "$ROOT/tools/patches/ffmpeg-$VERSION-xmaframes.patch"

echo "== Configuring"
ARCH_FLAGS="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__"
(cd "$SRC" && ./configure --prefix="$PREFIX" \
    --enable-cross-compile --cross-prefix="$DEVKITPRO/devkitA64/bin/aarch64-none-elf-" \
    --arch=aarch64 --target-os=none \
    --extra-cflags="$ARCH_FLAGS -I$DEVKITPRO/libnx/include" \
    --disable-everything --enable-decoder=xmaframes \
    --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample \
    --disable-pthreads --disable-w32threads --disable-os2threads \
    --enable-static --disable-shared --enable-pic >/dev/null)

echo "== Building"
make -C "$SRC" -j"$JOBS" >/dev/null
make -C "$SRC" install >/dev/null
echo "Done: $PREFIX"
