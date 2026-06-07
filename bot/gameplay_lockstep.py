"""Gameplay lockstep test: the real recompilation-correctness check.

1. Drive each engine independently from boot into the overworld (new game ->
   Red's bedroom) and let it settle (close menus, stand idle). The intro
   pacing differs, but the *logical* state converges (same map/pos).
2. Verify the two engines are synced at that point (WRAM diff count).
3. Feed IDENTICAL movement inputs frame-for-frame and report whether they
   stay byte-for-byte identical -- gameplay is frame-deterministic, so a
   correct recompilation should track PyBoy exactly from a synced state.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROM = os.path.abspath("red/rom.gb")
WRAM_LO, WRAM_HI = 0xC000, 0xE000
D_MAP, D_PY, D_PX, D_PARTY = 0xD35E, 0xD361, 0xD362, 0xD163


def wram(e):
    return bytes(e.memory[WRAM_LO:WRAM_HI])


def newgame_macro(e, frames):
    for f in range(1, frames + 1):
        if f % 16 == 0:   e.button_press("a")
        elif f % 16 == 4: e.button_release("a")
        elif f % 16 == 8: e.button_press("start")
        elif f % 16 == 12:e.button_release("start")
        e.tick(1, True)


def settle(e, frames=240):
    """Release everything, tap B to dismiss menus/text, then idle."""
    for n in ("a", "start", "up", "down", "left", "right", "b"):
        e.button_release(n)
    for f in range(frames):
        if f % 20 == 0: e.button_press("b")
        elif f % 20 == 4: e.button_release("b")
        e.tick(1, True)
    e.button_release("b")


def diff_count(a, b):
    return sum(1 for i in range(len(a)) if a[i] != b[i])


def main():
    from pyboy import PyBoy
    ref = PyBoy(ROM, window="null")
    from pyboy_shim import PyBoyShim
    test = PyBoyShim()

    # Each engine drives itself into the bedroom (different frame budgets ok).
    newgame_macro(ref, 1200)
    newgame_macro(test, 1100)   # ~100f ahead, give it less so both land idle
    settle(ref)
    settle(test)

    print(f"ref : map=0x{ref.memory[D_MAP]:02X} "
          f"pos=({ref.memory[D_PX]},{ref.memory[D_PY]}) party={ref.memory[D_PARTY]}")
    print(f"test: map=0x{test.memory[D_MAP]:02X} "
          f"pos=({test.memory[D_PX]},{test.memory[D_PY]}) party={test.memory[D_PARTY]}")
    d0 = diff_count(wram(ref), wram(test))
    print(f"WRAM diff at sync point: {d0} / {WRAM_HI-WRAM_LO} bytes")

    # Identical movement script: walk a square. One tile = hold dir ~16 frames.
    moves = ["down", "down", "left", "left", "up", "up", "right", "right"] * 3
    print(f"\nfeeding {len(moves)} identical tile-moves...")
    diverged_at = None
    for i, mv in enumerate(moves):
        for e in (ref, test):
            e.button_press(mv)
        for _ in range(8):
            ref.tick(1, True); test.tick(1, True)
        for e in (ref, test):
            e.button_release(mv)
        for _ in range(8):
            ref.tick(1, True); test.tick(1, True)
        d = diff_count(wram(ref), wram(test))
        rp = (ref.memory[D_PX], ref.memory[D_PY])
        tp = (test.memory[D_PX], test.memory[D_PY])
        flag = "" if (d == 0 and rp == tp) else "  <-- DIVERGED"
        if flag and diverged_at is None:
            diverged_at = i
        print(f"  move {i:2d} {mv:5s}: ref_pos={rp} test_pos={tp} "
              f"wram_diff={d}{flag}")

    print("\n===== RESULT =====")
    if diverged_at is None:
        print("Gameplay stayed BYTE-FOR-BYTE identical across all moves. "
              "Recompiled engine tracks PyBoy exactly in the overworld.")
    else:
        print(f"Diverged starting at move {diverged_at}. "
              f"(sync-point WRAM diff was {d0}; if large, the engines were "
              "not truly synced before movement.)")

    ref.stop(save=False); test.stop(save=False)


if __name__ == "__main__":
    main()
