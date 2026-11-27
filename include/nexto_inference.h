#pragma once
#include "rl_structs.h"
#include "game_state.h"
#include <memory>
#include <string>

// Forward declare ONNX types to avoid pulling the full header everywhere
namespace Ort { class Session; class Env; class SessionOptions; class MemoryInfo; }

// ============================================================
//  Wraps ONNX Runtime session for Nexto inference.
//
//  The observation vector layout matches the RLGym Nexto obs builder:
//  https://github.com/rolv-arild/necto/blob/master/training/obs.py
//
//  Input:  float32[1, 107]  — OBS_SIZE from config.h
//  Output: float32[1, 8]    — throttle, steer, pitch, yaw, roll,
//                             jump, boost, handbrake
//
//  The model file (nexto.onnx) must be obtained separately:
//    1. Install rlgym-tools: pip install rlgym-tools
//    2. Download Nexto weights from the community repo
//    3. Export: python scripts/export_nexto_onnx.py
//  See scripts/export_nexto_onnx.py in this repo.
// ============================================================

class NextoInference {
public:
    NextoInference();
    ~NextoInference();

    // Load model from path. Returns false if file missing or model invalid.
    bool load(const char* model_path);
    bool is_loaded() const { return loaded_; }

    // Build observation vector from snapshot and run inference.
    // Returns false if snapshot is invalid or model not loaded.
    bool infer(const GameStateSnapshot& snap, NextoAction& action_out);

    // Build the 107-float observation vector from a game state snapshot.
    // Static so the experience recorder can call it directly.
    static bool build_obs(const GameStateSnapshot& snap, NextoObs& obs_out);

private:
    std::unique_ptr<Ort::Env>           env_;
    std::unique_ptr<Ort::Session>       session_;
    std::unique_ptr<Ort::SessionOptions> opts_;
    std::unique_ptr<Ort::MemoryInfo>    mem_info_;
    bool loaded_ = false;
};
