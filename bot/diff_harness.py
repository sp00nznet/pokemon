"""Differential test harness: recompiled Red (rom_headless.dll) vs stock PyBoy.

Drives BOTH engines from boot with an identical input schedule, then compares
game state each checkpoint:

  * curated game-state addresses (the ones PokemonRedExperiments rewards on)
  * the full WRAM range 0xC000-0xDFFF (first divergence + diff count)
  * the screen, STRUCTURALLY -- each engine's pixels are remapped to a 0..3
    palette index by luminance, so the two engines' different DMG palettes
    don't register as false differences.

Output is a divergence report (first-divergence frame, which addresses, screen
match %), not a brittle pass/fail -- some divergence in timing/RNG-derived
bytes is expected and is itself useful signal for the recompiler.

Usage:
    python bot/diff_harness.py [--frames N] [--rom red/rom.gb]
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pyboy_shim import PyBoyShim  # noqa: E402

# --- curated game-state addresses (from PokemonRedExperiments) ----------------
CURATED = {
    0xD163: "party_size",
    0xD18C: "mon1_level", 0xD1B8: "mon2_level", 0xD1E4: "mon3_level",
    0xD210: "mon4_level", 0xD23C: "mon5_level", 0xD268: "mon6_level",
    0xD16C: "mon1_hp_lo", 0xD16D: "mon1_hp_hi",
    0xD356: "badges", 0xD35E: "current_map",
    0xD361: "pos_y", 0xD362: "pos_x", 0xD057: "battle_type",
    0xD747: "event_flags_start", 0xD87E: "event_flags_end",
}
WRAM_LO, WRAM_HI = 0xC000, 0xE000  # [lo, hi)


def remap_palette(rgb):
    """Map a (H,W,3) RGB frame to a (H,W) 0..3 palette-index image by ranking
    the (up to 4) distinct DMG shades by luminance. Palette-independent."""
    h, w, _ = rgb.shape
    lum = (0.299 * rgb[:, :, 0] + 0.587 * rgb[:, :, 1] + 0.114 * rgb[:, :, 2])
    shades = np.unique(lum)
    order = {v: i for i, v in enumerate(sorted(shades))}
    idx = np.vectorize(order.get)(lum)
    return idx.astype(np.int16), len(shades)


def make_pyboy(rom):
    from pyboy import PyBoy
    return PyBoy(rom, window="null")


def input_schedule(frame):
    """Deterministic, identical for both engines. Returns a list of
    (button_name, pressed) edges to apply at this frame boundary.
    Lets the intro/title play, then taps Start/A to poke menus."""
    edges = []
    # Tap START for 1 frame every 600 frames after the title (~frame 2400+),
    # and A shortly after, to exercise menu transitions deterministically.
    if frame >= 2400 and frame % 600 == 0:
        edges.append(("start", True))
    if frame >= 2400 and frame % 600 == 4:
        edges.append(("start", False))
    if frame >= 2400 and frame % 600 == 60:
        edges.append(("a", True))
    if frame >= 2400 and frame % 600 == 64:
        edges.append(("a", False))
    return edges


def apply_edges(engine, edges):
    for name, pressed in edges:
        (engine.button_press if pressed else engine.button_release)(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=3600)
    ap.add_argument("--rom", default="red/rom.gb")
    ap.add_argument("--checkpoint", type=int, default=100,
                    help="compare every N frames")
    ap.add_argument("--ref-head-start", type=int, default=80,
                    help="frames to pre-roll PyBoy past its built-in boot "
                         "splash so cartridge execution is phase-aligned with "
                         "our post-bootrom engine (measured ~80)")
    args = ap.parse_args()

    rom = os.path.abspath(args.rom)
    print(f"ref : PyBoy {rom}")
    print(f"test: rom_headless.dll (recompiled Red)")
    ref = make_pyboy(rom)
    test = PyBoyShim()

    # Phase-align: PyBoy runs a built-in boot splash before the cartridge,
    # our engine starts post-bootrom at PC=0x100. Pre-roll PyBoy so both are
    # at the same cartridge frame before identical inputs/comparison begin.
    if args.ref_head_start:
        ref.tick(args.ref_head_start, True)
        print(f"(pre-rolled PyBoy {args.ref_head_start} frames past boot splash)")

    first_wram_div = None
    curated_div = {}        # addr -> first frame it diverged
    screen_matches = []     # (frame, pct)

    for frame in range(1, args.frames + 1):
        edges = input_schedule(frame)
        apply_edges(ref, edges)
        apply_edges(test, edges)
        ref.tick(1, True)
        test.tick(1, True)

        if frame % args.checkpoint != 0:
            continue

        # --- WRAM compare ---
        r = bytes(ref.memory[WRAM_LO:WRAM_HI])
        t = bytes(test.memory[WRAM_LO:WRAM_HI])
        diffs = [i for i in range(len(r)) if r[i] != t[i]]
        if diffs and first_wram_div is None:
            first_wram_div = (frame, len(diffs), WRAM_LO + diffs[0])

        # --- curated compare ---
        for addr, name in CURATED.items():
            if ref.memory[addr] != test.memory[addr] and addr not in curated_div:
                curated_div[addr] = (frame, ref.memory[addr], test.memory[addr])

        # --- structural screen compare ---
        ri, rn = remap_palette(ref.screen.ndarray[:, :, :3])
        ti, tn = remap_palette(test.screen.ndarray[:, :, :3])
        pct = 100.0 * np.mean(ri == ti)
        screen_matches.append((frame, pct, rn, tn))

        print(f"[f{frame:5d}] wram_diffs={len(diffs):5d}  "
              f"screen_struct_match={pct:6.2f}%  shades ref={rn} test={tn}")

    ref.stop(save=False)
    test.stop(save=False)

    print("\n===== DIVERGENCE REPORT =====")
    if first_wram_div is None:
        print("WRAM: identical through entire run (no divergence).")
    else:
        f, n, a = first_wram_div
        print(f"WRAM: first divergence at frame {f}: {n} bytes differ, "
              f"first at 0x{a:04X}")
    if not curated_div:
        print("Curated game-state addresses: all identical.")
    else:
        print("Curated game-state divergences (addr: first_frame ref/test):")
        for addr, (f, rv, tv) in sorted(curated_div.items()):
            print(f"  0x{addr:04X} {CURATED[addr]:18s} frame {f}: "
                  f"ref={rv} test={tv}")
    if screen_matches:
        avg = np.mean([p for _, p, _, _ in screen_matches])
        worst = min(screen_matches, key=lambda x: x[1])
        print(f"Screen structural match: avg {avg:.2f}%, "
              f"worst {worst[1]:.2f}% at frame {worst[0]}")


if __name__ == "__main__":
    main()
