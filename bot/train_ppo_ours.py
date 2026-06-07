"""Real PPO training of the PokemonRedExperiments agent against OUR recompiled
Red engine (rom_headless.dll via pyboy_shim), not PyBoy.

Mirrors PWhiddy's baseline_fast_v2 hyperparameters (MultiInputPolicy, gamma
0.997, ent_coef 0.01, n_epochs 1) but:
  * swaps PyBoy for PyBoyShim (the bot drives our engine),
  * uses our own start-state snapshot (red_start.gbromstate),
  * uses SubprocVecEnv so each env runs in a SEPARATE PROCESS -- required
    because our DLL's joypad state (g_joypad_buttons/dpad) is process-global,
    so two envs in one process would clash. One env per process = isolated.

Config via env vars: N_ENVS (default 8), TOTAL_STEPS (default 5_000_000),
EP_LEN (per-episode max steps, default 20480), DEVICE (default cpu).

Run:  python bot/train_ppo_ours.py
TensorBoard:  tensorboard --logdir bot/train_out
"""
import os
import sys
from pathlib import Path

BOT = os.path.dirname(os.path.abspath(__file__))
V2 = os.path.join(BOT, "upstream", "v2")
ROOT = os.path.dirname(BOT)
sys.path.insert(0, BOT)
sys.path.insert(0, V2)

GB_PATH = os.path.join(ROOT, "red", "rom.gb")
# Start state is overridable so the extended run can train from the richer
# "has a starter Pokemon" snapshot instead of the empty-party bedroom one.
INIT_STATE = os.environ.get("START_STATE") or os.path.join(BOT, "red_start.gbromstate")
SESS = Path(os.environ.get("TRAIN_OUT") or os.path.join(BOT, "train_out"))

EP_LEN = int(os.environ.get("EP_LEN", "20480"))


def env_config(rank):
    return {
        "session_path": SESS,
        "save_final_state": False,
        "print_rewards": False,
        "headless": True,
        "init_state": INIT_STATE,
        "action_freq": 24,
        "max_steps": EP_LEN,
        "save_video": False,
        "fast_video": False,
        "gb_path": GB_PATH,
        "explore_weight": 0.25,
        "reward_scale": 0.5,
        "instance_id": f"ours_{rank}",
    }


def make_env(rank, seed=0):
    """Factory run INSIDE each subprocess: apply the PyBoy->PyBoyShim monkeypatch
    there (subprocess globals don't inherit), build the real RedGymEnv."""
    def _init():
        os.chdir(V2)  # RedGymEnv opens events.json / map_data.json relatively
        from pyboy_shim import PyBoyShim
        import red_gym_env_v2 as envmod
        envmod.PyBoy = PyBoyShim
        env = envmod.RedGymEnv(env_config(rank))
        env.reset(seed=seed + rank)
        return env
    return _init


def main():
    from stable_baselines3 import PPO
    from stable_baselines3.common.vec_env import SubprocVecEnv
    from stable_baselines3.common.callbacks import CheckpointCallback

    n_envs = int(os.environ.get("N_ENVS", "8"))
    total_steps = int(os.environ.get("TOTAL_STEPS", "5000000"))
    device = os.environ.get("DEVICE", "cpu")
    SESS.mkdir(parents=True, exist_ok=True)

    print(f"PPO training on recompiled Red | n_envs={n_envs} "
          f"ep_len={EP_LEN} total_steps={total_steps} device={device}")
    env = SubprocVecEnv([make_env(i) for i in range(n_envs)])

    n_steps = max(512, EP_LEN // n_envs)
    ckpt = CheckpointCallback(save_freq=n_steps * 4, save_path=str(SESS),
                              name_prefix="poke_ours")
    model = PPO(
        "MultiInputPolicy", env, verbose=1,
        n_steps=n_steps, batch_size=512, n_epochs=1,
        gamma=0.997, ent_coef=0.01,
        tensorboard_log=str(SESS), device=device,
    )
    print(model.policy)
    model.learn(total_timesteps=total_steps, callback=ckpt,
                tb_log_name="poke_ppo_ours")
    model.save(str(SESS / "poke_ours_final"))
    print("training finished; model saved to", SESS / "poke_ours_final.zip")


if __name__ == "__main__":
    main()
