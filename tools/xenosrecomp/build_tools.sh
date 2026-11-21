#!/usr/bin/env bash
# Build the offline shader translator and packer into a separate host-tool folder.
# Usage: tools/xenosrecomp/build_tools.sh <output folder>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:?usage: tools/xenosrecomp/build_tools.sh <output folder>}"
. "$ROOT/tools/toolchain.sh"
mkdir -p "$OUT"
TR="$HERE/translator"
TP="$ROOT/sdk/thirdparty"
RENDER="$ROOT/mw2mp/src/native/render"
clang++ -std=c++23 -O1 -w \
  "-I$TR" "-I$TP/fmt/include" "-I$TP/xxHash" \
  -include "$TR/pch_min.h" -include "$HERE/assert_throw.h" \
  -DFMT_HEADER_ONLY -DXXH_INLINE_ALL -DMW2_RECOMP -D_CRT_SECURE_NO_WARNINGS \
  "$HERE/mw2_hlsl.cpp" "$TR/shader_recompiler.cpp" -o "$OUT/mw2_hlsl.exe"
clang++ -std=c++23 -O2 "-I$RENDER" "-I$TP/xxHash" \
  "$HERE/mw2_pack.cpp" "$RENDER/mw2mp_shader_library.cpp" \
  "$RENDER/mw2mp_shader_index.cpp" -o "$OUT/mw2_pack.exe"
echo "shader tools: $OUT"
