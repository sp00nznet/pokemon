# RL bot integration — driving recompiled Red

This directory connects [PWhiddy's **PokemonRedExperiments**](https://github.com/PWhiddy/PokemonRedExperiments)
(a PyBoy-based reinforcement-learning Pokémon player) to our **statically
recompiled** Pokémon Red, so the bot can play and test the recompiled engine
instead of an emulator.

The bot only touches six PyBoy entry points, and every one maps 1:1 onto a
primitive the `gbrt` runtime already exports. So we build the recompiled engine
as a headless shared library, wrap it in a PyBoy-compatible Python class, and
hand that to the env.

```
PokemonRedExperiments RedGymEnv  (unmodified)
        │   uses: PyBoy(path), .tick, .send_input, .memory[a], .screen.ndarray, .load_state
        ▼
bot/pyboy_shim.py  (PyBoyShim — ctypes, PyBoy-2.x-compatible)
        │   gbrom_create / step / read / write / set_buttons / framebuffer / snapshot / restore
        ▼
red/build/Release/rom_headless.dll   (recompiled Red, no SDL/ImGui)
        │
        ▼
gbrt runtime (CPU + PPU + MBC) running translated SM83 → C
```

---

## The headless DLL

`red/CMakeLists.txt` builds a second target, **`rom_headless.dll`**, alongside
the SDL `rom.exe`:

- **`gbrt_headless`** — the core runtime (`gbrt.c`, `ppu.c`, `audio.c`,
  `interpreter.c`, `hwtrace.c`) compiled **without** `GB_HAS_SDL2`/ImGui, plus
  `red/platform_headless.c`, which supplies the only two platform symbols the
  core references directly: `g_joypad_buttons` and `g_joypad_dpad`.
- **`rom_headless`** — `rom.c` + `rom_rom.c` (the translated game) + the bridge.

`red/rom_bridge.c` exports a stable C ABI (each call wraps an existing gbrt
primitive — no game logic lives there):

| Export | Wraps | Notes |
|---|---|---|
| `gbrom_create()` | `gb_context_create(DMG)` + `rom_init` | returns opaque `GBContext*` |
| `gbrom_step(ctx, n)` | `gb_run_frame` ×n | one call = one rendered frame |
| `gbrom_read/write(ctx, addr[,v])` | `gb_read8` / `gb_write8` | |
| `gbrom_set_buttons(ctx, dpad, btn)` | writes joypad globals | active-low masks (below) |
| `gbrom_framebuffer(ctx, out)` | `gb_get_framebuffer` | decodes ARGB → 160·144·3 RGB |
| `gbrom_snapshot/restore(ctx, blob)` | memcpy of WRAM/VRAM/OAM/HRAM/IO + regs + **GBPPU** | our own save-state format (not PyBoy `.state`) |

Joypad masks are active-low (0 = pressed), matching `platform_sdl.cpp`:
`dpad` bit0 Right, bit1 Left, bit2 Up, bit3 Down; `btn` bit0 A, bit1 B,
bit2 Select, bit3 Start.

Build it:

```bash
cmake --build red/build --target rom_headless --config Release
```

> The snapshot embeds the **whole `GBPPU` struct** (`ly`/`lcdc`/`mode`/
> framebuffer). The runtime keeps PPU state separate from `ctx->io`, so without
> it a restored state renders **blank** until the game happens to resync. The
> snapshot size is therefore tied to the DLL build — **regenerate
> `red_start.gbromstate` after any DLL rebuild** (`make_start_state.py` does
> this and reload-verifies the result).

---

## `pyboy_shim.py` — PyBoy-compatible wrapper

`PyBoyShim` implements exactly the PyBoy 2.x surface the env uses:
`PyBoy(gb_path, window=...)`, `set_emulation_speed` (no-op), `tick(n, render)`,
`button_press/button_release/button/send_input` (incl. `WindowEvent` ints),
`memory[addr]` (with slice support), `screen.ndarray` → `(144,160,3)` uint8,
and `load_state/save_state` backed by `gbrom_snapshot/restore`.
`from pyboy_shim import PyBoy` is a drop-in alias.

---

## Differential testing vs PyBoy

`diff_harness.py` drives stock PyBoy **and** our engine with identical inputs
and compares game state each checkpoint:

- curated reward addresses (party `0xD163`, levels, HP, badges `0xD356`,
  map `0xD35E`, pos `0xD361/2`, …),
- the full WRAM range `0xC000–0xDFFF`,
- the screen **structurally** — each engine's pixels are remapped to a 0–3
  palette index by luminance, so the two engines' different DMG palettes don't
  count as differences.

```bash
python bot/diff_harness.py --frames 2000
```

### Finding: the recompilation is cycle-faithful

The only difference between the engines is an ~80-frame startup offset, and it
is **PyBoy's fault, not ours**: PyBoy runs its own built-in boot splash
(a scrolling "PyBoy" logo) for ~78 frames before the cartridge, while our engine
starts post-bootrom at `PC=0x100`. After phase-aligning (`--ref-head-start`,
default 80), curated game-state addresses are **all identical**, screen match
averages ~95% (dipping only on fast transition frames, then re-converging to
100%), and the persistent WRAM residual is **7 bytes** — six of uninitialized
high-WRAM (benign emulator power-on fill) and one frame/RNG counter off by 2.
**Zero structural/logic divergence.**

Diagnostic scripts used to establish this: `timing_probe.py`,
`phase_align_test.py`, `boot_compare.py`, `diag_divergence.py`.

---

## The bot plays our engine (`run_phase_d.py`)

`run_phase_d.py` monkeypatches `red_gym_env_v2.PyBoy = PyBoyShim`, points the
real `RedGymEnv` at our ROM + start-state, and runs a movement-biased policy.

```bash
python bot/make_start_state.py      # once: produce a controllable-overworld snapshot
python bot/run_phase_d.py 2500      # run the env on our engine, dump screenshots
```

**Start state.** PyBoy `.state` files are incompatible with our snapshot format,
so `make_start_state.py` drives boot → a controllable bedroom by **A + START**
mashing (pure-A gets stuck on the name-entry keyboard; START confirms the name),
confirms real control via a two-direction walk test, saves
`red_start.gbromstate`, and **reload-verifies** that the snapshot renders and the
player moves before accepting it.

In a 2500-step run the agent leaves the bedroom → downstairs (map 37) → Pallet
Town (map 0) → Oak's Lab (map 40), triggers the "Wild POKéMON live in tall
grass!" event, and visits ~140 unique tiles — all reward/observation computed
from our engine (`bot/phase_d_journey.png`).

---

## Watch it (`watch_agent.py`)

Render a **trained** agent (auto-loads the latest PPO checkpoint) playing the
recompiled engine — to an **MP4** by default, plus an optional **live window**.
Each frame is the engine's framebuffer upscaled with a small HUD (step / map /
party / position / reward).

