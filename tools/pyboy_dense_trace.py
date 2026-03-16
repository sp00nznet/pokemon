#!/usr/bin/env python3
"""
Dense execution trace capture using PyBoy 2.7's hook_register API.

Captures unique (bank, PC) pairs visited during N frames of Pokemon Red.
Output: "bank:addr" (hex), one per line -- compatible with recompiler --use-trace.

Strategy
--------
PyBoy's hook_register replaces a ROM byte with 0xDB. Each fire goes through
the full BRK path (GIL, dict lookup, callback, opcode restore, re-inject),
so hooking too many addresses makes emulation extremely slow.

We use a three-pronged approach for dense coverage with low overhead:

1. **Strategic hooks (~100)** -- bank-switch sites (LDH [FFB8], A patterns),
   RST/interrupt vectors, and key entry points. Each callback reads the
   current register state and call stack to extract multiple addresses.

2. **Call-stack harvesting** -- when a hook fires, we read SP and walk the
   stack to extract return addresses. Each return address implies the CALL
   instruction that pushed it, giving us two addresses per stack frame.

3. **Per-frame sampling** -- after each tick(), we read PC, SP, and the
   bank register, plus walk the stack again. With 9000+ frames this catches
   a different slice of execution each time.

Usage
-----
    python pyboy_dense_trace.py "roms/Pokemon Red Version.gb" -o trace.txt --frames 9000
    python pyboy_dense_trace.py "roms/Pokemon Red Version.gb" -o trace.txt --frames 9000 --input auto_pokemon.txt
"""

import sys
import os
import argparse
import time

# SM83 opcode lengths for instruction boundary detection
OPCODE_LENGTHS = [
    1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 2, 3, 3, 2, 1,
    1, 1, 3, 0, 3, 1, 2, 1, 1, 1, 3, 1, 3, 0, 2, 1,
    2, 1, 1, 0, 0, 1, 2, 1, 2, 1, 3, 0, 0, 0, 2, 1,
    2, 1, 1, 1, 0, 1, 2, 1, 2, 1, 3, 1, 0, 0, 2, 1,
]


def find_bankswitch_sites(rom_data):
    """Find all LDH [0xFFB8], A instructions in bank 0.
    These are bank-switch sites where we can capture bank + context."""
    sites = []
    for i in range(0x3FFF):
        if rom_data[i] == 0xE0 and rom_data[i + 1] == 0xB8:
            sites.append(i)
    return sites


def extract_branch_targets(rom_data, bank, bank_size=0x4000):
    """Extract CALL/JP/JR target addresses from a ROM bank.
    Returns only the branch DESTINATIONS (not every instruction)."""
    rom_offset = bank * bank_size
    base_addr = 0 if bank == 0 else 0x4000
    data = rom_data[rom_offset:rom_offset + bank_size]
    targets = set()

    pc = 0
    while pc < bank_size:
        op = data[pc]
        ln = OPCODE_LENGTHS[op]
        if ln == 0:
            pc += 1
            continue
        if pc + ln > bank_size:
            break

        cpu_addr = base_addr + pc

        if ln == 3:
            imm16 = data[pc + 1] | (data[pc + 2] << 8)
            if op in (0xCD, 0xCC, 0xC4, 0xD4, 0xDC,  # CALL
                      0xC3, 0xC2, 0xCA, 0xD2, 0xDA):  # JP
                if 0 <= imm16 < 0x8000:
                    targets.add((bank if 0x4000 <= imm16 < 0x8000 else 0, imm16))

        elif ln == 2 and op in (0x18, 0x20, 0x28, 0x30, 0x38):  # JR
            offset = data[pc + 1]
            if offset >= 128:
                offset -= 256
            target = cpu_addr + 2 + offset
            if base_addr <= target < base_addr + bank_size:
                targets.add((bank, target))

        pc += ln

    return targets


def parse_bank_range(spec, max_bank):
    if spec.lower() == 'all':
        return list(range(max_bank))
    banks = set()
    for part in spec.split(','):
        part = part.strip()
        if '-' in part:
            lo, hi = part.split('-', 1)
            banks.update(range(int(lo), int(hi) + 1))
        else:
            banks.add(int(part))
    return sorted(b for b in banks if 0 <= b < max_bank)


