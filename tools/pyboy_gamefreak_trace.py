#!/usr/bin/env python3
"""Find the caller of the GAME FREAK palette builder (b1C:6346) in PyBoy.

When the 6346 hook fires, read the pushed return address off the stack
and check for a `CALL $6346` (CD 46 63) signature at ret-3 to confirm
the real caller our recompiler missed.
"""
import sys, os
from pyboy import PyBoy

ROM = "roms/Pokemon Yellow Version - Special Pikachu Edition.gbc"
FRAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 900

pyboy = None
frame = [0]
calls_6346 = []
calls_60AD = []

def cap(lst):
    def cb(_ctx):
        rf = pyboy.register_file
        sp = rf.SP
        stk = [pyboy.memory[(sp + i) & 0xFFFF] for i in range(6)]
        ret = stk[0] | (stk[1] << 8)
        bank = pyboy.memory[0xFFB8]
        sig = bytes(pyboy.memory[(ret - 3 + i) & 0xFFFF] for i in range(3)).hex()
        lst.append((frame[0], ret, bank, sig, bytes(stk).hex()))
    return cb

if not os.path.exists(ROM):
    print("ROM not found:", ROM); sys.exit(1)

with PyBoy(ROM, window="null") as pb:
    pyboy = pb
    pyboy.set_emulation_speed(0)
    pyboy.hook_register(0x1C, 0x6346, cap(calls_6346), None)
    pyboy.hook_register(0x1C, 0x60AD, cap(calls_60AD), None)
    for f in range(FRAMES):
        frame[0] = f
        if not pyboy.tick():
            break

print(f"=== 6346 (GAME FREAK palette builder) — {len(calls_6346)} calls, first 10 ===")
for f, ret, bank, sig, stk in calls_6346[:10]:
    callsite = (ret - 3) & 0xFFFF
    via = "CALL@%04X" % callsite if sig == "cd4663" else "(JP/other) sig=%s" % sig
    print(f"  frame={f:5d}  ret={ret:04X}  {via}  FFB8bank={bank:02X}  stack={stk}")
print(f"=== 60AD (palette-setup wrapper) — {len(calls_60AD)} calls, first 10 ===")
for f, ret, bank, sig, stk in calls_60AD[:10]:
    print(f"  frame={f:5d}  ret={ret:04X}  FFB8bank={bank:02X}  stack={stk}")
