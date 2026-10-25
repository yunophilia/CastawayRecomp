"""Builds input scripts for runtime/input (--script): a shared boot prefix plus steps.

  python tools/scripts/gen.py <name>      writes tools/scripts/<name>.txt

Frames are presented frames (60 per second). Pointer coordinates are given as screen
fractions (u, v) and converted to the game's pointer coordinates like the mouse is.
"""
import os
import sys


def game(u, v):
    """Screen fraction -> the game's pointer coordinates (runtime/input/input.cpp)."""
    return (u - 0.488) / 0.360, (v - 0.462) / 0.462


class Script:
    def __init__(self, comment):
        self.lines = [f'# {comment}']
        self.frame = 0

    def at(self, frame, action):
        self.lines.append(f'{frame} {action}')

    def tap(self, frame, button='A', hold=6):
        self.at(frame, f'press {button}')
        self.at(frame + hold, f'release {button}')

    def click(self, frame, u, v, settle=40):
        """Point at screen fraction (u, v), then press A."""
        x, y = game(u, v)
        self.at(frame, f'point {x:.3f} {y:.3f}')
        self.tap(frame + settle)
        return frame + settle + 6

    def shots(self, start, end, every, prefix):
        for i, f in enumerate(range(start, end, every)):
            self.at(f, f'shot {prefix}_{i:02d}')

    def save(self, name, quit_at):
        self.at(quit_at, 'quit')
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), name + '.txt')
        open(path, 'w', newline='\n').write('\n'.join(self.lines) + '\n')
        print(path)


def boot_to_menu(s):
    """Tap A through the safety screens and the title (A does nothing on the menu itself)."""
    for f in range(240, 1250, 50):
        s.tap(f, hold=5)
    return 1300


def new_crew(s, f):
    """Main menu -> Start New Game -> Yes (D-pad down, A); returns the frame Create a Crew is up."""
    f = s.click(f + 50, 575 / 960, 560 / 672)
    # the dialog takes a varying time to accept input: click Yes a few times
    for d in (90, 190, 290, 390):
        s.click(f + d, 0.5, 0.714, settle=20)
    return f + 1150


SCRIPTS = {}


def script(fn):
    SCRIPTS[fn.__name__] = fn
    return fn


@script
def cas_customize():
    s = Script('Create a Crew -> Add a Sim -> Customize')
    f = new_crew(s, boot_to_menu(s))
    f = s.click(f, 0.47, 0.85)          # Add a Sim
    f = s.click(f + 150, 0.306, 0.86)   # Customize
    s.shots(f + 60, f + 1500, 100, 'cc')
    s.save('cas_customize', f + 1502)


@script
def cas_explore():
    s = Script('CAS: try inputs on the Occupation panel, a screenshot after each')
    f = new_crew(s, boot_to_menu(s))
    f = s.click(f, 0.47, 0.85)          # Add a Sim
    f = s.click(f + 150, 0.306, 0.86)   # Customize
    s.at(f + 100, 'shot ce_00_start')
    f = s.click(f + 110, 0.219, 0.253)  # the Ranger icon
    s.at(f + 60, 'shot ce_01_icon')
    f = s.click(f + 100, 0.219, 0.253)  # again
    s.at(f + 60, 'shot ce_02_icon2')
    for i, b in enumerate(['RIGHT', 'A', 'PLUS', 'DOWN', 'A'], start=3):
        s.tap(f + 80, b)
        s.at(f + 150, f'shot ce_{i:02d}_{b}')
        f += 150
    s.save('cas_explore', f + 10)


def create_sim(s, f):
    """Create a Crew -> Add a Sim -> Customize -> Ranger -> Accept; returns the next frame."""
    f = s.click(f, 0.47, 0.85)           # Add a Sim
    f = s.click(f + 150, 0.306, 0.86)    # Customize
    f = s.click(f + 110, 0.219, 0.253)   # occupation: Ranger
    return s.click(f + 100, 0.306, 0.86)   # Accept


