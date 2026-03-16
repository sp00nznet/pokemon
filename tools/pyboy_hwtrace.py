#!/usr/bin/env python3
"""
PyBoy hardware trace capture - produces output comparable to gb-recompiled's hwtrace.

Captures per-frame: registers, WRAM CRC, OAM CRC, VRAM CRCs, enabling
frame-by-frame comparison against the recompiled binary.

Usage:
    python pyboy_hwtrace.py roms/"Pokemon Red Version.gb" -o pyboy_trace.log --frames 1000
    python pyboy_hwtrace.py roms/"Pokemon Red Version.gb" -o pyboy_trace.log --frames 9000 --input auto_pokemon.txt
"""
import sys
import os
import argparse
import zlib
from pyboy import PyBoy

def crc32(data):
    """CRC32 matching gb-recompiled's implementation."""
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF

def main():
    parser = argparse.ArgumentParser(description="PyBoy hardware trace for ground truth comparison")
    parser.add_argument("rom", help="Path to ROM file")
    parser.add_argument("-o", "--output", default="pyboy_trace.log", help="Output trace file")
    parser.add_argument("-f", "--frames", type=int, default=1000, help="Number of frames")
    parser.add_argument("--input", help="Input script file (frame:button per line)")
    parser.add_argument("--speed", type=int, default=0, help="Emulation speed (0=unlimited)")
    parser.add_argument("--no-window", action="store_true", default=True, help="Run headless")
    parser.add_argument("--entry-trace", help="Also output entry point trace for recompiler seeding")
    args = parser.parse_args()

    if not os.path.exists(args.rom):
        print(f"Error: ROM not found: {args.rom}")
        return 1

    # Parse input script
    input_events = {}
    if args.input and os.path.exists(args.input):
        with open(args.input) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                parts = line.split(':')
                if len(parts) >= 2:
                    frame = int(parts[0])
                    btn = parts[1].strip().lower()
                    dur = int(parts[2]) if len(parts) > 2 else 10
                    if frame not in input_events:
                        input_events[frame] = []
                    input_events[frame].append((btn, dur))

    # Track unique entry points for recompiler seeding
    visited_addrs = set()

    window = "null" if args.no_window else "SDL2"
    print(f"Initializing PyBoy: {args.rom}")
    with PyBoy(args.rom, window=window) as pyboy:
        pyboy.set_emulation_speed(args.speed)

        out = open(args.output, "w")
        out.write("# PyBoy HW Trace (ground truth)\n")
        out.write(f"# ROM: {args.rom}\n")
        out.write(f"# Frames: {args.frames}\n")

        print(f"Running {args.frames} frames...")

        for frame in range(args.frames):
            # Process input events
            if frame in input_events:
                for btn, dur in input_events[frame]:
                    pyboy.button(btn, dur)

            # Advance one frame
            if not pyboy.tick():
                print(f"PyBoy quit at frame {frame}")
                break

            # Sample entry point
            try:
                pc = pyboy.register_file.PC
                bank = pyboy.memory[0xFFB8] if (0x4000 <= pc < 0x8000) else 0
                visited_addrs.add((bank, pc))
            except:
                pass

            # Capture hardware state at VBlank (end of frame)
            try:
                regs = pyboy.register_file
                af = (regs.A << 8) | regs.F
                bc = (regs.B << 8) | regs.C
                de = (regs.D << 8) | regs.E
                hl = regs.HL
                sp = regs.SP
                pc_val = regs.PC

                # Read ROM bank from Pokemon's hLoadedROMBank (0xFFB8)
                rom_bank = pyboy.memory[0xFFB8]

                # WRAM CRC (C000-DFFF = 8KB)
                wram_data = bytes(pyboy.memory[addr] for addr in range(0xC000, 0xE000))
                wram_crc = crc32(wram_data)

                # OAM CRC (FE00-FE9F = 160 bytes)
                oam_data = bytes(pyboy.memory[addr] for addr in range(0xFE00, 0xFEA0))
                oam_crc = crc32(oam_data)

                # VRAM tilemap CRCs
                tmap0_data = bytes(pyboy.memory[addr] for addr in range(0x9800, 0x9C00))
                tmap0_crc = crc32(tmap0_data)
                tmap1_data = bytes(pyboy.memory[addr] for addr in range(0x9C00, 0xA000))
                tmap1_crc = crc32(tmap1_data)

                # VRAM tile data CRC (8000-97FF)
                tdata_data = bytes(pyboy.memory[addr] for addr in range(0x8000, 0x9800))
                tdata_crc = crc32(tdata_data)

                # DIV register
                div_val = pyboy.memory[0xFF04]

                # Key game state for Pokemon debugging
                lcdc = pyboy.memory[0xFF40]
                stat = pyboy.memory[0xFF41]
                scx = pyboy.memory[0xFF43]
                scy = pyboy.memory[0xFF42]
                bgp = pyboy.memory[0xFF47]

                out.write(f"VBLANK FRAME:{frame} OAM_CRC={oam_crc:08X} TMAP0_CRC={tmap0_crc:08X} TMAP1_CRC={tmap1_crc:08X} TDATA_CRC={tdata_crc:08X} VRAM1_CRC=00000000 FB_CRC=00000000\n")
                out.write(f"REGS FRAME:{frame} AF={af:04X} BC={bc:04X} DE={de:04X} HL={hl:04X} SP={sp:04X} PC={pc_val:04X} BANK={rom_bank} DIV={div_val:04X} WRAM={wram_crc:08X}\n")

            except Exception as e:
                out.write(f"# Error at frame {frame}: {e}\n")

            if frame % 300 == 0:
                print(f"  Frame {frame}/{args.frames}")

        out.close()
        print(f"Trace saved to {args.output}")
        print(f"Captured {len(visited_addrs)} unique entry points")

        # Save entry point trace for recompiler seeding
        if args.entry_trace:
            with open(args.entry_trace, "w") as ef:
                for bank, addr in sorted(visited_addrs):
                    ef.write(f"{bank}:{addr:04x}\n")
            print(f"Entry trace saved to {args.entry_trace}")

    return 0

if __name__ == "__main__":
    sys.exit(main() or 0)
