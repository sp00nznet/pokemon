"""Drive Red from the bedroom start-state until the player has a starter Pokemon
and is back in overworld control, then snapshot a richer training start-state.

Uses a movement+A biased random policy (reaches Oak's Lab, walks into a ball,
confirms the choice, mashes through the rival battle). Checks party_size each
step; once party>=1 and not in battle and controllable, snapshots.

Run against Red's CURRENT DLL (read-only context creation is fine while training
holds the file open). Output: bot/red_starter.gbromstate
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pyboy_shim import PyBoyShim  # noqa: E402

BOT = os.path.dirname(os.path.abspath(__file__))
START = os.path.join(BOT, "red_start.gbromstate")
OUT = os.path.join(BOT, "red_starter.gbromstate")

D_PARTY, D_MAP, D_PY, D_PX, D_BATTLE = 0xD163, 0xD35E, 0xD361, 0xD362, 0xD057
ACTS = ["down", "left", "right", "up", "a"]


def act(e, name, hold=8, rest=8):
    e.button_press(name)
    for _ in range(hold):
        e.tick(1, True)
    e.button_release(name)
    for _ in range(rest):
        e.tick(1, True)


def manhattan(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def controllable(e):
    """Strict: the player must actually WALK >=2 tiles (not just turn or bump a
    wall), proving free overworld control rather than a scripted/blocked state."""
    for d in ("down", "up", "left", "right"):
        p0 = (e.memory[D_PX], e.memory[D_PY])
        for _ in range(3):
            act(e, d, 16, 4)
        if manhattan((e.memory[D_PX], e.memory[D_PY]), p0) >= 2:
            return True
    return False


def main():
    budget = int(sys.argv[1]) if len(sys.argv) > 1 else 25000
    rng = np.random.default_rng(1)
    e = PyBoyShim()
    with open(START, "rb") as f:
        e.load_state(f)
    print(f"start: party={e.memory[D_PARTY]} map=0x{e.memory[D_MAP]:02X}")

    got_starter_at = None
    frames = 0
    last_log = 0
    while frames < budget:
        party = e.memory[D_PARTY]
        in_battle = e.memory[D_BATTLE] != 0
        # policy: once we have a starter, mash A to clear text/battle; else
        # bias movement to reach the lab + A to interact.
        if party >= 1 and got_starter_at is None:
            got_starter_at = frames
            print(f"[f{frames}] GOT STARTER (party={party}) map=0x{e.memory[D_MAP]:02X} "
                  f"battle={e.memory[D_BATTLE]}")
        if party >= 1:
            # clear the receive-text + rival battle by mashing A, occasional B
            name = "a" if frames % 5 else "b"
            act(e, name, 6, 6)
        else:
            name = rng.choice(ACTS, p=[0.22, 0.22, 0.22, 0.22, 0.12])
            act(e, name)
        frames += 1

        if frames - last_log >= 1000:
            last_log = frames
            print(f"[f{frames}] party={party} map=0x{e.memory[D_MAP]:02X} "
                  f"pos=({e.memory[D_PX]},{e.memory[D_PY]}) battle={e.memory[D_BATTLE]}")

        # success: have a starter, out of battle (rival battle done), and the
        # player can actually free-roam. Check only periodically (the strict
        # controllable() test itself walks ~12 frames).
        if party >= 1 and not in_battle and got_starter_at is not None \
                and frames > got_starter_at + 60 and frames % 40 == 0:
            if controllable(e):
                print(f"[f{frames}] starter + controllable overworld -> snapshot")
                for n in ACTS + ["b", "start", "select"]:
                    e.button_release(n)
                for _ in range(20):
                    e.tick(1, True)
                with open(OUT, "wb") as f:
                    e.save_state(f)
                Image.fromarray(e.screen.ndarray[:, :, :3], "RGB").save(
                    os.path.join(BOT, "red_starter_debug.png"))
                print(f"SAVED {OUT}  party={e.memory[D_PARTY]} "
                      f"map=0x{e.memory[D_MAP]:02X}")
                e.stop(save=False)
                return

    print(f"\nDID NOT reach a controllable starter state in {budget} steps "
          f"(party={e.memory[D_PARTY]}, got_starter_at={got_starter_at}).")
    Image.fromarray(e.screen.ndarray[:, :, :3], "RGB").save(
        os.path.join(BOT, "red_starter_debug.png"))
    e.stop(save=False)


if __name__ == "__main__":
    main()
