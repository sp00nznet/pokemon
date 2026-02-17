# Pokemon Static Recompilation - Architecture

## Overview

This project statically recompiles Pokemon Game Boy ROMs (Red, Blue, Yellow) into native
Windows 11 executables. The SM83 CPU instructions are translated to C code at build time,
then compiled with a hardware abstraction layer (HAL) to produce standalone `.exe` files.

## Build Pipeline

```
ROM binary
    |
    v
[Recompiler Tool] (tools/recompiler/)
    |-- decoder.c    : SM83 instruction decoder (512 opcodes)
    |-- analyzer.c   : Control flow analysis, function detection
    |-- codegen.c    : SM83 -> C translation
    |-- symbols.c    : Hardware register names, known addresses
    |
    v
Generated C files (src/generated/banks/bank_XX.c, dispatch.c)
    |
    v
[Game Runtime] (src/)
    |-- hal/         : Hardware Abstraction Layer
    |   |-- cpu.c    : CPU state, register macros
    |   |-- memory.c : 64KB memory map, MBC3/MBC5
    |   |-- ppu.c    : Scanline renderer (160x144)
    |   |-- apu.c    : 4-channel audio
    |   |-- timer.c  : DIV/TIMA/TMA/TAC
    |   |-- joypad.c : Button input
    |   |-- dma.c    : OAM DMA
    |   |-- serial.c : Link cable stub
    |   +-- interrupts.c
    |
    |-- platform/    : SDL2 platform layer
    |   |-- window.c : Window management
    |   |-- renderer.c: Framebuffer display
    |   |-- audio.c  : Audio callback
    |   +-- input.c  : Keyboard mapping
    |
    +-- main.c       : Game entry point, main loop
    |
    v
pokemon_red.exe / pokemon_blue.exe / pokemon_yellow.exe
```

## Key Design Decisions

- **Static recompilation**: ROM code is translated to C at build time, not interpreted
- **C11**: Natural codegen target, fast compilation, universal compiler support
- **SDL2**: Cross-platform media layer for video, audio, input
- **Per-bank C files**: Maps to ROM structure, manageable compilation units
- **Cooperative interrupts**: Checked at HALT and basic block boundaries
- **Cycle-based scheduling**: Avoids per-instruction hardware polling

## Memory Map

| Range         | Size   | Description          |
|---------------|--------|----------------------|
| 0000-3FFF     | 16 KB  | ROM Bank 0           |
| 4000-7FFF     | 16 KB  | ROM Bank N (switchable)|
| 8000-9FFF     | 8 KB   | Video RAM            |
| A000-BFFF     | 8 KB   | External RAM         |
| C000-DFFF     | 8 KB   | Work RAM             |
| E000-FDFF     | -      | Echo RAM             |
| FE00-FE9F     | 160 B  | OAM (sprites)        |
| FF00-FF7F     | 128 B  | I/O Registers        |
| FF80-FFFE     | 127 B  | High RAM             |
| FFFF          | 1 B    | Interrupt Enable     |

## Controls

| Key       | Button  |
|-----------|---------|
| Z         | A       |
| X         | B       |
| Enter     | Start   |
| Backspace | Select  |
| Arrows    | D-pad   |
| Tab       | Fast-forward |
| F11       | Fullscreen |
| M         | Mute    |
