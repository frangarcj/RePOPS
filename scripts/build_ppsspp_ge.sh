#!/bin/sh
# Build the optional PPSSPP GE bridge. Does not launch a PSP executable.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
PIN=3c6c7f548b1582956478779952e424e2e3ed8ddd
SOURCE=.tools/ppsspp-ge-source
BUILD=.tools/ppsspp-ge-build
mkdir -p .tools out
if [ ! -d "$SOURCE/.git" ]; then
    git clone --filter=blob:none --no-checkout https://github.com/hrydgard/ppsspp.git "$SOURCE"
    git -C "$SOURCE" checkout --detach "$PIN"
fi
[ "$(git -C "$SOURCE" rev-parse HEAD)" = "$PIN" ] || {
    echo 'Unexpected PPSSPP revision; refusing to change the checkout.' >&2; exit 1;
}
PATCH="$ROOT/src/ppsspp_ge/ppsspp.patch"
if git -C "$SOURCE" apply --reverse --check "$PATCH" 2>/dev/null; then
    : # Already patched.
else
    git -C "$SOURCE" apply --check "$PATCH"
    git -C "$SOURCE" apply "$PATCH"
fi
# Core's PortManager headers include miniupnpc types even with UPnP disabled.
git -C "$SOURCE" submodule update --init --depth 1 --jobs 4 -- \
    ext/armips ext/cpu_features ext/glslang ext/SPIRV-Cross ext/rapidjson \
    ext/rcheevos ext/lua ext/libchdr ext/zstd ext/freetype ext/nanosvg ext/aemu_postoffice ext/OpenXR-SDK \
    ext/miniupnp
cmake -S "$SOURCE" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_INCLUDE= \
    -DHEADLESS_CROSS=ON -DHEADLESS=OFF -DUNITTEST=OFF -DATLAS_TOOL=OFF \
    -DUSE_FFMPEG=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
    -DUSE_SYSTEM_LIBPNG=ON -DUSE_CCACHE=OFF -DARMIPS_USE_STD_FILESYSTEM=ON \
    -DREPOPS_ROOT="$ROOT"
cmake --build "$BUILD" --target repops-ge-smoke repops-native-ge -j "${REPOPS_BUILD_JOBS:-4}"
"$BUILD/repops-ge/repops-ge-smoke"
