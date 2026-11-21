#!/usr/bin/env bash
# Builds the Mesa/NVK Vulkan driver for the Switch on the Linux host (x86_64 / WSL2, no sudo)
# (static libvulkan.a SDK) from danfromtico/mesa-switch at 1a8c1a66d6f plus patches/mesa-mw2nx.patch.
# mesa-switch's own build-unified.sh only runs under MSYS2 and its Linux path needs Docker and root, so this follows
# the same two steps by hand: native host tools (mesa_clc, vtn_bindgen2), then the Horizon cross build. Vulkan only.
#
# Needs:
#   - devkitA64 + libnx + switch-zlib + switch-libexpat + switch-zstd in DEVKITPRO (default ~/devkitpro)
#   - HOST_ROOT (default ~/tools/clang): a dpkg -x of Ubuntu's clang-21 set plus llvm-21-dev, libclang-21-dev,
#     libclang1-21, libclang-cpp21-dev, libclc-21(-dev), libllvmspirvlib-21-dev, libllvmspirvlib21.1, spirv-tools-dev,
#     spirv-tools-headers, spirv-headers
#   - meson >= 1.4, ninja, gcc/g++, flex, bison, pkg-config, python3 + mako/yaml/packaging, git
#   - rustup with target aarch64-unknown-linux-gnu; cargo install bindgen-cli cbindgen
#
# Run: tools/build_mesa_linux.sh   (writes $MESA_DIR/mesa-unified-install/opt/devkitpro/portlibs/switch, the MESA_SDK
# that tools/toolchain.sh expects; MESA_DIR defaults to mesa/ in the data folder, tools/data_dir.sh)
# --resume: rerun ninja + install without reconfiguring.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MESA="${MESA_DIR:-$(bash "$ROOT/tools/data_dir.sh")/mesa}"
PATCH="$ROOT/patches/mesa-mw2nx.patch"
export DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"
HOST_ROOT="${HOST_ROOT:-$HOME/tools/clang}"
JOBS="${JOBS:-3}"
HOST_LIB="$HOST_ROOT/usr/lib/x86_64-linux-gnu"

if [ ! -d "$MESA" ]; then
  git clone --filter=blob:none --no-checkout https://github.com/danfromtico/mesa-switch.git "$MESA"
  git -C "$MESA" checkout 1a8c1a66d6f
fi
if git -C "$MESA" apply --check "$PATCH" 2>/dev/null; then
  git -C "$MESA" apply "$PATCH"
elif ! git -C "$MESA" apply --check -R "$PATCH" 2>/dev/null; then
  echo "patches/mesa-mw2nx.patch neither applies nor is already applied to $MESA" >&2
  exit 1
fi

# Host LLVM/clang/libclc/SPIR-V from HOST_ROOT. The extracted .pc files say prefix=/usr; point them inside HOST_ROOT.
sed -i "s|^prefix=/usr\$|prefix=$HOST_ROOT/usr|; s|^libexecdir=/usr/|libexecdir=$HOST_ROOT/usr/|" \
  "$HOST_LIB/pkgconfig/"{LLVMSPIRVLib,SPIRV-Tools}.pc "$HOST_ROOT/usr/lib/pkgconfig/libclc.pc" \
  "$HOST_ROOT/usr/share/pkgconfig/SPIRV-Headers.pc"
export PATH="$HOST_ROOT/usr/lib/llvm-21/bin:$HOME/.cargo/bin:$PATH"
export LD_LIBRARY_PATH="$HOST_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBCLANG_PATH="$HOST_ROOT/usr/lib/llvm-21/lib"  # bindgen

GEN="$MESA/build/linux-gen"
mkdir -p "$GEN"

# Cross file: mesa-switch's Linux one with /opt/devkitpro moved to DEVKITPRO and the wrappers taken from the checkout.
# portlibs .pc files hard-code /opt/devkitpro, so pkg-config output is rewritten too (as the MSYS2 wrapper does).
cat > "$GEN/pkg-config" <<EOF
#!/usr/bin/env bash
set -o pipefail  # else sed's status hides "not found" and Meson takes every lookup as a hit
export PKG_CONFIG_DIR= PKG_CONFIG_PATH= PKG_CONFIG_SYSROOT_DIR= PKG_CONFIG_LIBDIR="$DEVKITPRO/portlibs/switch/lib/pkgconfig"
[ "\${1:-}" = --version ] && exec /usr/bin/pkg-config --version
/usr/bin/pkg-config --static "\$@" | sed 's|/opt/devkitpro|$DEVKITPRO|g'
EOF
chmod +x "$GEN/pkg-config"
# The wrapper builds its native probe only for the probe names of older Meson; 1.10's is meson-private/sanity.rs.
cat > "$GEN/rustc" <<EOF
#!/usr/bin/env bash
case "\$*" in *meson-private/sanity.rs*) export MESA_SWITCH_RUST_TARGET= ;; esac
exec bash "$MESA/rustc-switch-wrapper.sh" "\$@"
EOF
chmod +x "$GEN/rustc"
sed -e "s|/opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-pkg-config|$GEN/pkg-config|" \
    -e "s|/opt/devkitpro|$DEVKITPRO|g" \
    -e "s|'/usr/local/libexec/rustc'|'$GEN/rustc'|" \
    -e "s|'/usr/local/libexec/bindgen'|['sh', '$MESA/bindgen-switch-wrapper.sh']|" \
    "$MESA/switch_cross_file.txt" > "$GEN/cross.txt"
