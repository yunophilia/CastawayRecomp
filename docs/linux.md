# Linux

The Linux build uses the same runtime as Windows. Direct3D 11 runs on Vulkan through
[DXVK-native](https://github.com/doitsujin/dxvk), shaders compile with
[vkd3d](https://gitlab.winehq.org/wine/vkd3d)'s `D3DCompile`, and SDL3 handles the window,
input and audio. It was developed and tested on Ubuntu 26.04 under WSL (clang 21, CMake 4.2,
SDL 3.4.2).

## Building

1. System packages (Ubuntu):

   ```
   sudo apt-get install clang lld cmake ninja-build python3 git curl meson glslang-tools \
       libvulkan-dev spirv-headers bison flex autoconf automake libtool pkg-config libsdl3-dev
   ```

2. DXVK-native 3.1.1 and vkd3d 2.1, built into `~/castaway-deps/prefix`:
   `sh tools/linux_deps.sh`. Use another location by configuring with
   `-DCASTAWAY_DEPS=<prefix>`.
3. Install the game data and generate the sources as on Windows (README, steps 2 and 3). The
   generated sources are the same on both platforms.
4. `cmake --preset linux-clang && ninja -C build/linux-clang`. The game is
   `out/linux/CastawayRecomp`. It finds DXVK and vkd3d in `lib/` next to the binary, or in the
   build prefix.

Linux builds put their binary, `config.ini`, shader cache and saves in `out/linux/`, so they
don't mix with the Windows build in `out/`.

## Testing without a display

With Xvfb and Mesa's lavapipe (CPU Vulkan; `sudo apt-get install xvfb mesa-vulkan-drivers`),
the scripted tests run headless:

```
cd out/linux
DXVK_WSI_DRIVER=SDL3 SDL_VIDEO_DRIVER=x11 SDL_AUDIO_DRIVER=dummy \
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
xvfb-run -a -s "-screen 0 1280x720x24" ./CastawayRecomp --nobox --skip-launcher --mute \
    --scale 1 --data ../../game/R42E69 --nand /tmp/castaway_nand \
    --script ../../tools/scripts/beach_move.txt
```

lavapipe presents roughly 40 frames per second at scale 1. Scripted runs lock guest time to
presented frames (`Timebase_SetFrameLock`), so a script reaches the same screens on a slow
renderer as on a fast one, just more slowly.

## Porting notes

- **vkd3d's calling convention.** vkd3d builds its `WINAPI` functions and COM methods with
  `ms_abi`, while DXVK-native's headers, which the runtime uses, declare everything with the
  System V convention. Calling vkd3d's `D3DCompile` directly scrambles the arguments.
  `runtime/gx/d3dcompiler_posix.cpp` calls it with the right convention and copies the
  results into blobs with DXVK's ABI. CMake renames `D3DCompile` and `D3DCreateBlob` to that
  bridge for the runtime and ImGui.
- **Platform files.** `*_win32.cpp` builds only on Windows and `*_posix.cpp` only elsewhere:
  - fibers: ucontext
  - guest memory: `memfd_create` + `MAP_FIXED`
  - crash handler: signals
  - D3DCompile bridge
  `runtime/kernel/host.h` holds the small OS services both share. `runtime/compat.h` (forced
  include) supplies the MSVC C runtime functions the code uses.
- **HD texture packs and texture dumps** read and write PNG with SDL (`SDL_LoadPNG`,
  `SDL_SavePNG`) on every platform. Besides `textures/` next to the game, Dolphin's user
  folders are searched (`$XDG_DATA_HOME/dolphin-emu`, `~/.local/share/dolphin-emu`,
  `~/.dolphin-emu`, each with `Load/Textures`).
