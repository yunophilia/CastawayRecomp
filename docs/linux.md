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

## Packaging

`sh tools/package_linux.sh` (needs `patchelf`) collects `dist/CastawayRecomp-linux`:
- the game and `bkr_install`
- DXVK, vkd3d and SDL3 in `lib/`, found through `RUNPATH $ORIGIN/lib`
- a precompiled shader cache

Debug info is stripped, so no build paths ship. The target system needs a Vulkan driver, and
a glibc at least as new as the build machine's.

## Testing without a display

With Xvfb and Mesa's lavapipe (CPU Vulkan; `sudo apt-get install xvfb mesa-vulkan-drivers`),
the scripted tests run headless. `tools/run_test_linux.sh` runs the game from `out/linux`
for at most the given number of seconds:

```
sh tools/run_test_linux.sh 300 --nobox --skip-launcher --mute --scale 1 \
    --data ../../game/R42E69 --nand /tmp/castaway_nand --script ../../tools/scripts/beach_move.txt
```

Wrap the game itself in `timeout`, not `xvfb-run`: killing `xvfb-run` leaves the game
running, in the background, writing the same log.

lavapipe presents roughly 35-40 frames per second at scale 1, while the game keeps its 60 Hz.
Script frames are video fields (game time, `Input_OnField`), so a script reaches the same
screens on a slow renderer as on a fast one.

## Porting notes

- **DXVK's window system**: DXVK-native creates no DXGI factory unless `DXVK_WSI_DRIVER`
  names one. The game sets it to `SDL3` at startup unless the variable is already set.
- **Installing on Linux** was checked end to end: `bkr_install` from the RVZ image gives files
  identical to the Windows install, and `--verify` passes.

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