HOST_DIR="$MESA/builddir-native-linux-host"
cat > "$GEN/native.txt" <<EOF
[binaries]
mesa_clc = '$HOST_DIR/src/compiler/clc/mesa_clc'
vtn_bindgen2 = '$HOST_DIR/src/compiler/spirv/vtn_bindgen2'
cbindgen = '$HOME/.cargo/bin/cbindgen'
EOF

export MESA_SWITCH_RUST_TARGET=aarch64-unknown-linux-gnu
export MESA_SWITCH_RUSTC="$HOME/.cargo/bin/rustc"
export MESA_SWITCH_BINDGEN="$HOME/.cargo/bin/bindgen"
export PKG_CONFIG_PATH="$HOST_LIB/pkgconfig:$HOST_ROOT/usr/lib/pkgconfig:$HOST_ROOT/usr/share/pkgconfig"
export SOURCE_DATE_EPOCH="$(git -C "$MESA" show -s --format=%ct HEAD)"

cd "$MESA"
BUILD="$MESA/builddir-linux-switch"
DEST="$MESA/mesa-unified-install"

if [ "${1:-}" != "--resume" ]; then
  # 1. Host tools. LIBRARY_PATH: LLVMSPIRVLib.pc names lib/, the library is in lib/x86_64-linux-gnu.
  if [ ! -x "$HOST_DIR/src/compiler/clc/mesa_clc" ] || [ ! -x "$HOST_DIR/src/compiler/spirv/vtn_bindgen2" ]; then
    wipe=(); [ -f "$HOST_DIR/meson-private/coredata.dat" ] && wipe=(--wipe)
    LIBRARY_PATH="$HOST_LIB" CC=gcc CXX=g++ meson setup "$HOST_DIR" "${wipe[@]}" \
      --buildtype=release \
      -Dvulkan-drivers= -Dgallium-drivers= -Dshader-cache=disabled -Dplatforms= \
      -Dglx=disabled -Degl=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
      -Dtools=[] -Dllvm=enabled -Dmesa-clc=enabled -Dprecomp-compiler=enabled -Dinstall-mesa-clc=true
    LIBRARY_PATH="$HOST_LIB" ninja -C "$HOST_DIR" -j "$JOBS" src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2
  fi

  # 2. Switch cross build: build-unified.sh's options, minus GL/EGL/gallium (only libvulkan.a is linked).
  wipe=(); [ -f "$BUILD/meson-private/coredata.dat" ] && wipe=(--wipe)
  meson setup "$BUILD" "${wipe[@]}" \
    --cross-file "$GEN/cross.txt" --native-file "$GEN/native.txt" \
    --default-library=static --prefix=/opt/devkitpro/portlibs/switch --libdir=lib \
    --buildtype=release -Doptimization=2 -Db_lto=false -Db_ndebug=true \
    -Dvulkan-drivers=nouveau -Dgallium-drivers= -Dgallium-rusticl=false -Dplatforms=switch \
    -Dglx=disabled -Degl=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
    -Dvideo-codecs= -Dshader-cache=enabled -Dxmlconfig=enabled -Dexpat=enabled -Dtools=[] \
    -Dllvm=disabled -Dshared-glapi=disabled -Dshared-llvm=disabled \
    -Dmesa-clc=system -Dprecomp-compiler=system -Dcpp_rtti=false -Dbuild-tests=false
fi

ninja -C "$BUILD" -j "$JOBS"
rm -rf -- "$DEST"
meson install -C "$BUILD" --destdir "$DEST"

SDK="$DEST/opt/devkitpro/portlibs/switch"
for pc in "$SDK"/lib/pkgconfig/*.pc; do sed -i 's|^prefix=.*|prefix=${pcfiledir}/../..|' "$pc"; done
[ -f "$SDK/lib/libvulkan.a" ] || { echo "no lib/libvulkan.a under $SDK" >&2; exit 1; }
echo
echo "Vulkan SDK: $SDK"
ls -la "$SDK/lib/libvulkan.a"
