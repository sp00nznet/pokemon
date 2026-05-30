#!/usr/bin/env python3
"""Enumerate executed bank-1C function entries in the real game (PyBoy).

Scans bank 1C ROM bytes for candidate function entries (addresses
immediately after a RET 0xC9 or RETI 0xD9), hooks every candidate, runs
the intro, and prints the hit addresses in yellow_seeds.txt format.
"""
import sys
from pyboy import PyBoy

ROM = "roms/Pokemon Yellow Version - Special Pikachu Edition.gbc"
FRAMES = int(sys.argv[1]) if len(sys.argv) > 1 else 2500
BANK = 0x1C

rom = open(ROM, "rb").read()
bank_start = BANK * 0x4000
bank_data = rom[bank_start:bank_start + 0x4000]

# Candidate function entries: address right after a RET (0xC9) or RETI (0xD9)
candidates = []
ILLEGAL_OPS = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}
for i in range(1, 0x4000):
    prev = bank_data[i - 1]
    if prev not in (0xC9, 0xD9):
        continue
    op = bank_data[i]
    if op in ILLEGAL_OPS:
        continue
    candidates.append(0x4000 + i)

print(f"Bank {BANK:02X}: {len(candidates)} candidate post-RET function entries")

hits = set()
def mk_hook(addr):
    def cb(_ctx):
        hits.add(addr)
    return cb

with PyBoy(ROM, window="null") as pyboy:
    pyboy.set_emulation_speed(0)
    for a in candidates:
        pyboy.hook_register(BANK, a, mk_hook(a), None)
    for _ in range(FRAMES):
        if not pyboy.tick():
            break

print(f"Hit {len(hits)} of {len(candidates)} candidates after {FRAMES} frames")
print("# pyboy_bank1c_sweep hits (yellow_seeds.txt format)")
for a in sorted(hits):
    print(f"{BANK}:{a:04X}")
