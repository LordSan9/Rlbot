"""
Load and analyze experience files recorded by rlbot_win.

Each .bin file: N frames, each frame = 465 bytes
  float32[107]  obs
  float32[8]    action
  float32[1]    reward
  uint8[1]      terminal

Usage:
    python scripts/analyze_experience.py data/experience/
"""

import struct
import os
import sys
import numpy as np

OBS_SIZE  = 107
ACT_SIZE  = 8
FRAME_SIZE = (OBS_SIZE + ACT_SIZE + 1) * 4 + 1  # 465 bytes


def load_episode(path: str):
    frames = []
    with open(path, "rb") as f:
        while True:
            raw = f.read(FRAME_SIZE)
            if len(raw) < FRAME_SIZE:
                break
            obs     = np.frombuffer(raw[:OBS_SIZE*4],              dtype=np.float32).copy()
            action  = np.frombuffer(raw[OBS_SIZE*4:(OBS_SIZE+ACT_SIZE)*4], dtype=np.float32).copy()
            reward  = struct.unpack_from("<f", raw, (OBS_SIZE+ACT_SIZE)*4)[0]
            terminal = raw[-1] == 1
            frames.append({"obs": obs, "action": action, "reward": reward, "terminal": terminal})
    return frames


def analyze(directory: str):
    files = sorted(f for f in os.listdir(directory) if f.endswith(".bin"))
    if not files:
        print("No .bin files found.")
        return

    all_rewards = []
    for fname in files:
        frames = load_episode(os.path.join(directory, fname))
        rewards = [f["reward"] for f in frames]
        ep_total = sum(rewards)
        all_rewards.extend(rewards)
        print(f"{fname}: {len(frames)} frames, total reward={ep_total:.2f}, "
              f"mean={np.mean(rewards):.4f}, goals~{rewards.count(1.0)}, "
              f"conceded~{rewards.count(-1.0)}")

    print(f"\nTotal frames: {len(all_rewards)}")
    print(f"Mean reward per frame: {np.mean(all_rewards):.4f}")


if __name__ == "__main__":
    directory = sys.argv[1] if len(sys.argv) > 1 else "data/experience"
    analyze(directory)
