"""Test the 'constant early-boot offset' hypothesis.

If the only difference is that our engine skips an ~N-frame power-on/boot delay
that PyBoy emulates, then advancing PyBoy by N extra frames first should put the
two engines into byte-for-byte lockstep for the rest of the no-input intro.

Sweeps candidate offsets, and for the best one prints the per-frame WRAM diff
to show whether lockstep holds.
"""
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pyboy_shim import PyBoyShim  # noqa: E402

ROM = os.path.abspath("red/rom.gb")
WRAM_LO, WRAM_HI = 0xC000, 0xE000


def wram(e):
    return np.frombuffer(bytes(e.memory[WRAM_LO:WRAM_HI]), dtype=np.uint8)


def run(offset, compare_frames=150, settle=400, verbose=False):
    from pyboy import PyBoy
    ref = PyBoy(ROM, window="null")
    test = PyBoyShim()
    # advance both into the intro; give ref `offset` extra frames up front
    ref.tick(settle + offset, True)
    test.tick(settle, True)
    diffs = []
    for f in range(compare_frames):
        ref.tick(1, True)
        test.tick(1, True)
        d = int(np.count_nonzero(wram(ref) != wram(test)))
        diffs.append(d)
        if verbose and f % 15 == 0:
            print(f"    f+{f:3d}: wram_diff={d}")
    ref.stop(save=False); test.stop(save=False)
    return np.array(diffs)


def main():
    print("sweeping ref head-start offsets (settle=400, compare 150 frames)...")
    results = {}
    for off in [0, 40, 60, 70, 75, 78, 80, 85, 90, 100]:
        d = run(off)
        results[off] = (d.mean(), d.min(), int(np.median(d)))
        print(f"  offset {off:3d}: mean_wram_diff={d.mean():7.1f}  "
              f"min={d.min():4d}  median={int(np.median(d)):4d}")
    best = min(results, key=lambda k: results[k][0])
    print(f"\nbest offset = {best} (mean diff {results[best][0]:.1f}). "
          "detail:")
    run(best, compare_frames=150, verbose=True)


if __name__ == "__main__":
    main()
