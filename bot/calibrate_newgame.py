"""Calibrate a 'new game' input macro: mash START/A from boot and log when
gameplay state populates (current_map, player position). Run against PyBoy
(ground truth) to find the frame budget + button cadence that reaches the
overworld, then reuse it to drive both engines into a comparable state.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROM = os.path.abspath("red/rom.gb")
D_MAP, D_PY, D_PX, D_PARTY = 0xD35E, 0xD361, 0xD362, 0xD163


def drive(engine, frames):
    """Alternate A and START taps to blow through title + Oak + naming."""
    last = {}
    for f in range(1, frames + 1):
        # tap A on even 8-frame slots, START on odd -> robust to pacing
        if f % 16 == 0:
            engine.button_press("a")
        elif f % 16 == 4:
            engine.button_release("a")
        elif f % 16 == 8:
            engine.button_press("start")
        elif f % 16 == 12:
            engine.button_release("start")
        engine.tick(1, True)
        if f % 100 == 0:
            mp, py, px, pty = (engine.memory[D_MAP], engine.memory[D_PY],
                               engine.memory[D_PX], engine.memory[D_PARTY])
            state = (mp, py, px, pty)
            if state != last.get("s"):
                print(f"[f{f:5d}] map=0x{mp:02X} pos=({px},{py}) party={pty}")
                last["s"] = state


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else "pyboy"
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
    if which == "pyboy":
        from pyboy import PyBoy
        eng = PyBoy(ROM, window="null")
        print("driving PyBoy")
    else:
        from pyboy_shim import PyBoyShim
        eng = PyBoyShim()
        print("driving recompiled (rom_headless.dll)")
    drive(eng, frames)
    eng.stop(save=False)


if __name__ == "__main__":
    main()
