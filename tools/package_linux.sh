#!/bin/sh
# Collects a playable Linux build into dist/CastawayRecomp-linux (no game data is included).
#   sh tools/package_linux.sh
# Needs patchelf (sudo apt-get install patchelf). DXVK, vkd3d and SDL3 are bundled in lib/,
# found through RUNPATH $ORIGIN/lib; the system needs a Vulkan driver.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
dist="$root/dist/CastawayRecomp-linux"
exe="$root/out/linux/CastawayRecomp"
[ -x "$exe" ] || { echo "build first (cmake --preset linux-clang && ninja -C build/linux-clang)"; exit 1; }
command -v patchelf >/dev/null || { echo "patchelf is needed (sudo apt-get install patchelf)"; exit 1; }

mkdir -p "$dist/lib" "$dist/game" "$dist/shadercache"
for f in CastawayRecomp bkr_install; do
    cp "$root/out/linux/$f" "$dist/$f"
    # debug info holds build paths; RUNPATH only the bundled libraries
    strip --strip-debug "$dist/$f"
    patchelf --set-rpath '$ORIGIN/lib' "$dist/$f"
done
# the libraries the game resolves from the build prefix, plus SDL3 (not on every distribution)
ldd "$exe" | awk '/dxvk|vkd3d|SDL3/ { print $3 }' | while read -r lib; do
    cp -L "$lib" "$dist/lib/$(basename "$lib")"
    strip --strip-debug "$dist/lib/$(basename "$lib")"
    patchelf --set-rpath '$ORIGIN' "$dist/lib/$(basename "$lib")"
done
cp "$root/README.md" "$root/LICENSE" "$dist/"
cp "$root/docs/linux.md" "$dist/LINUX.md"

# Shader cache: the GX states recorded in data/shadercache and out/linux/shadercache,
# compiled here, so a fresh install does not compile on first play.
rm -f "$dist/shadercache/"*.cso
for k in vs_keys.bin ps_keys.bin; do
    cat "$root/data/shadercache/$k" "$root/out/linux/shadercache/$k" "$root/out/shadercache/$k" 2>/dev/null > "$dist/shadercache/$k.new" || true
    mv "$dist/shadercache/$k.new" "$dist/shadercache/$k"
done
if [ -s "$dist/shadercache/vs_keys.bin" ]; then
    "$dist/CastawayRecomp" --precompile-shaders "$dist/shadercache" || { echo "shader precompilation failed"; exit 1; }
else
    echo "no recorded shaders yet (play a while in the development build first)"
fi

# the precompile run's own files
rm -f "$dist/CastawayRecomp.log" "$dist/config.ini"
cat > "$dist/game/ABOUT_THIS_FOLDER.txt" <<'EOF'
You don't need to put anything here. Start ./CastawayRecomp and select your own disc
image (USA; ISO, RVZ, WIA or WBFS) in the file dialog, wherever it is on your computer.
The launcher installs the game data into this folder; the image is not needed afterwards.

Optional, if you have one:
  textures/R42E69/...                       (Dolphin-format HD texture pack; the folder is
                                            named by the game ID)
EOF
echo "packaged to $dist"
