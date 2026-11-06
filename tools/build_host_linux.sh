#!/usr/bin/env bash
# Linux (x86_64 / WSL2) twin of tools/build_host.sh: builds the ReXGlue codegen (rexglue) and xexdump.
#
# Needs: clang >= 20 with lld, CMake, Ninja, Python, git. CLANG_DIR points at a clang bin folder
# (default ~/tools/clang/usr/lib/llvm-21/bin, a user-space extract of Ubuntu's clang-21/lld-21 debs).
# JOBS defaults to 3 (WSL box with 3 cores / 9 GB).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/sdk"
CLANG_DIR="${CLANG_DIR:-$HOME/tools/clang/usr/lib/llvm-21/bin}"
export PATH="$CLANG_DIR:$PATH"

# Always: resumable and cached in out/rexglue-sdk-src, and a partial earlier copy must be completed.
(cd "$ROOT" && python3 tools/fetch_thirdparty.py)

# Same as build_host.sh step 2: the SDK wants a .git entry in each vendored submodule.
for d in "$SDK"/thirdparty/*/; do [ -e "$d.git" ] || mkdir "$d.git"; done

cmake -S "$SDK" -B "$ROOT/out/host-linux" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \
  -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 \
  -DGIT_EXECUTABLE=/nonexistent/git \
  "-DCMAKE_PROJECT_ReXGlue_INCLUDE=$ROOT/cmake/host_tools_inject.cmake"

cmake --build "$ROOT/out/host-linux" --target rexglue xexdump -j "${JOBS:-3}"

find "$ROOT/out/host-linux" "$SDK/out" -maxdepth 4 -type f \( -name rexglue -o -name xexdump \) 2>/dev/null
