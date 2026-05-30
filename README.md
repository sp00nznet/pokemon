# Pokémon — Statically Recompiled

> *Generation I Pokémon as native executables. No emulator. No ROM interpretation at runtime. Just SM83 assembly translated, ahead of time, into portable C.*

This repository ports **Generation I Pokémon** — Yellow, Red, Blue — to native code via [**gb-recompiled**](https://github.com/sp00nznet/gb-recompiled): a static recompiler that lifts SM83 (Z80-ish) machine code directly into C. Each game builds into its own native `rom.exe`. No emulator in the loop, only a small fallback interpreter for indirect jumps the static analyser couldn't resolve.

Sibling projects on the same toolchain: [`LinksAwakening`](https://github.com/sp00nznet/LinksAwakening), [`pokemon-gold`](https://github.com/sp00nznet/pokemon-gold), [`pokemon-crystal`](https://github.com/sp00nznet/pokemon-crystal), [`oracle-recompiled`](https://github.com/sp00nznet/oracle-recompiled).

---

## Status

| Game | Cart | Color | Status |
|---|---|---|---|
| **Pokémon Yellow** | MBC5, GBC | CGB palettes | ✅ Boots; GAME FREAK + Pikachu intro render with correct palettes |
| **Pokémon Red** | MBC3, DMG | Mono | ⏳ Bring-up |
| **Pokémon Blue** | MBC3, DMG | Mono | ⏳ Bring-up |

---

## Layout

```
pokemon/
├── gbrecomp/   submodule → sp00nznet/gb-recompiled (the recompiler + runtime)
├── roms/       your legal ROMs (gitignored)
├── tools/      PyBoy diagnostic scripts + input scripts
├── yellow/     Yellow build dir (rom_main.c + CMakeLists committed; rom.c/rom_rom.c regenerated)
├── red/        Red, WIP
├── blue/       Blue, WIP
└── screenshots/
```

Each game directory is self-contained, matching the `pokemon-gold` convention: tracked `rom_main.c` + `CMakeLists.txt`, generated `rom.c` / `rom.h` / `rom_rom.c` (gitignored, regenerated each recompile).

---

## Building

### Prerequisites

- **Windows 11**: Visual Studio 2022 Build Tools (C++ workload), CMake ≥ 3.20, SDL2 via [vcpkg](https://github.com/microsoft/vcpkg) (`vcpkg install sdl2:x64-windows`).
- **Python 3.11+** with `pyboy` (`pip install pyboy`) — for the ground-truth trace capture.
- A legally obtained ROM placed in `roms/`.

### Clone

```bash
git clone --recurse-submodules https://github.com/sp00nznet/pokemon.git
cd pokemon
```

### Build the gb-recompiled recompiler (once)

```bash
cmake -S gbrecomp -B gbrecomp/build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build gbrecomp/build --target gbrecomp --config Release
```

That produces `gbrecomp/build/bin/Release/gbrecomp.exe`.

### Bring up a game (Yellow shown — Red/Blue are identical with their own ROM)

```bash
# 1) point the recompiler at a rom-named copy so output files are rom.{c,h,_rom.c}
cp "roms/Pokemon Yellow Version - Special Pikachu Edition.gbc" yellow/rom.gbc

# 2) capture a PyBoy ground-truth execution trace
python gbrecomp/tools/capture_ground_truth.py yellow/rom.gbc \
    -o yellow/rom.trace -f 18000 --random

# 3) recompile (writes rom.c, rom.h, rom_main.c, rom_rom.c)
gbrecomp/build/bin/Release/gbrecomp.exe yellow/rom.gbc \
    -o yellow --use-trace yellow/rom.trace

# 4) configure + build
cmake -S yellow -B yellow/build \
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build yellow/build --config Release

# 5) run
./yellow/build/Release/rom.exe
```

Useful flags on `rom.exe` (from gbrecomp's runtime):

| Flag | Effect |
|---|---|
| `--dump-frames "100,400,900"` | dump frames at the listed numbers as `screenshot_NNNNN.ppm` |
| `--limit N` | cap total instructions (handy for headless smoke tests) |
| `--input FILE` | replay a recorded button script |
| `--trace`, `--trace-entries FILE` | dump every translated instruction address that executed |

---

## How it works

```
ROM (.gb/.gbc)               PyBoy ground truth                gbrecomp
       │                            │                              │
       ▼                            ▼                              ▼
[2 MB SM83 machine code]  +  [proven entry-points trace]  →  [~66 MB native C]
                                                                    │
                                                                    ▼
                                                          cmake / MSVC + SDL2
                                                                    │
                                                                    ▼
                                                  rom.exe (statically recompiled)
                                                  ├─ translated game code
                                                  ├─ gbrt runtime (PPU, APU, MBCs, save)
                                                  └─ SDL2 + Dear ImGui debug overlay
```

The `--use-trace` step is the key: it seeds the recompiler with every `(bank, addr)` PyBoy actually executed, solving the `JP (HL)` / PUSH-ret trampoline patterns Pokémon uses for predef / farcall / jump-table dispatch — control flow the static analyser cannot follow on its own.

See [`gbrecomp/GROUND_TRUTH_WORKFLOW.md`](https://github.com/sp00nznet/gb-recompiled/blob/main/GROUND_TRUTH_WORKFLOW.md) for the detailed flow.

---

## Notes

- ROMs are **never** committed. Provide your own legal copy under `roms/`.
- Each game's `rom.c` (≈ 66 MB for Yellow) and `rom_rom.c` (≈ 6.6 MB) are generated and gitignored; only `rom_main.c` and `CMakeLists.txt` are tracked per game.
- Editing the auto-generated `CMakeLists.txt` is fine — re-running the recompiler overwrites it, so re-apply any local tweaks afterwards (mostly `GBRT_DIR`, `LA_HAS_IMGUI`, `GB_RECOMPILED_DISPATCH`).
- `tools/pyboy_*.py` are project-specific PyBoy diagnostic scripts kept from the Yellow GAME FREAK palette debugging session.

---

## License

MIT. The runtime (`gbrecomp/runtime/`) is MIT via the gb-recompiled submodule. ROMs are © Nintendo / Game Freak / Creatures — bring your own.
