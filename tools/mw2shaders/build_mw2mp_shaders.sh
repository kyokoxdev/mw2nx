#!/usr/bin/env bash
# Builds mw2mp_shaders.spvpack (+ .spvidx), the native renderer's shader library, from the multiplayer's fastfiles:
#   1. inflate each zone (a signed fastfile interleaves 8 KB of hashes every 2 MB of its zlib stream);
#   2. zone_containers: every shader as container + physical part (mw2-recompiled's zone walker);
#   3. mw2_hlsl (tools/xenosrecomp): XenosRecomp to HLSL; 4. dxc to SPIR-V, spirv-val; 5. mw2_pack.
# Game-derived output: never commit it. Copy both files next to mw2mp.nro.
# MW2_SP=1: the campaign's library instead, mw2sp_shaders.spvpack (+ .spvidx) from every zone that is not the
# multiplayer's (mw2sp/src/mw2sp_game_remap.h names it); same folder.
#
# Usage: tools/mw2shaders/build_mw2mp_shaders.sh <work folder> [sd folder with game/ and update/]
# Env: DXC (default ~/tools/dxc/bin/dxc), SPIRV_VAL (spirv-val on PATH or ~/tools/clang/usr/bin), MW2_RECOMPILED,
#      JOBS (dxc in parallel, default nproc).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$(mkdir -p "${1:?usage: build_mw2mp_shaders.sh <work folder> [sd folder]}" && cd "$1" && pwd)"
SD="${2:-$ROOT/data/sd/switch/mw2mp}"
DXC="${DXC:-$HOME/tools/dxc/bin/dxc}"
SPIRV_VAL="${SPIRV_VAL:-$(command -v spirv-val || echo "$HOME/tools/clang/usr/bin/spirv-val")}"
JOBS="${JOBS:-$(nproc)}"
export PATH="$HOME/tools/clang/usr/lib/llvm-21/bin:$PATH"
for t in "$DXC" "$SPIRV_VAL"; do [ -x "$t" ] || { echo "missing $t" >&2; exit 1; }; done

bash "$ROOT/tools/xenosrecomp/build_tools.sh" "$WORK/tools"
bash "$HERE/build_zone_containers.sh" "$WORK/tools"

# The multiplayer's zones: shared ones, every mp_ map, and the update's. The campaign's: every other zone on the disc
# (code_post_gfx, common, ui, the levels, Special Ops), none of the update's (they are all multiplayer).
if [ "${MW2_SP:-0}" = "1" ]; then
  PACK=mw2sp_shaders
  ZONES=()
  for f in "$SD"/game/*.ff; do case "$(basename "$f")" in *_mp.ff|mp_*) ;; *) ZONES+=("$f") ;; esac; done
else
  PACK=mw2mp_shaders
  ZONES=("$SD"/game/{code_post_gfx_mp,common_mp,ui_mp}.ff "$SD"/game/mp_*.ff "$SD"/update/*.ff)
fi
rm -rf "$WORK/zones" "$WORK/containers" "$WORK/hlsl" "$WORK/spirv"
mkdir -p "$WORK/zones" "$WORK/spirv"
python3 -I - "$WORK/zones" "${ZONES[@]}" <<'EOF'
import os, struct, sys, zlib
out = sys.argv[1]
for path in sys.argv[2:]:
    data = open(path, "rb").read()
    if data[:8] not in (b"IWffu100", b"IWff0100"):
        sys.exit(f"{path}: not a 360 fastfile")
    body = 0x1D + struct.unpack_from(">I", data, 0x19)[0] * 12 + 8   # header, picture table, two sizes
    if data[body:body + 8] == b"IWffs100":                           # signed: 8 KB header, then hashes + 2 MB
        parts, at = [], body + 0x2000
        while at + 0x2000 < len(data):
            at += 0x2000
            parts.append(data[at:at + 0x200000])
            at += 0x200000
        stream = b"".join(parts)
    else:
        stream = data[body:]
    d = zlib.decompressobj()
    zone = d.decompress(stream)
    if not d.eof:
        sys.exit(f"{path}: the zone does not decompress to its end")
    open(os.path.join(out, os.path.basename(path)[:-3] + ".zone"), "wb").write(zone)
EOF
"$WORK/tools/zone_containers" "$WORK/containers" "$WORK"/zones/*.zone | tail -1
"$WORK/tools/mw2_hlsl.exe" "$WORK/containers" "$WORK/hlsl" "$ROOT/tools/xenosrecomp/translator/shader_common.h" | tail -1

# dxc: a shader that fails is left out of the pack and listed.
export DXC SPIRV_VAL WORK
find "$WORK/hlsl" -name '*.hlsl' -printf '%f\n' | xargs -P "$JOBS" -I{} bash -c '
  f={}; b=${f%.hlsl}
  case $b in v_*) set -- -T vs_6_6 -fvk-invert-y ;; *) set -- -T ps_6_6 ;; esac
  if "$DXC" -spirv "$@" -E main -HV 2021 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout \
       -Fo "$WORK/spirv/$b.spv" "$WORK/hlsl/$f" > "$WORK/spirv/$b.log" 2>&1 &&
     "$SPIRV_VAL" --target-env vulkan1.2 --scalar-block-layout "$WORK/spirv/$b.spv" >> "$WORK/spirv/$b.log" 2>&1; then
    rm -f "$WORK/spirv/$b.log"
  else
    rm -f "$WORK/spirv/$b.spv"; echo "$b"
  fi' > "$WORK/compile_failed.txt" || true
echo "dxc: $(find "$WORK/spirv" -name '*.spv' | wc -l) compiled, $(wc -l < "$WORK/compile_failed.txt") failed (compile_failed.txt)"

rm -f "$WORK/$PACK.spvpack" "$WORK/$PACK.spvidx"
"$WORK/tools/mw2_pack.exe" "$WORK/containers" "$WORK/spirv" "$WORK/$PACK.spvpack" > "$WORK/pack.log"
"$WORK/tools/mw2_pack.exe" --index "$WORK/$PACK.spvpack" "$WORK/$PACK.spvidx" >> "$WORK/pack.log"
tail -2 "$WORK/pack.log"
ls -la "$WORK/$PACK.spvpack" "$WORK/$PACK.spvidx"