def harvest_stack(pyboy, visited, rom_data, max_depth=16):
    """Read the call stack from SP upward, extracting return addresses.
    Each return address on the stack was pushed by a CALL instruction,
    so we can infer both the return site and the call site."""
    try:
        sp = pyboy.register_file.SP
        bank = pyboy.memory[0xFFB8]

        for i in range(max_depth):
            addr = sp + i * 2
            if addr + 1 > 0xFFFE:
                break
            lo = pyboy.memory[addr]
            hi = pyboy.memory[addr + 1]
            ret_addr = (hi << 8) | lo

            # Return addresses point into ROM (0x0000-0x7FFF)
            if ret_addr < 0x4000:
                visited.add((0, ret_addr))
                # The CALL that pushed this was 3 bytes before
                call_addr = ret_addr - 3
                if 0 <= call_addr < 0x4000:
                    visited.add((0, call_addr))
            elif 0x4000 <= ret_addr < 0x8000:
                # Could be current bank or any bank - use hLoadedROMBank
                visited.add((bank, ret_addr))
                call_addr = ret_addr - 3
                if 0x4000 <= call_addr < 0x8000:
                    visited.add((bank, call_addr))
    except Exception:
        pass


def sample_pc(pyboy, visited):
    """Sample current PC and bank, add to visited set."""
    try:
        pc = pyboy.register_file.PC
        if 0x4000 <= pc < 0x8000:
            bank = pyboy.memory[0xFFB8]
        elif pc < 0x4000:
            bank = 0
        else:
            bank = 0
        visited.add((bank, pc))
        return bank, pc
    except Exception:
        return 0, 0


class HookContext:
    """Shared state for hook callbacks."""
    __slots__ = ['visited', 'pyboy', 'fire_count']

    def __init__(self, pyboy_inst):
        self.visited = set()
        self.pyboy = pyboy_inst
        self.fire_count = 0


def make_hook_callback(hook_bank, hook_addr):
    """Create a callback that harvests PC, bank, and stack on each fire."""
    def cb(ctx):
        ctx.fire_count += 1
        ctx.visited.add((hook_bank, hook_addr))
        # Read current state
        try:
            pc = ctx.pyboy.register_file.PC
            if 0x4000 <= pc < 0x8000:
                bank = ctx.pyboy.memory[0xFFB8]
            else:
                bank = 0
            ctx.visited.add((bank, pc))

            # Harvest stack
            sp = ctx.pyboy.register_file.SP
            for i in range(8):
                addr = sp + i * 2
                if addr + 1 > 0xFFFE:
                    break
                lo = ctx.pyboy.memory[addr]
                hi = ctx.pyboy.memory[addr + 1]
                ret_addr = (hi << 8) | lo
                if ret_addr < 0x4000:
                    ctx.visited.add((0, ret_addr))
                elif 0x4000 <= ret_addr < 0x8000:
                    ctx.visited.add((bank, ret_addr))
        except Exception:
            pass
    return cb


