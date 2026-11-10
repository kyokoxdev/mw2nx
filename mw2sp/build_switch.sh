#!/usr/bin/env bash
# Builds mw2sp/out/switch/mw2sp.nro with devkitA64, libnx and Mesa's NVK (Linux host).
# Needs the generated code (tools/codegen_mw2sp.sh), devkitPro (DEVKITPRO, default ~/devkitpro) and the Mesa SDK that
# tools/build_mesa_linux.sh builds (MESA_SDK). JOBS: parallel compiles (default 2; each generated file wants ~2-3 GB).
set -euo pipefail
APP="$(cd "$(dirname "$0")" && pwd)"
ROOT="$APP/.."
OUT="$APP/out/switch"
# MW2_PGO (profile-guided optimization, mw2sp/CMakeLists.txt; development only), as mw2mp/build_switch.sh:
#   generate  training build in out/switch-pgo. Run it with mw2mp_benchmark = true in mw2sp.toml, in a level, leave the
#             controller alone; at the end of the window it writes sdmc:/switch/mw2sp/pgo. Copy that folder to
#             MW2_PGO_DIR (default data/pgo_sp), then put the normal NRO back.
#   use       normal build in out/switch with that profile (retrain after codegen or flag changes).
case "${MW2_PGO:-}" in
  generate) OUT="$APP/out/switch-pgo" ;;
  use|"") ;;
  *) echo "MW2_PGO must be generate or use" >&2; exit 1 ;;
esac
PGO_DIR="${MW2_PGO_DIR:-$(bash "$ROOT/tools/data_dir.sh")/pgo_sp}"
if [ "${MW2_PGO:-}" = "use" ] && [ -z "$(find "$PGO_DIR" -name '*.gcda' -print -quit 2>/dev/null)" ]; then
  echo "MW2_PGO=use: no .gcda files in $PGO_DIR" >&2; exit 1
fi
export DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"
MESA_SDK="${MESA_SDK:-$(bash "$ROOT/tools/data_dir.sh")/mesa/mesa-unified-install/opt/devkitpro/portlibs/switch}"
GEN="$APP/generated/default"

# The build depends on tools/codegen_mw2sp.sh's fix-ups: fail early without them.
python3 -I "$ROOT/tools/setjmp_locals.py" --verify --gen "$GEN"
python3 -I "$ROOT/tools/guest_barriers.py" --check --gen "$GEN"
python3 -I "$ROOT/tools/direct_calls.py" --check --gen "$GEN"

# Plain guest loads and stores in the generated code (MW2SP_GUEST_MEMORY_PLAIN; bo2 measured +5.8 % on the console),
# safe with guest_barriers.py's barriers, checked above. MW2_GUEST_MEMORY_PLAIN=0 builds the volatile form.
# MW2_LTO=1: GCC LTO as bo2 (+10 % FPS there); off by default (a long link).
# -O3 as mw2mp.
RELEASE="-O3 -DNDEBUG"
LTO_LINK=""
if [ "${MW2_LTO:-0}" = "1" ]; then
  RELEASE="$RELEASE -flto=${JOBS:-2} -ffat-lto-objects"
  LTO_LINK=" -flto=${JOBS:-2}"
fi
cmake -S "$APP" -B "$OUT" -G Ninja "-DCMAKE_MAKE_PROGRAM=$(command -v ninja)" \
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS_RELEASE=$RELEASE" \
  "-DMW2SP_PGO=${MW2_PGO:-}" "-DMW2SP_PGO_DIR=$PGO_DIR" \
  "-DMW2SP_GUEST_MEMORY_PLAIN=$([ "${MW2_GUEST_MEMORY_PLAIN:-1}" = "1" ] && echo ON || echo OFF)" \
  "-DCMAKE_TOOLCHAIN_FILE=$ROOT/tools/switch/cmake/switch-devkitA64.cmake" \
  "-DREXSDK_DIR=$ROOT/sdk" \
  "-DREXGLUE_SWITCH_NVK_SDK=$MESA_SDK" \
  "-DCMAKE_EXE_LINKER_FLAGS=-specs=$OUT/switch.specs -Wl,--gc-sections -Wl,--allow-multiple-definition -Wl,--no-relax$LTO_LINK"
cmake --build "$OUT" --target mw2sp -j "${JOBS:-2}"
ls -la "$OUT/mw2sp.nro"