@script
def cas_accept():
    s = Script('create a Sim and accept it')
    f = create_sim(s, new_crew(s, boot_to_menu(s)))
    s.shots(f + 30, f + 2400, 120, 'ca')
    s.save('cas_accept', f + 2402)


@script
def crew_done():
    s = Script('create a Sim, Done, then follow the story (A every 4 s)')
    f = create_sim(s, new_crew(s, boot_to_menu(s)))
    f = s.click(f + 300, 0.737, 0.853)   # Done
    s.at(f + 10, 'nopoint')
    for d in range(600, 6000, 240):
        s.tap(f + d)
    s.shots(f + 60, f + 6000, 200, 'cd')
    s.save('crew_done', f + 6002)


def to_beach(s, park=None):
    """Boot, new game, create a Sim, Done, save, story intro, OK: returns the frame the Sim
    stands on the beach with the movement tutorial up."""
    f = create_sim(s, new_crew(s, boot_to_menu(s)))
    f = s.click(f + 300, 0.737, 0.853)   # Done
    for d in (120, 220, 320):            # "save before continuing?": Yes
        s.click(f + d, 0.5, 0.48, settle=20)
    for d in (420, 520):                 # "overwrite your saved game data?" (if there is one): Yes
        s.click(f + d, 0.5, 0.5625, settle=20)
    if park is None:                     # then no pointer (pointed away from the screen)
        s.at(f + 600, 'nopoint')
    else:                                # or parked at a screen position
        x, y = game(*park)
        s.at(f + 600, f'point {x:.3f} {y:.3f}')
    for d in range(900, 5400, 240):      # story, "shipwrecked" OK (its timing varies)
        s.tap(f + d)
    return f + 5500


@script
def beach_move():
    s = Script('to the beach, then walk with the stick in four directions')
    f = to_beach(s)
    s.at(f, 'shot bm_00')
    for i, (x, y) in enumerate([(0, 100), (100, 0), (0, -100), (-100, 0)], start=1):
        s.at(f + 10, f'stick {x} {y}')
        s.at(f + 100, f'shot bm_{i:02d}a')
        s.at(f + 200, f'shot bm_{i:02d}b')
        s.at(f + 210, 'stick 0 0')
        f += 260
    s.at(f + 100, 'shot bm_99')
    s.save('beach_move', f + 102)


@script
def soak():
    """Ten minutes of random play on the island (seeded, so a crash can be replayed)."""
    import random
    rnd = random.Random(1234)
    s = Script('to the beach, then 10 minutes of random input (seed 1234)')
    f = to_beach(s)
    end = f + 10 * 60 * 60
    buttons = ['A', 'A', 'A', 'B', 'B', 'C', 'Z', '1', '2', 'PLUS', 'MINUS', 'UP', 'DOWN', 'LEFT', 'RIGHT']
    while f < end:
        r = rnd.random()
        if r < 0.35:
            s.at(f, f'stick {rnd.randint(-100, 100)} {rnd.randint(-100, 100)}')
            s.at(f + rnd.randint(30, 180), 'stick 0 0')
        elif r < 0.45:
            s.at(f, f'cstick {rnd.randint(-100, 100)} {rnd.randint(-100, 100)}')
            s.at(f + rnd.randint(20, 90), 'cstick 0 0')
        elif r < 0.65:
            x, y = game(rnd.uniform(0.05, 0.95), rnd.uniform(0.05, 0.95))
            s.at(f, f'point {x:.3f} {y:.3f}')
        else:
            s.tap(f, rnd.choice(buttons), hold=rnd.randint(4, 20))
        f += rnd.randint(20, 120)
    s.shots(end - 10 * 60 * 60, end, 1200, 'sk')
    s.save('soak', end + 2)


