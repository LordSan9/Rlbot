# RLBot-Win

Rocket League bot for Windows using external memory reading + Nexto AI.

## Architecture

```
RocketLeague.exe  ←─ ReadProcessMemory ─── ProcessReader
                                              │
                                         OffsetResolver  (AOB scan → cache)
                                              │
                                         GameStateReader  (ball/car/boost)
                                              │
                                         NextoInference  (ONNX Runtime)
                                              │
                                         VigemInput      (virtual Xbox controller)
                                              │
                                         ExperienceRecorder (learning data)
```

## Requirements

| Dependency | Download |
|---|---|
| ViGEmBus driver | https://github.com/nefarius/ViGEmBus/releases |
| ViGEmClient SDK | https://github.com/nefarius/ViGEmClient |
| ONNX Runtime 1.19.2 | https://github.com/microsoft/onnxruntime/releases |
| Visual Studio 2022 | with C++ Desktop workload |
| CMake 3.20+ | |
| Nexto ONNX model | see below |

## Build

```bat
# 1. Install ViGEmBus driver (run installer as Administrator)

# 2. Clone and build ViGEmClient
git clone https://github.com/nefarius/ViGEmClient
cd ViGEmClient && mkdir build && cd build
cmake .. -A x64 && cmake --build . --config Release

# 3. Download ONNX Runtime
# Extract onnxruntime-win-x64-1.19.2.zip next to this repo

# 4. Configure and build
mkdir build && cd build
cmake .. -A x64 `
  -DONNXRUNTIME_DIR=../../onnxruntime-win-x64-1.19.2 `
  -DVIGEMCLIENT_DIR=../../ViGEmClient
cmake --build . --config Release

# Output: build/Release/rlbot_win.exe
```

## Get the Nexto Model

```bash
# Install dependencies
pip install torch onnx rlgym-tools

# Clone Nexto and get weights
git clone https://github.com/rolv-arild/necto
# Download nexto.pt from their GitHub releases

# Export to ONNX
python scripts/export_nexto_onnx.py --input nexto.pt --output models/nexto.onnx
```

**Note:** `export_nexto_onnx.py` has a placeholder `load_nexto_model()` function.
You must fill it in with the real import from the necto repo.

## Usage

```bat
# Run as Administrator (needed for ReadProcessMemory on some systems)
rlbot_win.exe

# F6 = toggle bot on/off
# Close console window to exit cleanly
```

## Offset Resolution

On first run the resolver:
1. Checks hint values (`GNAMES_RVA_HINT`, `GOBJECTS_RVA_HINT`) from config.h
2. If those fail, runs AOB scan (~1-2 seconds)
3. Writes confirmed RVAs to `config/offsets.json`

**If AOB scan fails after a game update:**
1. Open x64dbg, attach to `RocketLeague.exe`
2. Search for the byte patterns in config.h
3. Update `GNAMES_RVA_HINT` / `GOBJECTS_RVA_HINT` in config.h

## Learning

The bot records every frame while active:
- Files: `data/experience/ep_YYYYMMDD_HHMMSS.bin`
- Format: obs(107f) + action(8f) + reward(1f) + terminal(1b) per frame
- Analyze: `python scripts/analyze_experience.py data/experience/`

**What's missing to close the learning loop:**
The recorder saves experience. To actually improve the model:
1. Collect ~100K+ frames
2. Run PPO/REINFORCE fine-tuning on the recorded data
3. Export new weights to ONNX and replace `models/nexto.onnx`

## Known Unknowns (verify before use)

| Item | Status |
|---|---|
| `GNAMES_RVA_HINT` | UNVERIFIED on Windows — AOB scanner is the fallback |
| `GOBJECTS_RVA_HINT` | UNVERIFIED on Windows |
| `BOOST_COMPONENT_OFFSET` | UNKNOWN — boost reads disabled until found |
| `BOOST_AMOUNT_OFFSET` | UNKNOWN |
| Local player identification | Currently assumes first Car_TA = local player |
| obs builder layout | Approximation — validate against Python obs builder |

## Find Missing Offsets

**Boost offsets:**
1. In Cheat Engine, scan for your current boost % as a byte (0-255 scale)
2. Drive around, narrow down
3. Look at pointer chain from Car_TA pointer
4. Set `BOOST_COMPONENT_OFFSET` and `BOOST_AMOUNT_OFFSET` in config.h

**Local player:**
1. Find `GWorld → GameInstance → LocalPlayers[0] → PlayerController → AcknowledgedPawn`
2. That pawn pointer leads to your Car_TA
3. Implement in GameStateReader::snapshot()
