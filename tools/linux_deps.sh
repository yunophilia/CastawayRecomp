#!/bin/sh
# Builds the Linux graphics dependencies into ~/castaway-deps/prefix (CMake's CASTAWAY_DEPS):
# DXVK-native (Direct3D 11 on Vulkan, SDL3 window integration) and vkd3d (vkd3d-utils
# provides D3DCompile). See docs/linux.md for the system packages this needs first.
set -e
D=${CASTAWAY_DEPS_DIR:-$HOME/castaway-deps}
mkdir -p "$D" && cd "$D"

[ -d dxvk ] || git clone -q --recursive --depth 1 --branch v3.1.1 https://github.com/doitsujin/dxvk.git dxvk
cd dxvk
[ -d build ] || meson setup build --buildtype=release --prefix="$D/prefix" \
    -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled
ninja -C build install

cd "$D"
[ -f vkd3d-2.1.tar.xz ] || curl -sSLO https://dl.winehq.org/vkd3d/source/vkd3d-2.1.tar.xz
[ -d vkd3d-2.1 ] || tar xf vkd3d-2.1.tar.xz
cd vkd3d-2.1
[ -f Makefile ] || ./configure --prefix="$D/prefix" --disable-tests --without-ncurses --without-xcb
make -j"$(nproc)"
make install
echo "installed to $D/prefix"
