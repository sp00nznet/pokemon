"""Phase D: run the ACTUAL PokemonRedExperiments env (red_gym_env_v2.RedGymEnv)
against our recompiled engine.

We monkeypatch the env's `PyBoy` symbol to our `PyBoyShim`, feed it our own
start-state snapshot (PyBoy .state is incompatible), and drive it with a policy
so the bot literally plays our recompiled Red. Logs reward and dumps periodic
screenshots proving the agent moves around inside our engine.

Run: python bot/run_phase_d.py [steps] [--agent random|explore]
"""
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image

BOT = os.path.dirname(os.path.abspath(__file__))
V2 = os.path.join(BOT, "upstream", "v2")
ROOT = os.path.dirname(BOT)
sys.path.insert(0, BOT)
sys.path.insert(0, V2)

from pyboy_shim import PyBoyShim  # noqa: E402

STEPS = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
AGENT = "explore"
if "--agent" in sys.argv:
    AGENT = sys.argv[sys.argv.index("--agent") + 1]

GB_PATH = os.path.join(ROOT, "red", "rom.gb")
INIT_STATE = os.path.join(BOT, "red_start.gbromstate")
OUT = os.path.join(BOT, "phase_d_out")
os.makedirs(OUT, exist_ok=True)


def main():
    os.chdir(V2)  # env opens events.json / map_data.json by relative path
    import red_gym_env_v2 as envmod
    envmod.PyBoy = PyBoyShim  # <-- the bot now drives our recompiled engine

    config = {
        "session_path": Path(OUT),
        "save_final_state": False,
        "print_rewards": False,
        "headless": True,
        "init_state": INIT_STATE,
        "action_freq": 24,
        "max_steps": STEPS,
        "save_video": False,
        "fast_video": False,
        "gb_path": GB_PATH,
        "explore_weight": 1.0,
        "reward_scale": 1.0,
        "instance_id": "phase_d",
    }
    env = envmod.RedGymEnv(config)
    obs, _ = env.reset()
    print(f"env reset OK. obs keys: {list(obs.keys())}")
    print(f"start: map=0x{env.read_m(0xD35E):02X} "
          f"pos=({env.read_m(0xD362)},{env.read_m(0xD361)})")

    rng = np.random.default_rng(0)
    n_actions = env.action_space.n
    DOWN, LEFT, RIGHT, UP, A, B, START = range(7)
    total_reward = 0.0
    visited = set()
    shots = 0
    for step in range(STEPS):
        if AGENT == "explore":
            # bias toward movement so the agent actually wanders our overworld
            a = rng.choice([DOWN, LEFT, RIGHT, UP, A],
                           p=[0.24, 0.24, 0.24, 0.24, 0.04])
        else:
            a = rng.integers(0, n_actions)
        obs, reward, done, trunc, info = env.step(int(a))
        total_reward += reward
        visited.add((env.read_m(0xD35E), env.read_m(0xD362), env.read_m(0xD361)))
        if step % 250 == 0 or step == STEPS - 1:
            fb = env.pyboy.screen.ndarray[:, :, :3]
            Image.fromarray(fb, "RGB").save(
                os.path.join(OUT, f"phase_d_{step:05d}.png"))
            shots += 1
            print(f"  step {step:5d}: map=0x{env.read_m(0xD35E):02X} "
                  f"pos=({env.read_m(0xD362)},{env.read_m(0xD361)}) "
                  f"reward_total={total_reward:8.3f} "
                  f"unique_tiles={len(visited)}")
        if done:
            print(f"  done at step {step}")
            break

    print("\n===== PHASE D RESULT =====")
    print(f"steps run: {step+1}")
    print(f"unique (map,x,y) tiles visited: {len(visited)}")
    print(f"distinct maps entered: {sorted(set(m for m,_,_ in visited))}")
    print(f"final cumulative reward: {total_reward:.3f}")
    print(f"screenshots: {shots} in {OUT}")
    env.pyboy.stop(save=False)


if __name__ == "__main__":
    main()
