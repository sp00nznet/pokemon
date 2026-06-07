"""Generate a start-state snapshot for the recompiled engine by driving it from
boot through the new-game intro until the player is actually CONTROLLABLE in the
overworld (detected by the player moving when we hold a direction), then save
our gbrom snapshot. This is the init_state the PWhiddy env will load() instead of
PyBoy's incompatible .state file.
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pyboy_shim import PyBoyShim  # noqa: E402

OUT = os.environ.get("STATE_OUT") or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "red_start.gbromstate")
D_MAP, D_PY, D_PX = 0xD35E, 0xD361, 0xD362


def mash(e, name, frames, on=4, period=8):
    for f in range(frames):
        if f % period == 0:
            e.button_press(name)
        elif f % period == on:
            e.button_release(name)
        e.tick(1, True)
    e.button_release(name)


def try_walk(e, name="down", hold=24):
    """Hold a direction; return True if the player's position changed."""
    before = (e.memory[D_PX], e.memory[D_PY])
    e.button_press(name)
    for _ in range(hold):
        e.tick(1, True)
    e.button_release(name)
    for _ in range(8):
        e.tick(1, True)
    after = (e.memory[D_PX], e.memory[D_PY])
    return after != before, after


def mash_a_start(e, frames, period=16):
    """Alternate A and START taps -- A advances text/menus, START confirms the
    name-entry screen (pure-A mashing gets stuck typing on the keyboard)."""
    for f in range(frames):
        m = f % period
        if m == 0:    e.button_press("a")
        elif m == 4:  e.button_release("a")
        elif m == 8:  e.button_press("start")
        elif m == 12: e.button_release("start")
        e.tick(1, True)
    e.button_release("a"); e.button_release("start")


def controllable_now(e):
    """Strict check: player moves on TWO consecutive walk attempts."""
    m1, _ = try_walk(e, "down", 32)
    m2, _ = try_walk(e, "up", 32)
    return m1 and m2


def main():
    import numpy as np
    e = PyBoyShim()
    # Phase 1: blow through title + Oak intro + naming (A advances, START
    # confirms the name). This reliably reaches the bedroom.
    mash_a_start(e, 1600)
    # Phase 2: clear any residual text, then confirm real player control.
    controllable = False
    for attempt in range(25):
        mash(e, "a", 120)           # dismiss any lingering textbox
        mash(e, "b", 60)            # B also backs out / advances
        if controllable_now(e):
            controllable = True
            print(f"control confirmed at attempt {attempt} "
                  f"(map=0x{e.memory[D_MAP]:02X})")
            break
        print(f"attempt {attempt:2d}: map=0x{e.memory[D_MAP]:02X} "
              f"pos=({e.memory[D_PX]},{e.memory[D_PY]}) not yet controllable")
    # debug screenshot of where we ended up
    Image.fromarray(e.screen.ndarray[:, :, :3], "RGB").save(
        os.path.join(os.path.dirname(OUT), "start_state_debug.png"))

    if not controllable:
        print("WARNING: never detected player control; saving anyway.")
    # Settle: release everything, idle a bit so we snapshot a clean idle frame.
    for n in ("a", "b", "up", "down", "left", "right", "start", "select"):
        e.button_release(n)
    for _ in range(30):
        e.tick(1, True)

    with open(OUT, "wb") as f:
        e.save_state(f)
    print(f"saved start state -> {OUT}  "
          f"(map=0x{e.memory[D_MAP]:02X} pos=({e.memory[D_PX]},{e.memory[D_PY]}))")
    e.stop(save=False)

    # Verify: reload into a FRESH engine, confirm it renders non-blank and the
    # player is immediately controllable (this is what the env will do).
    import numpy as np
    v = PyBoyShim()
    with open(OUT, "rb") as f:
        v.load_state(f)
    v.tick(2, True)
    shades = len(np.unique(v.screen.ndarray[:, :, :3]))
    moved, pos = try_walk(v, "down")
    print(f"verify reload: screen_shades={shades} (1=blank) "
          f"controllable={moved} pos_after={pos}")
    if shades <= 1 or not moved:
        print("  !! reloaded state is blank or not controllable -- investigate")
    else:
        print("  OK: reloaded state renders and player moves immediately")
    v.stop(save=False)


if __name__ == "__main__":
    main()