def main():
    parser = argparse.ArgumentParser(
        description="Dense execution trace via PyBoy hooks + stack harvesting"
    )
    parser.add_argument("rom", help="Path to ROM file")
    parser.add_argument("-o", "--output", default="dense_trace.txt",
                        help="Output trace file")
    parser.add_argument("-f", "--frames", type=int, default=9000,
                        help="Frames to run (default: 9000)")
    parser.add_argument("--banks", default="all",
                        help="Banks for static analysis: 'all', '0-7', etc.")
    parser.add_argument("--input",
                        help="Input script (frame:button[:duration] per line)")
    parser.add_argument("--speed", type=int, default=0,
                        help="Emulation speed (0=unlimited)")
    parser.add_argument("--window", action="store_true",
                        help="Show emulator window")
    parser.add_argument("--progress", type=int, default=300,
                        help="Progress interval in frames (0=off)")
    parser.add_argument("--save",
                        help="Save state to load before tracing")
    parser.add_argument("--stack-depth", type=int, default=16,
                        help="Stack frames to harvest per sample (default: 16)")
    args = parser.parse_args()

    if not os.path.exists(args.rom):
        print(f"Error: ROM not found: {args.rom}", file=sys.stderr)
        return 1

    sys.stdout.reconfigure(line_buffering=True)

    with open(args.rom, "rb") as f:
        rom_data = f.read()

    rom_size = len(rom_data)
    num_banks = rom_size // 0x4000
    print(f"ROM: {args.rom} ({rom_size} bytes, {num_banks} banks)")

    # --- Phase 1: Static analysis ---
    # Find bank-switch sites in bank 0
    bankswitch_sites = find_bankswitch_sites(rom_data)
    print(f"Bank-switch sites (LDH [FFB8],A): {len(bankswitch_sites)}")

    # Standard hook points
    hook_addrs = set()
    # RST vectors
    for v in [0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38]:
        hook_addrs.add((0, v))
    # Interrupt vectors
    for v in [0x40, 0x48, 0x50, 0x58, 0x60]:
        hook_addrs.add((0, v))
    # Entry points
    hook_addrs.add((0, 0x0100))
    hook_addrs.add((0, 0x0150))
    # Bank-switch sites
    for addr in bankswitch_sites:
        hook_addrs.add((0, addr))

    # Extract CALL/JP targets from all requested banks (static analysis)
    analysis_banks = parse_bank_range(args.banks, num_banks)
    static_targets = set()
    for bank in analysis_banks:
        targets = extract_branch_targets(rom_data, bank)
        static_targets.update(targets)
    print(f"Static branch targets: {len(static_targets)} across "
          f"{len(analysis_banks)} banks")

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

    # --- Phase 2: Emulation with hooks ---
    window_type = "SDL2" if args.window else "null"
    print(f"Initializing PyBoy (window={window_type})...")

    from pyboy import PyBoy
    with PyBoy(args.rom, window=window_type) as pyboy:
        pyboy.set_emulation_speed(args.speed)

        if args.save and os.path.exists(args.save):
            with open(args.save, "rb") as sf:
                pyboy.load_state(sf)
            print(f"Loaded save state: {args.save}")

        ctx = HookContext(pyboy)

        # Register strategic hooks (only ~100 total)
        registered = 0
        skipped = 0
        for bank, addr in sorted(hook_addrs):
            try:
                pyboy.hook_register(bank, addr,
                                    make_hook_callback(bank, addr), ctx)
                registered += 1
            except Exception:
                skipped += 1
        print(f"Hooks: {registered} registered, {skipped} skipped")

        # Run emulation
        print(f"Running {args.frames} frames...")
        t0 = time.time()
        last_count = 0

        for frame in range(args.frames):
            if frame in input_events:
                for btn, dur in input_events[frame]:
                    pyboy.button(btn, dur)

            if not pyboy.tick(render=(args.window and frame >= args.frames - 60)):
                print(f"PyBoy quit at frame {frame}")
                break

            # Per-frame: sample PC + harvest stack
            sample_pc(pyboy, ctx.visited)
            harvest_stack(pyboy, ctx.visited, rom_data, args.stack_depth)

            if args.progress and frame % args.progress == 0 and frame > 0:
                count = len(ctx.visited)
                elapsed = time.time() - t0
                fps = frame / elapsed if elapsed > 0 else 0
                print(f"  Frame {frame}/{args.frames}: "
                      f"{count} unique (+{count - last_count}), "
                      f"{ctx.fire_count} hook fires, {fps:.0f} fps")
                last_count = count

        elapsed = time.time() - t0
        fps = args.frames / elapsed if elapsed > 0 else 0
        print(f"Completed {args.frames} frames in {elapsed:.1f}s ({fps:.0f} fps)")
        print(f"Hook fires: {ctx.fire_count}")

    # --- Phase 3: Merge with static analysis ---
    # The static targets tell us about branch destinations even if they
    # weren't reached during this run. We only include them if their SOURCE
    # bank was active (i.e., we visited at least one address in that bank).
    visited_banks = {bank for bank, addr in ctx.visited}
    merged = set(ctx.visited)

    # Add static targets only for visited banks
    static_added = 0
    for bank, addr in static_targets:
        if bank in visited_banks:
            if (bank, addr) not in merged:
                merged.add((bank, addr))
                static_added += 1

    print(f"Dynamic trace: {len(ctx.visited)} addresses")
    print(f"Static targets added: {static_added}")
    print(f"Total merged: {len(merged)} unique (bank, addr) pairs")

    # Filter to ROM addresses only (0x0000-0x7FFF)
    rom_pairs = sorted((b, a) for b, a in merged if a < 0x8000)

    with open(args.output, "w") as f:
        for bank, addr in rom_pairs:
            f.write(f"{bank}:{addr:04x}\n")

    print(f"\nTrace written to {args.output} ({len(rom_pairs)} entries)")

    bank_counts = {}
    for bank, addr in rom_pairs:
        bank_counts[bank] = bank_counts.get(bank, 0) + 1
    for bank in sorted(bank_counts):
        print(f"  Bank {bank:2d}: {bank_counts[bank]:5d} addresses")

    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
