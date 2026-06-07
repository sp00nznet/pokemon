"""Diagnose where recompiled Red and PyBoy desync during the no-input intro.

Captures side-by-side screens at chosen frames, lists the specific divergent
WRAM addresses, and tests the frame-offset hypothesis (is test simply running
the same sequence a few frames ahead/behind ref?).
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pyboy_shim import PyBoyShim  # noqa: E402

ROM = os.path.abspath("red/rom.gb")
WRAM_LO, WRAM_HI = 0xC000, 0xE000


def step_to(engine, n):
    engine.tick(n, True)


def shade_idx(rgb):
    lum = 0.299 * rgb[:, :, 0] + 0.587 * rgb[:, :, 1] + 0.114 * rgb[:, :, 2]
    order = {v: i for i, v in enumerate(sorted(np.unique(lum)))}
    return np.vectorize(order.get)(lum).astype(np.int16)


def main():
    from pyboy import PyBoy
    ref = PyBoy(ROM, window="null")
    test = PyBoyShim()

    snaps_ref, snaps_test = {}, {}
    grab = [900, 1000, 1100, 1200, 1300]
    cur = 0
    for target in grab:
        step_to(ref, target - cur)
        step_to(test, target - cur)
        cur = target
        snaps_ref[target] = ref.screen.ndarray[:, :, :3].copy()
        snaps_test[target] = test.screen.ndarray[:, :, :3].copy()

    # side-by-side montage (ref top row, test bottom row)
    cols = []
    for t in grab:
        cols.append(np.concatenate([snaps_ref[t], snaps_test[t]], axis=0))
    montage = np.concatenate(cols, axis=1)
    Image.fromarray(montage, "RGB").save("red/diag_montage.png")
    print("saved red/diag_montage.png (top=PyBoy, bottom=recompiled, "
          f"cols={grab})")

    # frame-offset test: does test@1200 structurally match ref@(1200+k)?
    # rebuild fresh engines and scan a window of ref offsets.
    ref2 = PyBoy(ROM, window="null")
    test2 = PyBoyShim()
    step_to(test2, 1200)
    test_idx = shade_idx(test2.screen.ndarray[:, :, :3])
    best = (None, -1.0)
    for f in range(1160, 1241):
        # ref2 advances frame by frame
        pass
    # advance ref2 to 1160 then sweep
    step_to(ref2, 1160)
    for f in range(1160, 1241):
        ri = shade_idx(ref2.screen.ndarray[:, :, :3])
        pct = 100.0 * np.mean(ri == test_idx)
        if pct > best[1]:
            best = (f, pct)
        ref2.tick(1, True)
    print(f"frame-offset test: test@1200 best matches ref@{best[0]} "
          f"({best[1]:.2f}% structural). offset = {best[0]-1200:+d} frames")

    ref.stop(save=False); test.stop(save=False)
    ref2.stop(save=False); test2.stop(save=False)


if __name__ == "__main__":
    main()
