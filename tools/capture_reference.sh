#!/bin/bash
# capture_reference.sh - Capture reference frames from BGB for comparison
#
# Usage: ./tools/capture_reference.sh [frame1 frame2 ...]
#   Default frames: 420 620 660 900 1200
#
# Captures frames at key game progression points using BGB in headless mode.
# Button inputs match the verified test sequence:
#   Frame 420: Start (title screen)
#   Frame 620: A (main menu)
#
# Prerequisites:
#   - BGB downloaded to tools/bgb/bgb64.exe
#     (download from https://bgb.bircd.org/bgbw64.zip)
#   - gen_bgb_demo built:
#     cmake --build build --target gen_bgb_demo --config Release
#   - ROM at: roms/Pokemon Red Version.gb

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BGB_DIR="$ROOT/tools/bgb"
BGB="$BGB_DIR/bgb64.exe"
DEMO_GEN="$ROOT/build/Release/gen_bgb_demo.exe"
ROM="$ROOT/roms/Pokemon Red Version.gb"
OUTDIR="$ROOT/reference_frames"

# Check prerequisites
if [ ! -f "$BGB" ]; then
    echo "ERROR: BGB not found at $BGB"
    echo "Download from https://bgb.bircd.org/bgbw64.zip and extract to tools/bgb/"
    exit 1
fi

if [ ! -f "$DEMO_GEN" ]; then
    echo "ERROR: gen_bgb_demo not found. Build with:"
    echo "  cmake --build build --target gen_bgb_demo --config Release"
    exit 1
fi

if [ ! -f "$ROM" ]; then
    echo "ERROR: ROM not found at $ROM"
    exit 1
fi

mkdir -p "$OUTDIR"

# Use command-line frames or defaults
if [ $# -gt 0 ]; then
    FRAMES=("$@")
else
    FRAMES=(420 620 660 900 1200)
fi

echo "=== Reference Frame Capture ==="
echo "ROM: $ROM"
echo "Output: $OUTDIR"
echo "Frames: ${FRAMES[*]}"
echo ""

# BGB must run from its own directory
ORIG_DIR="$(pwd)"
cd "$BGB_DIR"

for target_frame in "${FRAMES[@]}"; do
    echo "--- Capturing frame $target_frame ---"

    DEM_FILE="$OUTDIR/demo_${target_frame}.dem"
    BMP_FILE="$OUTDIR/ref_frame_${target_frame}.bmp"

    # Build demo with correct inputs for this frame range
    DEMO_ARGS=""
    if [ "$target_frame" -ge 420 ]; then
        DEMO_ARGS="$DEMO_ARGS 420:start"
    fi
    if [ "$target_frame" -ge 620 ]; then
        DEMO_ARGS="$DEMO_ARGS 620:a"
    fi

    "$DEMO_GEN" "$DEM_FILE" "$target_frame" $DEMO_ARGS

    # Run BGB headlessly with demo, capture screenshot on exit
    ./bgb64.exe "$ROM" -hf -demoplay "$DEM_FILE" -screenonexit "$BMP_FILE" 2>/dev/null || true

    # Clean up demo file
    rm -f "$DEM_FILE"

    if [ -f "$BMP_FILE" ]; then
        echo "  Captured: ref_frame_${target_frame}.bmp"
    else
        echo "  WARNING: Failed to capture frame $target_frame"
    fi
    echo ""
done

cd "$ORIG_DIR"

echo "=== Done ==="
echo ""
echo "Reference frames saved to: $OUTDIR/"
echo ""
echo "To capture matching frames from our recompiler:"
echo "  ./build/Release/pokemon_red.exe --dump-frames $(IFS=,; echo "${FRAMES[*]}") --stop-at $((${FRAMES[-1]} + 10))"
