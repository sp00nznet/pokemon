"""Watch the RL agent play our recompiled Pokemon engine.

Runs a trained PPO checkpoint (or a random policy) on RedGymEnv backed by the
headless DLL, captures the engine's framebuffer each step with a small HUD, and
writes a watchable MP4 -- plus an optional live on-screen window (--live).

Examples:
    python bot/watch_agent.py                       # latest checkpoint -> MP4
    python bot/watch_agent.py --live                # also show a live window
    python bot/watch_agent.py --policy random       # watch a random policy
    python bot/watch_agent.py --checkpoint bot/train_out_starter/poke_ours_409600_steps.zip
    python bot/watch_agent.py --start bot/red_start.gbromstate --steps 1200

MP4 -> bot/watch_out/agent.mp4  (override with --out)
"""
import argparse
import glob
import os
import re
import sys
from pathlib import Path

import numpy as np
import cv2
import imageio

BOT = os.path.dirname(os.path.abspath(__file__))
V2 = os.path.join(BOT, "upstream", "v2")
ROOT = os.path.dirname(BOT)
sys.path.insert(0, BOT)
sys.path.insert(0, V2)

GB_PATH = os.path.join(ROOT, "red", "rom.gb")


def latest_checkpoint(train_dir):
    cks = glob.glob(os.path.join(train_dir, "poke_ours_*_steps.zip"))
    if not cks:
        return None
    return max(cks, key=lambda p: int(re.search(r"_(\d+)_steps", p).group(1)))


def hud(rgb_big, lines):
    """Draw a translucent top bar with white text (order-agnostic)."""
    h = 14 * len(lines) + 8
    overlay = rgb_big.copy()
    cv2.rectangle(overlay, (0, 0), (rgb_big.shape[1], h), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.45, rgb_big, 0.55, 0, rgb_big)
    for i, t in enumerate(lines):
        cv2.putText(rgb_big, t, (6, 16 + i * 14), cv2.FONT_HERSHEY_SIMPLEX,
                    0.42, (255, 255, 255), 1, cv2.LINE_AA)
    return rgb_big


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--steps", type=int, default=900)
    ap.add_argument("--policy", choices=["trained", "random"], default="trained")
    ap.add_argument("--checkpoint", default=None, help="path, or auto-latest")
    ap.add_argument("--train-dir", default=os.path.join(BOT, "train_out_starter"))
    ap.add_argument("--start", default=os.path.join(BOT, "red_starter.gbromstate"))
    ap.add_argument("--out", default=os.path.join(BOT, "watch_out", "agent.mp4"))
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--live", action="store_true")
    ap.add_argument("--gif", default=None, help="also write a (clean, decimated) GIF here")
    ap.add_argument("--gif-scale", type=int, default=3)
    ap.add_argument("--gif-frames", type=int, default=160, help="max frames in GIF")
    ap.add_argument("--gif-fps", type=int, default=14)
    args = ap.parse_args()

    os.chdir(V2)  # RedGymEnv opens events.json/map_data.json relatively
    from pyboy_shim import PyBoyShim
    import red_gym_env_v2 as envmod
    envmod.PyBoy = PyBoyShim

    config = {
        "session_path": Path(os.path.join(BOT, "watch_out")),
        "save_final_state": False, "print_rewards": False, "headless": True,
        "init_state": args.start, "action_freq": 24, "max_steps": args.steps,
        "save_video": False, "fast_video": False, "gb_path": GB_PATH,
        "explore_weight": 0.25, "reward_scale": 0.5, "instance_id": "watch",
    }
    base = envmod.RedGymEnv(config)

    model = None
    if args.policy == "trained":
        from stable_baselines3 import PPO
        from stable_baselines3.common.vec_env import DummyVecEnv
        ck = args.checkpoint or latest_checkpoint(args.train_dir)
        if not ck or not os.path.exists(ck):
            print(f"no checkpoint found in {args.train_dir}; falling back to random")
            args.policy = "random"
        else:
            print(f"loading checkpoint: {ck}")
            venv = DummyVecEnv([lambda: base])          # match training obs pipeline
            model = PPO.load(ck, env=venv, device="cpu")
            vobs = model.env.reset()

    rng = np.random.default_rng(0)
    if args.policy == "random":
        obs, _ = base.reset()

    out_path = os.path.join(ROOT, args.out) if not os.path.isabs(args.out) else args.out
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    writer = imageio.get_writer(out_path, fps=args.fps, macro_block_size=None,
                                codec="libx264", quality=8)
    print(f"writing {out_path} ({args.steps} steps @ {args.fps}fps, "
          f"{args.policy} policy){' + live window' if args.live else ''}")

    gif_frames = []          # raw (no-HUD) frames, decimated at the end
    quit_early = False
    total_r = 0.0
    for step in range(args.steps):
        base.pyboy._frame_sink = []   # capture every intra-step game-frame
        if model is not None:
            action, _ = model.predict(vobs, deterministic=True)
            vobs, rew, done, info = model.env.step(action)
            total_r += float(rew[0]); done = bool(done[0])
        else:
            a = rng.choice([0, 1, 2, 3, 4], p=[0.23, 0.23, 0.23, 0.23, 0.08])
            obs, rew, done, trunc, info = base.step(int(a))
            total_r += float(rew); done = done or trunc
        frames = base.pyboy._frame_sink or []
        base.pyboy._frame_sink = None

        lines = [
            f"step {step:4d}  map 0x{base.read_m(0xD35E):02X}  "
            f"party {base.read_m(0xD163)}  rew {total_r:6.1f}",
            f"pos ({base.read_m(0xD362)},{base.read_m(0xD361)})  "
            f"{'TRAINED' if model else 'RANDOM'}",
        ]
        for fr in frames:                              # smooth: one video frame per game-frame
            big = cv2.resize(fr, (fr.shape[1] * args.scale, fr.shape[0] * args.scale),
                             interpolation=cv2.INTER_NEAREST).copy()
            writer.append_data(hud(big, lines))
            if args.live:
                cv2.imshow("recompiled Pokemon - RL agent",
                           cv2.cvtColor(big, cv2.COLOR_RGB2BGR))
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    quit_early = True
                    break
        if args.gif:
            gif_frames.extend(frames)
        if quit_early:
            break
        if done and model is None:
            obs, _ = base.reset()   # gym single-env needs manual reset; the
            # VecEnv (trained path) auto-resets and vobs already holds the next obs.

    writer.close()
    if args.live:
        cv2.destroyAllWindows()
    print(f"done. final reward {total_r:.1f}. video -> {out_path}")

    if args.gif and gif_frames:
        gif_path = os.path.join(ROOT, args.gif) if not os.path.isabs(args.gif) else args.gif
        os.makedirs(os.path.dirname(gif_path), exist_ok=True)
        keep = max(1, len(gif_frames) // args.gif_frames)
        sel = gif_frames[::keep][:args.gif_frames]
        small = [cv2.resize(f, (f.shape[1] * args.gif_scale, f.shape[0] * args.gif_scale),
                            interpolation=cv2.INTER_NEAREST) for f in sel]
        imageio.mimsave(gif_path, small, fps=args.gif_fps, loop=0)
        print(f"gif ({len(small)} frames) -> {gif_path}")

    base.pyboy.stop(save=False)


if __name__ == "__main__":
    main()