```bash
python bot/watch_agent.py                 # latest checkpoint -> bot/watch_out/agent.mp4
python bot/watch_agent.py --live          # also show a real-time window (press q to quit)
python bot/watch_agent.py --policy random # watch an untrained random policy
python bot/watch_agent.py --checkpoint <path.zip> --start bot/red_start.gbromstate --steps 1200
```

Since the bot drives the **headless** DLL (no window of its own), the script
grabs `screen.ndarray` each step and feeds it to an `imageio`/ffmpeg writer and
(optionally) an OpenCV window — so the same run produces a shareable video and
a live view. Needs `imageio-ffmpeg` (MP4) and `opencv-python` (window). The MP4
and `bot/watch_out/` are gitignored.

---

## Real PPO training (`train_ppo_ours.py`)

Trains the agent with PPO (Stable-Baselines3) using our engine as the backend,
mirroring PWhiddy's hyperparameters (`MultiInputPolicy`, `gamma=0.997`,
`ent_coef=0.01`, `n_epochs=1`).

```bash
python bot/train_ppo_ours.py            # writes checkpoints + TensorBoard to bot/train_out/
tensorboard --logdir bot/train_out
```

Env vars: `N_ENVS` (default 8), `TOTAL_STEPS` (default 5e6), `EP_LEN`
(default 20480), `DEVICE` (default `cpu`).

> **Why SubprocVecEnv (one env per process).** Our DLL reads the joypad from
> **process-global** symbols (`g_joypad_buttons/dpad`), so two envs in one
> process would clash. `SubprocVecEnv` runs each env in its own process → its
> own DLL load → isolated joypad. The `PyBoy→PyBoyShim` monkeypatch is applied
> *inside* each subprocess factory (`make_env`), since subprocess globals don't
> inherit.

> **GPU note.** Training defaults to CPU. The env step (our DLL) is the
> bottleneck, and an RTX 5070 is Blackwell (sm_120) which needs a very recent
> CUDA/torch build; set `DEVICE=cuda` only with a matching torch.

---

## Setup

```bash
pip install pyboy numpy pillow                       # shim + harness
pip install gymnasium scikit-image einops mediapy matplotlib   # to run RedGymEnv
pip install stable_baselines3 torch                  # only for train_ppo_ours.py
pip install imageio-ffmpeg opencv-python             # only for watch_agent.py (video/live)
# clone the env (skip LFS model/state blobs):
GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 \
    https://github.com/PWhiddy/PokemonRedExperiments.git bot/upstream
```

`RedGymEnv` opens `events.json`/`map_data.json` by relative path, so the runner
scripts `chdir` into `bot/upstream/v2`.

---

## File index

| File | Purpose |
|---|---|
| `pyboy_shim.py` | PyBoy-compatible ctypes wrapper over `rom_headless.dll` |
| `diff_harness.py` | differential tester vs stock PyBoy |
| `run_phase_d.py` | run the real `RedGymEnv` on our engine (random/explore policy) |
| `watch_agent.py` | render a trained/random agent to MP4 + optional live window |
| `make_start_state.py` | generate + verify the controllable-overworld start snapshot |
| `train_ppo_ours.py` | PPO training against our engine |
| `timing_probe.py`, `phase_align_test.py`, `boot_compare.py`, `diag_*.py`, `calibrate_newgame.py`, `gameplay_lockstep.py` | diagnostics from the timing investigation |
| `upstream/` | shallow clone of PokemonRedExperiments (gitignored) |
| `red_start.gbromstate` | our start-state snapshot (gitignored; regenerate per DLL build) |
| `phase_d_out/`, `train_out/` | run artifacts (gitignored) |

Generated/large artifacts (`upstream/`, `*.gbromstate`, `phase_d_out/`,
`train_out/`, `*.png`) are gitignored; the Python sources are tracked.
