# TAS scripts (test automation)

Scripted runs (`--tas <script>`) drive the controller and the pointer with input timed by
game time, wait for the game's own state changes and values, and log PASS/FAIL checks.
They never change game code or game data: input goes through the normal controller path,
and everything else only reads guest memory or watches what the game does (disc reads, the
state machines' state changes; `runtime/tas/tas_hooks.cpp`). While a script runs, real
controllers, the keyboard and the mouse are ignored and rumble is off.

The language follows BlackKnightRecomp's TAS scripts, adapted to this game: Wii Remote
buttons, a pointer, state conditions, `or`, variables and loops.

## The end-to-end test

`tools/tas/e2e.tas` plays from the first boot to a reloaded save, about 4 minutes of game
time, and checks each step:

| step | checked by |
|---|---|
| boot to the main menu | state `TheSimsMainMenuState` |
| Start New Game, Create a Crew | state `PreGameCreateAFamilyState` |
| Create a Sim (Ranger), Done, save | the island loads: `InLevelStoryState`, `MUStatesLoadHouse` |
| the Sim on the beach | the selected Sim's tile position becomes valid |
| the story dialogs | the clock stops while the shipwreck dialog is up and runs after A |
| control after the story | the stick moves the Sim |
| the clock | the minute advances |
| HUD: Goals, pause menu | screenshots |
| Inventory, Use the sleeping mat, Sleep on Ground | the Sim falls asleep and wakes up; hours pass |
| walking in four directions | each moves the Sim; no input, no movement |
| Save & Quit, overwrite | state `TheSimsMainMenuState` again |
| Continue Game | the saved game loads (`LiveModeInitState`); the Sim is where it was saved, on the saved day |

```
powershell -ExecutionPolicy Bypass -File tools/tas_run.ps1 -Script tools/tas/e2e.tas
sh tools/tas_run_linux.sh tools/tas/e2e.tas
```

Both start from an empty save folder and print `e2e : OK` and the number of passed checks,
or what failed; the exit code is 0 only when every check passed. A failed `until` stops the
run with a screenshot `FAIL.bmp`. Logs, telemetry (`.csv`, one row per presented frame) and
screenshots go to `out/tas/` (`out/linux/tas/` on Linux). `tas_run.ps1` runs minimized,
muted, at 2x internal resolution and below-normal priority unless `-Full`.

## Script language

One command per line, `#` starts a comment. Commands run in order; input commands take
game time (video fields, 60 per second), so a script reaches the same screens however fast
the machine renders.

| command | effect |
|---|---|
| `wait S` | no input for S seconds |
| `hold S INPUT` | hold INPUT for S seconds |
| `tap INPUT [S]` | hold for S (default 0.1) then release for 0.1 s |
| `mash INPUT S [P]` | press for 0.1 s every P seconds (default 0.2) for S seconds |
| `set INPUT` / `set` | background input added to everything that follows / clear it |
| `point U V` | the pointer at screen fraction (U, V), until changed |
| `nopoint` | the pointer away from the screen (the game hides its interface) |
| `click U V [S]` | `point U V`, wait S (default 0.5), `tap A` |
| `until COND [mash INPUT [P]] [timeout S] [msg TEXT]` | wait until COND holds (default timeout 60 s); a timeout fails the run (exit code 3) |
| `check COND [msg TEXT]` | log PASS or FAIL without waiting; a FAIL makes the exit code 1 |
| `let NAME TYPE EXPR` | remember a value now, for conditions (`$NAME`) |
| `repeat N` ... `end` | loop |
| `breakif COND` | leave the innermost loop |
| `mark` | `file` and `state` conditions only count events after this point (default: the script start) |
| `log TEXT` | log line and telemetry marker |
| `shot NAME` | screenshot to `shots/NAME.bmp` |
| `dump NAME` | MEM1/MEM2 to `dumps/NAME.mem1/.mem2` |
| `calls NAME` / `calls -` | call counts since the last `calls` to `dumps/NAME.calls` / reset (call-counting build: `tools\dev_build.bat counts`, compare with `tools/count_diff.py`) |
| `watch NAME TYPE EXPR` | telemetry column |
| `alias NAME EXPR` | a name for an address expression |
| `include FILE` | include another script (path relative to this one) |
| `quit [CODE]` | exit the game (default code: 1 if a check failed, else 0) |

INPUT: buttons `A B C Z 1 2 PLUS MINUS HOME UP DOWN LEFT RIGHT` (the Wii controls the game's
commands are mapped to) joined with `+`, and analog values `LX= LY=` (movement stick),
`RX= RY=` (right stick, which moves the pointer) and `LT= RT=`, -1..1 (triggers 0..1), e.g.
`hold 2 LY=1+Z`.

COND: one or more conditions joined with `and` and `or` (`A and B or C` is `(A and B) or C`):
- `state CLASS`: a state machine entered the state CLASS (a `StateMachineState` class name,
  e.g. `TheSimsMainMenuState`) since the last `mark`; the log lists the states as the game
  enters them (`TAS: ... state ...`)
- `instate CLASS`: some state machine is in that state now
- `file TEXT`: a disc file whose path contains TEXT was read since the last `mark` (the
  game keeps most data in a few large archives, so states say more)
- `TYPE EXPR OP VALUE`: a value in guest memory, TYPE `u8 s8 u16 s16 u32 s32 f32`, OP
  `== != < <= > >=`, VALUE a number or `$NAME`, `$NAME+N`, `$NAME-N` (a `let` value)

EXPR: hex numbers (`0x` optional), names, `+`/`-` offsets and `[expr]` to read a pointer,
e.g. `[[pEORGlobals__5Globs]+0x174]+0xC0`. Names are aliases, `tools/game_addrs.txt` names,
or any symbol of the game's ELF (`files/tsc_wii_release_dvd.elf` on the disc). An address
outside guest RAM makes the condition false and the telemetry cell empty.

## Game values

`tools/tas/castaway.tas` names what the end-to-end test reads (from the game's symbols and
its code, noted there):

| name | value |
|---|---|
| `sim` | the selected Sim (`cXPerson*`): `[pEORGlobals__5Globs]+0x174` |
| `simx`, `simy` | its tile position (`s32`, fixed point; 16 units per tile, -16 before the Sim is placed) |
| `simsleep` | `f32`, below -30 while the Sim sleeps (`cXPerson::IsSleeping`) |
| `hour`, `day`, `minute` | the simulator's clock globals (`s16`) |

The 16-units-per-tile scale is inferred from the values seen while walking, not from code.

## Notes on this game

- With no pointer, A confirms a dialog's highlighted button, and the stick moves the
  dialog's cursor (so it can move off the button). Story dialogs are answered with A and
  the stick left alone.
- After the pointer was away, the interface takes a moment to come back: point at the
  screen and wait before clicking the HUD.
- Answering Yes to "play the tutorial?" locks HUD buttons until the tutorial's steps are
  done; the end-to-end test answers No.
