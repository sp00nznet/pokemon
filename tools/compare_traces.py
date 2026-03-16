#!/usr/bin/env python3
"""
Compare PyBoy ground truth trace against gb-recompiled hwtrace output.

Finds the first frame where hardware state diverges and reports what changed.

Usage:
    python compare_traces.py pyboy_trace.log recomp_trace.log
"""
import sys
import re
import argparse

def parse_trace(filename):
    """Parse a trace file into per-frame records."""
    frames = {}
    with open(filename) as f:
        for line in f:
            line = line.strip()
            if line.startswith('#') or not line:
                continue

            if line.startswith('VBLANK '):
                m = re.match(r'VBLANK FRAME:(\d+) (.+)', line)
                if m:
                    fnum = int(m.group(1))
                    if fnum not in frames:
                        frames[fnum] = {}
                    for kv in m.group(2).split():
                        if '=' in kv:
                            k, v = kv.split('=', 1)
                            frames[fnum][k] = v

            elif line.startswith('REGS '):
                m = re.match(r'REGS FRAME:(\d+) (.+)', line)
                if m:
                    fnum = int(m.group(1))
                    if fnum not in frames:
                        frames[fnum] = {}
                    for kv in m.group(2).split():
                        if '=' in kv:
                            k, v = kv.split('=', 1)
                            frames[fnum][k] = v

    return frames

def main():
    parser = argparse.ArgumentParser(description="Compare hardware traces")
    parser.add_argument("reference", help="PyBoy ground truth trace")
    parser.add_argument("recompiled", help="Recompiled hwtrace output")
    parser.add_argument("--max-diffs", type=int, default=20, help="Max divergences to report")
    parser.add_argument("--fields", default="WRAM,OAM_CRC,TMAP0_CRC,TDATA_CRC,AF,BC,DE,HL,SP,PC,BANK",
                        help="Comma-separated fields to compare")
    parser.add_argument("--offset", type=int, default=0,
                        help="Frame offset: recompiled_frame = ref_frame - offset")
    args = parser.parse_args()

    ref = parse_trace(args.reference)
    rec = parse_trace(args.recompiled)
    fields = args.fields.split(',')

    if not ref:
        print(f"Error: No frames in {args.reference}")
        return 1
    if not rec:
        print(f"Error: No frames in {args.recompiled}")
        return 1

    max_frame = min(max(ref.keys()), max(rec.keys()))
    min_frame = max(min(ref.keys()), min(rec.keys()))

    print(f"Reference: {len(ref)} frames, Recompiled: {len(rec)} frames")
    print(f"Comparing frames {min_frame}-{max_frame}")
    print(f"Fields: {', '.join(fields)}")
    print()

    diffs = 0
    first_diff_frame = None
    match_count = 0

    for frame in range(min_frame, max_frame + 1):
        rec_frame = frame - args.offset
        if frame not in ref or rec_frame not in rec:
            continue

        r = ref[frame]
        c = rec[rec_frame]
        frame_diffs = []

        for field in fields:
            rv = r.get(field, '?')
            cv = c.get(field, '?')
            if rv != cv:
                frame_diffs.append((field, rv, cv))

        if frame_diffs:
            if first_diff_frame is None:
                first_diff_frame = frame
            diffs += 1
            if diffs <= args.max_diffs:
                print(f"DIVERGENCE at frame {frame}:")
                for field, rv, cv in frame_diffs:
                    print(f"  {field}: ref={rv} recomp={cv}")
                # Show context: what was the same
                same = [f for f in fields if r.get(f) == c.get(f) and f in r]
                if same:
                    print(f"  (matching: {', '.join(same)})")
                print()
        else:
            match_count += 1

    print(f"=== Summary ===")
    print(f"Matching frames: {match_count}")
    print(f"Divergent frames: {diffs}")
    if first_diff_frame is not None:
        print(f"First divergence: frame {first_diff_frame}")
    else:
        print("No divergences found - traces match!")

    return 0 if diffs == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
