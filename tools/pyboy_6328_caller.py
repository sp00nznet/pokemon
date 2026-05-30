#!/usr/bin/env python3
"""Capture who calls bank1C:6328 (GAME FREAK palette setup) in PyBoy."""
import sys
from pyboy import PyBoy

ROM = "roms/Pokemon Yellow Version - Special Pikachu Edition.gbc"
FRAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 300
pyboy = None
frame = [0]
calls = []

def cb(_ctx):
    rf = pyboy.register_file
    sp = rf.SP
    stk = [pyboy.memory[(sp + i) & 0xFFFF] for i in range(8)]
    ret = stk[0] | (stk[1] << 8)
    bank = pyboy.memory[0xFFB8]
    # CALL signature at ret-3 in currently-mapped bank
    sig = bytes(pyboy.memory[(ret - 3 + i) & 0xFFFF] for i in range(3)).hex()
    calls.append((frame[0], ret, bank, sig, bytes(stk).hex(), rf.B, rf.HL))

with PyBoy(ROM, window="null") as pb:
    pyboy = pb
    pyboy.set_emulation_speed(0)
    pyboy.hook_register(0x1C, 0x6328, cb, None)
    for f in range(FRAMES):
        frame[0] = f
        if not pyboy.tick():
            break

print(f"=== bank1C:6328 called {len(calls)} times ===")
for f, ret, bank, sig, stk, B, HL in calls[:12]:
    call_site = (ret - 3) & 0xFFFF
    print(f"  frame={f:5d} ret={ret:04X} (call@{call_site:04X}) FFB8={bank:02X} "
          f"sig@ret-3={sig} B={B:02X} HL={HL:04X} stack={stk}")