@script
def continue_game():
    s = Script('main menu -> Continue Game (needs a save from an earlier run)')
    f = boot_to_menu(s)
    f = s.click(f + 50, 0.578, 0.632)    # Continue Game
    s.at(f + 10, 'nopoint')
    for d in range(200, 2400, 200):      # confirm dialogs, if any
        s.tap(f + d)
    s.shots(f + 60, f + 3000, 150, 'cg')
    s.save('continue_game', f + 3002)


@script
def menu_dpad():
    s = Script('main menu with the D-pad only (no pointer): shots after each press')
    f = boot_to_menu(s) + 200
    s.at(f, 'shot md_00')
    for i, b in enumerate(['DOWN', 'DOWN', 'UP', 'LEFT', 'RIGHT', 'A'], start=1):
        s.tap(f + 20, b)
        s.at(f + 100, f'shot md_{i:02d}_{b}')
        f += 120
    s.save('menu_dpad', f + 300)


@script
def menu_stick():
    s = Script('main menu, gamepad only: the right stick moves the pointer to Start New Game, A')
    f = boot_to_menu(s) + 200
    s.at(f, 'shot ms_00')
    # 2.4 pointer units/s at full deflection = 0.04 per frame; target ~(0.31, 0.80) from (0, 0)
    s.at(f + 10, 'cstick 100 0')
    s.at(f + 18, 'cstick 0 -100')
    s.at(f + 38, 'cstick 0 0')
    s.at(f + 60, 'shot ms_01_pointed')
    s.tap(f + 70)
    s.at(f + 200, 'shot ms_02_after_a')
    s.save('menu_stick', f + 210)


@script
def find_bamboo():
    s = Script('beach: walk up-left looking for bamboo, screenshots every half second')
    f = to_beach(s)
    s.at(f, 'stick -70 70')
    s.shots(f, f + 600, 30, 'fb')
    s.at(f + 600, 'stick 0 0')
    s.save('find_bamboo', f + 602)


@script
def actions():
    s = Script('beach: harvest bamboo with Z; at the coconut pick Eat with the D-pad and Z')
    f = to_beach(s)
    s.at(f, 'stick -70 70')
    s.at(f + 100, 'stick 0 0')
    s.at(f + 130, 'shot ac_00_bamboo')
    s.tap(f + 140, 'Z')
    s.shots(f + 200, f + 800, 100, 'ac_1')
    f += 800
    s.at(f, 'stick -70 70')        # on towards the coconut (it was ~380 frames further)
    s.at(f + 380, 'stick 0 0')
    s.at(f + 420, 'shot ac_20_coconut')
    s.tap(f + 430, 'DOWN')
    s.at(f + 470, 'shot ac_21_down')
    s.tap(f + 480, 'Z')
    s.shots(f + 540, f + 1200, 100, 'ac_3')
    s.save('actions', f + 1202)


@script
def actions_choice():
    s = Script('beach: walk to the coconut (two actions), DOWN to pick the second, Z')
    f = to_beach(s)
    s.at(f, 'stick -70 70')
    s.at(f + 470, 'stick 0 0')
    s.at(f + 500, 'shot ax_00_coconut')
    s.tap(f + 510, 'DOWN')
    s.at(f + 550, 'shot ax_01_down')
    s.tap(f + 560, 'UP')
    s.at(f + 600, 'shot ax_02_up')
    s.tap(f + 610, 'DOWN')
    s.at(f + 650, 'shot ax_03_down')
    s.tap(f + 660, 'Z')
    s.shots(f + 700, f + 1300, 100, 'ax_1')
    s.save('actions_choice', f + 1302)


@script
def dialog_park_low():
    s = Script('to the beach with the pointer parked below the dialogs')
    f = to_beach(s, park=(0.5, 0.9))
    s.shots(f - 2000, f + 200, 200, 'dp')
    s.save('dialog_park_low', f + 202)


if __name__ == '__main__':
    for name in sys.argv[1:]:
        SCRIPTS[name]()
