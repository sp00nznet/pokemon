# Pokemon Red - Static Recompilation

> *What if we just... turned the whole ROM into C and ran it natively?*

A static recompiler that translates the Pokemon Red Game Boy ROM (SM83 CPU) into native C code, then runs it with a hardware abstraction layer and SDL2 frontend. No interpreter loop. No JIT. Just 64 banks of generated C functions that think they're running on a Game Boy.

## The Idea

The Game Boy CPU (Sharp SM83) has a finite ROM. Every instruction has a fixed address. So instead of emulating one opcode at a time, we can analyze the entire ROM ahead of time, discover every function and basic block, and generate equivalent C code. At runtime, the generated code calls through a HAL that provides the memory bus, PPU, APU, timers, and everything else the game expects to find.

It's like a whole-program decompilation, except the "decompiler" is a robot that doesn't understand what the code *does* -- it just faithfully translates what it *is*.

### Recompiler Pipeline

```
ROM binary --> Decoder (SM83 instructions)
           --> Analyzer (functions, basic blocks, control flow graph)
           --> Codegen (C source per bank + dispatch table)
```

Each ROM function becomes a C function (`func_b01_42B7`). Branches become `goto`. Calls become C calls. Hardware ops go through the HAL. Interrupts are checked cooperatively at basic block boundaries and HALT instructions.

## How Far Does It Get?

**Pretty far.** The full intro sequence plays with graphics, the title screen works, and you can get into the overworld and start playing:

| Copyright | Game Freak Intro | Title Sequence |
|:---:|:---:|:---:|
| ![Copyright](screenshots/01_copyright.png) | ![Intro](screenshots/02_intro_pokemon.png) | ![Title](screenshots/03_title.png) |

| Nidorino vs Gengar | Battle Intro | Oak's Lab |
|:---:|:---:|:---:|
| ![Menu](screenshots/04_mainmenu.png) | ![Nidorino](screenshots/05_nidorino.png) | ![Oak](screenshots/06_oak_intro.png) |

**Current progression:** Copyright -> Game Freak -> Nidorino battle -> cycling Pokemon -> title screen -> main menu -> NEW GAME -> Oak's intro -> name entry -> overworld (Pallet Town) -> Oak's Route 1 script (in progress)

Zero dispatch errors. Zero stub fallbacks. The generated code handles it all.

## Building

### Prerequisites
- CMake 3.16+
- MSVC 2022 (Windows) or GCC/Clang
- SDL2 (via vcpkg recommended)
- A Pokemon Red ROM file (`roms/Pokemon Red Version.gb`) -- you supply your own

### Steps

```bash
# Configure
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build the recompiler
cmake --build build --target recompiler --config Release

# Feed it the ROM (generates ~64 C files + dispatch table)
./build/tools/recompiler/Release/recompiler.exe "roms/Pokemon Red Version.gb" -o src/generated

# Reconfigure (picks up the freshly generated source files)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build the game
cmake --build build --target pokemon_red --config Release

# Play
./build/Release/pokemon_red.exe "roms/Pokemon Red Version.gb"
```

## Controls

| Key | Action |
|-----|--------|
| **Z** | A button |
| **X** | B button |
| **Enter** | START |
| **Backspace** | SELECT |
| **Arrow keys** | D-Pad |
| **Tab** (hold) | Fast-forward (skip frame limiter) |
| **F5** | Save state |
| **F9** | Load state |
| **F11** | Toggle fullscreen |
| **F12** | Screenshot |
| **M** | Toggle mute |

There's also a **Windows menu bar** (File -> Save/Load State, Config -> Scale 1x-4x) for people who prefer clicking things.

### Automated Testing

```bash
# Press Start at frame 420, A at frame 620, stop at frame 1200
./build/Release/pokemon_red.exe "roms/Pokemon Red Version.gb" \
    --auto-input 420:start 620:a --stop-at 1200 --dump-frames 900,1200
```

## What's Working

**The big stuff:**
- Full ROM analysis and C code generation for all 64 banks
- Complete hardware abstraction: CPU state, memory/MBC3, PPU, APU, timer, joypad, DMA, serial, interrupts
- SDL2 window with pixel-accurate rendering, audio output, and native Win32 menu bar
- Game progression from boot through intro, title, menus, and into the overworld
- Sprite decompression and VBlank-synchronized VRAM transfers
- All 4 audio channels (2x pulse, wave, noise) with proper DAC, envelopes, and Bresenham sample timing
- Save states and SRAM persistence

**The tricky stuff we had to figure out:**
- **Non-local returns (POP trick):** Pokemon Red's sprite decompressor manipulates the stack to return to a different caller. We detect these automatically with a table-driven NLR system in codegen.
- **OAM DMA source address:** The DMA routine copies its source address into HRAM at runtime. We had to read it from HRAM, not the DMA register. Sprites were invisible for a *while*.
- **Instruction boundary tracking:** The analyzer now maintains an `is_inst_start[]` bitmap so we never accidentally split a multi-byte instruction across basic blocks. This fixed a `CALL DelayFrame` that was hiding inside a 3-byte instruction's operand bytes.
- **STAT interrupt edge detection:** Without tracking the previous STAT line, interrupts fired every cycle instead of once per transition. The LCD went haywire.
- **Frame timing:** `SDL_Delay(16)` isn't 59.73 fps. We use performance counters with spin-wait for the sub-millisecond remainder. The game notices if you drift.

## Project Structure

```
tools/recompiler/       SM83-to-C static recompiler
  decoder.c               Instruction decoder (all SM83 opcodes + CB prefix)
  analyzer.c              Function/block discovery, control flow, dispatch seeds
  codegen.c               C code generation, NLR detection, dispatch table
  symbols.c               Symbol table management
  main.c                  Entry point

src/hal/                Game Boy hardware abstraction layer
  cpu.c/h                 CPU state, register file, interrupts, HALT
  memory.c/h              Memory bus, MBC3 banking, I/O registers
  ppu.c/h                 Pixel Processing Unit (scanline renderer)
  apu.c/h                 Audio Processing Unit (4 channels, high-pass filter)
  timer.c/h               Timer/divider with glitch emulation
  interrupts.c/h          Interrupt dispatch and edge detection
  joypad.c/h              Input with action/direction multiplexing
  dma.c/h                 OAM DMA transfers
  serial.c/h              Serial link cable (stub -- no link partner)

src/platform/           SDL2 platform layer
  window.c/h              Window management and scaling
  renderer.c/h            Pixel rendering with palette support
  audio.c/h               Audio output (thread-safe ring buffer)
  input.c/h               Keyboard input and fast-forward
  menu.c/h                Native Win32 menu bar (File, Config)

src/generated/          Auto-generated from ROM (mostly gitignored)
  banks/                  Per-bank C files (one per ROM bank, gitignored)
  dispatch.c/h            Function dispatch table (gitignored)
  stubs.c                 Hand-written stubs for edge cases (committed)
```

## What's Next

- Oak's Route 1 script and wild Pokemon battles
- More dispatch seeds as new code paths are discovered
- Visual accuracy refinements
- Maybe Pokemon Blue and Yellow support (the infrastructure is there, the seeds aren't)

## ROM Files

Place ROM files in `roms/` (gitignored):
- `Pokemon Red Version.gb` -- Primary target, actively tested
- `Pokemon Blue Version.gb` -- Should work (same engine), untested
- `Pokemon Yellow Version - Special Pikachu Edition.gbc` -- MBC5, untested

## License

This project is for educational and research purposes. No ROM data is included -- you must provide your own legally obtained ROM files.
