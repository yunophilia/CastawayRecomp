# Progress log

## Milestones

- [x] Fork of BlackKnightRecomp's recompiler and runtime, retargeted to R42E69 (without its
      input code); function names and starts from the disc's linker ELF
- [x] Boot: OS, IOS, DVD, NAND, DSP/AX, KPAD, safety and Nunchuk screens, EA logo, title
- [x] Main menu, Start New Game, Create a Crew, Create a Sim (occupation, body), saving
- [x] Story intro (boat cutscene, shipwreck), the island, walking and swimming with the stick
- [x] 60 fps in menus and gameplay, audio output (not yet checked by ear)
- [x] Input: gamepads (SDL3), keyboard, mouse pointer, scripted input for tests
- [x] Random-input soak, 12 minutes from the beach (tools/scripts/gen.py soak): no crash;
      harvesting bamboo, the pause menu, Plans & Ideas, the island map dialog, tutorial tips
- [x] Actions: Z harvests bamboo; with two actions (coconut: Put in Inventory / Eat) the
      D-pad picks one instead of tilting the Nunchuk, Z does it (gen.py actions, actions_choice)
- [ ] The HUD buttons, the inventory, building
- [x] Continue Game from a save (/dev/fs ReadDir of a file is EINVAL, not ENOENT: the game
      checks for its save that way)
- [x] Gamepad-only play: the right stick moves a virtual pointer (the mouse takes over when it
      moves); main menu verified (tools/scripts/gen.py menu_stick)
- [x] Remappable buttons: config.ini [Gamepad] / [Keyboard] and the Input settings tab;
      launcher text and controls for this game
- [ ] Linux build

## Porting notes (R42E69)

* `files/tsc_wii_release_dvd.elf` is the game's linker output: all main.dol sections are
  byte-identical, with the full symbol table, DWARF and relocations. `tools/elf.py` reads it;
  `tools/syms.py`, `tools/callers.py` (callers from `.rela.text`), `tools/dis.py` and
  `tools/addr2sym.py` help reading the code.
* NTSC: the boot TV mode (0x800000CC) and the VI format follow the release (the game panics in
  VIConfigure when they disagree with its render mode).
* Frame pacing: `ENgcRenderer::EndFrame` arms a GX FIFO breakpoint; the CP breakpoint
  interrupt (`BreakPtCallback`) sets `s_frameAvailable`, and the VI pre-retrace callback then
  releases `s_copyMutex`. The CP emulation raises it once the GPU thread has executed the
  command stream up to the breakpoint address (gx/gx_fifo.cpp).
* Paletted textures are cached per palette: the game binds the same C8 data with different
  palettes on two texture maps in one draw.

## Controllers

* `EControllerManager::_ctrlPads[4]`: slots 0-2 Wii Remotes (EWiiController), slot 3 a
  GameCube controller (ENgcController) that `EWiiControllerManager::Init/Update` also run.
* The game only half wires the GameCube controller: `MapControllerCommands` sets the button
  -> command map on `_ctrlPads[0]` only, `ENgcController::GetButtons` has no codes for C, Z
  and 2, and only Wii Remotes get a cursor. runtime/input/pad_hle.cpp completes it.
* Button codes (low half of an EController mask): A 0x40, B 0x10, C 0x200, Z 0x400, 1 0x2,
  2 0x100, + 0x800, - 0x80, up 0x1000, down 0x4000, left 0x8000, right 0x2000; GameCube L/R
  0x4/0x8. The high half holds stick directions (main 0x1..0x8, C-stick 0x10..0x80, << 16).
* EController data buffers: +508 filled this frame, +512 read; in each, +0 buttons, +4
  previous buttons, +108/+112 cursor. The command map pointer/count are at +420/+424.
* The player: `TryAutoMapPlayer` gives it to the first controller that presses command 5;
  `MapPlayerToController` is overridden to always pick slot 3. The Flash UI (AptViewer) reads
  the player's controller once mapped, else the first active one.
* Pointer: on the main menu the cursor lands at screen fraction u = 0.488 + 0.360 x,
  v = 0.462 + 0.462 y for game pointer coordinates (x, y). EWiiController reports
  (-1000, -1000) when the remote points away. While a pointer is on screen, dialogs act on
  the pointed button and ignore A for the highlighted one, so an idle pointer (8 s without
  mouse or right-stick use) counts as pointed away. On the GameCube slot "away" means not
  writing the cursor at all; writing -1000 there also breaks A in dialogs.
* The safety screens, the Nunchuk notice and the title read Wii Remotes directly, so the
  virtual remote carries the same buttons.
