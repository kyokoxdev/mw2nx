#!/usr/bin/env bash
# Builds the ReXGlue codegen (rexglue) and xexdump on the Linux (x86_64 / WSL2) host.
#
# Needs: clang >= 20 with lld, CMake, Ninja, Python, git. CLANG_DIR points at a clang bin folder
# (default ~/tools/clang/usr/lib/llvm-21/bin, a user-space extract of Ubuntu's clang-21/lld-21 debs).
# JOBS defaults to 3 (WSL box with 3 cores / 9 GB).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/sdk"
CLANG_DIR="${CLANG_DIR:-$HOME/tools/clang/usr/lib/llvm-21/bin}"
export PATH="$CLANG_DIR:$PATH"
# X11/Wayland dev files the SDK's rexui wants, when they are not installed system-wide: DEVROOT is a dpkg -x of
# Ubuntu's libx11-xcb-dev, libwayland-dev and their deps, with each .pc prefix rewritten to point inside it.
DEVROOT="${DEVROOT:-$HOME/tools/devroot}"
if [ -d "$DEVROOT" ]; then
  export PKG_CONFIG_PATH="$DEVROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$DEVROOT/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  # rexui links pkg-config's library names without its -L, so the folder goes to the linker here.
  DEVROOT_LDFLAGS="-L$DEVROOT/usr/lib/x86_64-linux-gnu -Wl,-rpath-link,$DEVROOT/usr/lib/x86_64-linux-gnu"
fi

# Always: resumable and cached in out/rexglue-sdk-src, and a partial earlier copy must be completed.
(cd "$ROOT" && python3 tools/fetch_thirdparty.py)

# The SDK wants a .git entry in each vendored submodule.
for d in "$SDK"/thirdparty/*/; do [ -e "$d.git" ] || mkdir "$d.git"; done

cmake -S "$SDK" -B "$ROOT/out/host-linux" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld ${DEVROOT_LDFLAGS:-}" "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld ${DEVROOT_LDFLAGS:-}" \
  -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
  -DGIT_EXECUTABLE=/nonexistent/git \
  "-DCMAKE_PROJECT_ReXGlue_INCLUDE=$ROOT/cmake/host_tools_inject.cmake"

cmake --build "$ROOT/out/host-linux" --target rexglue xexdump -j "${JOBS:-3}"

find "$ROOT/out/host-linux" "$SDK/out" -maxdepth 4 -type f \( -name rexglue -o -name xexdump \) 2>/dev/null
