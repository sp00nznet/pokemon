"""Precisely characterize the intro timing gap between recompiled Red and PyBoy.

Runs BOTH from boot with NO input for N frames, recording a cheap per-frame
fingerprint (fraction dark / fraction mid / mean luminance). Then:
  * prints the per-frame signals side by side at sample points,
  * finds the first frame where the structural screen breaks alignment,
  * cross-correlates the 'darkness' signal to estimate any global lag,
so we can tell a constant startup shift from a rate drift from a single
transition that fires at a different frame.
"""
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROM = os.path.abspath("red/rom.gb")
N = int(sys.argv[1]) if len(sys.argv) > 1 else 1500


def fingerprint_series(engine, n):
    """Return (n,3) array: [frac_dark, frac_mid, mean_lum] per frame, plus the
    full structural-index frames (n,144,160) int8 for exact matching."""
    fp = np.zeros((n, 3), dtype=np.float32)
    idxs = np.zeros((n, 144, 160), dtype=np.int8)
    for f in range(n):
        engine.tick(1, True)
        rgb = engine.screen.ndarray[:, :, :3].astype(np.float32)
        lum = 0.299 * rgb[:, :, 0] + 0.587 * rgb[:, :, 1] + 0.114 * rgb[:, :, 2]
        shades = sorted(np.unique(lum))
        order = {v: i for i, v in enumerate(shades)}
        idx = np.vectorize(order.get)(lum).astype(np.int8)
        maxi = max(1, len(shades) - 1)
        fp[f, 0] = np.mean(idx >= maxi)            # darkest fraction
        fp[f, 1] = np.mean((idx > 0) & (idx < maxi))  # mid fraction
        fp[f, 2] = lum.mean()
        idxs[f] = idx
    return fp, idxs


def main():
    from pyboy import PyBoy
    ref = PyBoy(ROM, window="null")
    from pyboy_shim import PyBoyShim
    test = PyBoyShim()

    print(f"capturing {N} frames (no input) on both engines...")
    rfp, ridx = fingerprint_series(ref, N)
    tfp, tidx = fingerprint_series(test, N)

    # structural per-frame match at the SAME frame index
    same = np.array([np.mean(ridx[f] == tidx[f]) for f in range(N)])
    # first sustained break (match < 0.95 for >=5 consecutive frames)
    brk = None
    run = 0
    for f in range(N):
        run = run + 1 if same[f] < 0.95 else 0
        if run >= 5:
            brk = f - 4
            break
    print(f"first sustained structural break (same-frame match <95%): "
          f"frame {brk}")

    # for a sample of test frames after the break, find best-matching ref frame
    print("\ntest_frame -> best ref_frame (structural %), within +/-120:")
    for tf in range(0 if brk is None else brk, N, 100):
        lo, hi = max(0, tf - 120), min(N, tf + 121)
        best_f, best = -1, -1.0
        for rf in range(lo, hi):
            m = np.mean(tidx[tf] == ridx[rf])
            if m > best:
                best, best_f = m, rf
        print(f"  test@{tf:4d} -> ref@{best_f:4d}  ({best*100:5.1f}%)  "
              f"offset {best_f-tf:+d}")

    # cross-correlate darkness signal to estimate global lag
    a = rfp[:, 0] - rfp[:, 0].mean()
    b = tfp[:, 0] - tfp[:, 0].mean()
    corr = np.correlate(a, b, mode="full")
    lag = corr.argmax() - (N - 1)
    print(f"\ndarkness-signal best global lag (ref - test): {lag} frames "
          f"(positive = test runs ahead)")

    ref.stop(save=False); test.stop(save=False)


if __name__ == "__main__":
    main()
