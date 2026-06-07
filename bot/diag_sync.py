"""Inspect the post-new-game sync point: screenshot both engines and bucket
the divergent WRAM addresses by region, to tell animation/RNG phase-noise
apart from genuine logic divergence."""
import os
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gameplay_lockstep import newgame_macro, settle, wram, WRAM_LO  # noqa: E402

ROM = os.path.abspath("red/rom.gb")


def regions(diff_addrs):
    buckets = {
        "C000-C0FF OAM-shadow/sprites": (0xC000, 0xC100),
        "C100-C2FF sprite-state/anim": (0xC100, 0xC300),
        "C300-CBFF tilemap/audio/buf": (0xC300, 0xCC00),
        "CC00-CFFF misc/temp": (0xCC00, 0xD000),
        "D000-D5FF game-state": (0xD000, 0xD600),
        "D600-DFFF events/maps": (0xD600, 0xE000),
    }
    out = {}
    for name, (lo, hi) in buckets.items():
        out[name] = sum(1 for a in diff_addrs if lo <= a < hi)
    return out


def main():
    from pyboy import PyBoy
    ref = PyBoy(ROM, window="null")
    from pyboy_shim import PyBoyShim
    test = PyBoyShim()
    newgame_macro(ref, 1200); newgame_macro(test, 1100)
    settle(ref); settle(test)

    a, b = wram(ref), wram(test)
    diff_addrs = [WRAM_LO + i for i in range(len(a)) if a[i] != b[i]]
    print(f"total WRAM diff: {len(diff_addrs)} bytes")
    for name, n in regions(diff_addrs).items():
        if n:
            print(f"  {name}: {n}")
    print("first 24 divergent addrs:",
          " ".join(f"{x:04X}" for x in diff_addrs[:24]))

    r = ref.screen.ndarray[:, :, :3]
    t = test.screen.ndarray[:, :, :3]
    Image.fromarray(np.concatenate([r, t], axis=1), "RGB").save("red/sync_pair.png")
    print("saved red/sync_pair.png (left=PyBoy, right=recompiled)")
    ref.stop(save=False); test.stop(save=False)


if __name__ == "__main__":
    main()
