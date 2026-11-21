#!/usr/bin/env bash
# Finds the build tools the build scripts share (Linux / WSL2 host), so none of them hard-codes where a tool is
# installed. Source it from a script (". tools/toolchain.sh") after setting ROOT; it only sets variables and PATH.
#
# Each tool can be named with an environment variable; without one, the usual location is tried, then PATH:
#
#   CLANG_DIR    folder with clang and clang++   (default ~/tools/clang/usr/lib/llvm-21/bin when it exists, as
#                tools/build_host_linux.sh)
#   MW2_NINJA    ninja                           (default: ninja on PATH)
#   DEVKITPRO    devkitPro's root                (default ~/devkitpro). The Switch toolchain file reads it.
#   MESA_SDK     the Mesa/NVK SDK for the Switch (default: what tools/build_mesa_linux.sh builds, in mesa/ of the
#                data folder, tools/data_dir.sh)

# LLVM (the host tools, the shader translator).
_mw2_clang="${CLANG_DIR:-$HOME/tools/clang/usr/lib/llvm-21/bin}"
if [ -d "$_mw2_clang" ]; then
  export PATH="$_mw2_clang:$PATH"
fi

# Ninja. The Switch toolchain file limits program lookups to the target sysroot, so the scripts also pass it to CMake
# by name (NINJA_EXE).
NINJA_EXE="${MW2_NINJA:-$(command -v ninja || true)}"

export DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"

_mw2_data="$(bash "$(dirname "${BASH_SOURCE[0]}")/data_dir.sh")"
MESA_SDK="${MESA_SDK:-$_mw2_data/mesa/mesa-unified-install/opt/devkitpro/portlibs/switch}"
unset _mw2_clang _mw2_data
