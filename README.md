# CastawayRecomp

A static recompilation of **The Sims 2: Castaway** (Wii) to native Windows x64 and Linux x64. The game's
PowerPC code is translated to C++ ahead of time and runs on a runtime that stands in for the
Wii hardware and system software. It plays with a gamepad of any brand, or with mouse and
keyboard.

Supported disc: USA (`R42E69`).

**You must own the game.** Nothing from the game is in this repository; the build reads your
own disc image.

## How it works

```
main.dol + tsc_wii_release_dvd.elf ──► tools/analyze.py ──► tools/recomp.py ──► generated/r42e69/*.cpp ─┐
 (PowerPC)  (the game's linker ELF)     function analysis    PPC -> C++ codegen    (28k functions)         ├─► CastawayRecomp.exe
                                                                                    runtime/ ──────────────┘
```

* **Symbols from the disc**: the disc ships `files/tsc_wii_release_dvd.elf`, the linker output
  `main.dol` was made from. Its sections are byte-identical to the DOL, and it keeps the full
  symbol table (27,584 functions), so every function has its real name and start, and the
  runtime refers to game code by symbol (`tools/game_addrs.txt`).
* **Recompiler** (`tools/`): decodes every Gekko/Broadway instruction (including paired singles
  and quantized loads/stores) and emits one C++ function per guest function.
* **Runtime** (`runtime/`): the game's own Revolution OS runs recompiled; threads are host
  fibers. Memory-mapped hardware (VI, PI, CP/PE, DSP/AI, EXI, SI, IPC) and IOS (`/dev/di`
  served from the installed disc files, `/dev/fs` with saves in `nand/`) are emulated at a high
  level. GX is rendered with Direct3D 11 on its own thread (through DXVK on Linux), audio
  goes through an AX HLE to SDL3.
* **Input** (`runtime/input`): see below.

The recompiler and most of the runtime come from
[BlackKnightRecomp](https://github.com/yunophilia/BlackKnightRecomp); the input layer is new.

## Fixes

* **Music from the front** (`[Audio] music_front`, on by default): the Wii version hands its
  music to the mixer with GameCube-era arguments, which puts it in the rear surround channel.
  On stereo it plays in antiphase (hollow, and near silent in mono). The fix plays it from
  the front, like the sound effects. Details in docs/progress.md.

## Controls

The game reads a GameCube controller on port 4 as well as Wii Remotes (a leftover of its
development). That port is completed and drives the game: gamepads and the keyboard play on
it with the Wii Remote and Nunchuk's buttons, and the mouse is the pointer. A virtual Wii
Remote with a Nunchuk is always connected, so the game never asks for one.

| Game (Wii) | Gamepad | Keyboard / mouse |
|---|---|---|
| Pointer | right stick | mouse, or I J K L |
| A | A (south) | Space, left mouse button |
| B | B (east) | Backspace, right mouse button |
| Nunchuk stick | left stick | W A S D |
| Nunchuk C / Z | RB / LB | C / Z, middle mouse button = C |
| 1 / 2 | X (west) / Y (north) | 1 / 2 |
| + / - | Start / Back | Enter / Tab |
| HOME | Guide, or Back + Start | H |
| D-pad | D-pad | arrow keys |
| GameCube L / R | LT / RT | Q / E |

The Wii version has no C-stick, so the right stick (and IJKL) moves a virtual pointer; the
mouse takes over as soon as it moves. After 8 seconds without use the pointer leaves the
screen, like pointing the Wii Remote away: then the D-pad and A work the highlighted buttons,
and the D-pad also picks between actions where the game asks to tilt the Nunchuk. Gamepads are read through SDL3 (Xbox, PlayStation, Switch Pro and generic pads). Rumble goes
to the gamepad.

## Building

Requirements: Windows 10+, Visual Studio 2022 or newer (the Build Tools are enough) with the
C++ workload and the "C++ Clang Compiler for Windows" component, CMake 3.21+ and Ninja,
Python 3.10+. The first configure downloads [nod](https://github.com/encounter/nod) (disc
images), [Dear ImGui](https://github.com/ocornut/imgui) and [SDL3](https://github.com/libsdl-org/SDL).

`tools/dev_build.bat` sets up the Visual Studio environment and builds with the
`windows-clang` preset (clang-cl, lld-link, Ninja).

1. Configure and build the installer (before the game code is generated only the installer is
   built): `tools\dev_build.bat`
2. Install the game data from your disc image: `out\bkr_install <image.iso|.rvz|.wbfs> game\R42E69`
3. Generate the recompiled sources: `python tools/build_generated.py`
4. Build everything: `tools\dev_build.bat`; the game is `out\CastawayRecomp.exe`.

On Linux, see [docs/linux.md](docs/linux.md): DXVK-native and vkd3d provide Direct3D 11 and
the shader compiler, and the build is `cmake --preset linux-clang && ninja -C build/linux-clang`.

## Testing

The end-to-end test is a TAS script ([docs/tas.md](docs/tas.md)): from the first boot it
starts a new game, creates a Sim, plays through the story, sleeps, walks, saves and quits,
and loads the save again. It waits for the game's own state changes and checks values in
game memory (where the Sim is, the clock, sleeping) at each step. It runs on Windows and
headless on Linux, and passes only if every check does:

```
powershell -ExecutionPolicy Bypass -File tools/tas_run.ps1 -Script tools/tas/e2e.tas
sh tools/tas_run_linux.sh tools/tas/e2e.tas
```

`tools/run_test.ps1` runs the game minimized for a fixed time. Input scripts (`--script`)
drive it frame by frame and take screenshots; `tools/scripts/gen.py` builds them, for example
`boot_to_menu`, `cas_accept` (new game, create a Sim), `beach_move` (walk on the island) and
`soak` (ten minutes of seeded random input). Script frames are video fields (game time),
so runs behave the same on fast and slow machines. With the call-counting build
(`tools\dev_build.bat counts`), the script commands `countsreset` and `counts <file>` dump
per-function call counts; `tools/count_diff.py` lists what one action called that another
did not. `tools/contact.py` tiles screenshots into one image.

```
python tools/scripts/gen.py beach_move
powershell -File tools/run_test.ps1 -Seconds 200 -GameArgs "--nobox --skip-launcher --mute --data ..\game\R42E69 --script ..\tools\scripts\beach_move.txt"
```

## License

GPL-2.0-only (see `LICENSE`); the runtime contains code derived from Dolphin.
