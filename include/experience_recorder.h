#pragma once
#include "rl_structs.h"
#include "game_state.h"
#include <vector>
#include <string>
#include <fstream>

// ============================================================
//  Records (obs, action, reward) tuples at 120Hz.
//  One episode = one game (from kickoff to end of match).
//  Each episode is saved as a binary file:
//    data/experience/ep_<timestamp>.bin
//
//  File format per frame (little-endian):
//    float32[107]  observation
//    float32[8]    action
//    float32[1]    reward
//    uint8[1]      terminal flag (1 = episode end)
//    = 116 * 4 + 1 = 465 bytes per frame
//
//  Reward function (dense, shaped):
//    +1.0  per goal scored
//    +0.5  per touch of ball moving toward opponent goal
//    +0.1  for being close to ball (< 500 uu)
//    -1.0  per goal conceded
//  This is intentionally simple and tunable.
// ============================================================

struct ExperienceFrame {
    NextoObs   obs;
    NextoAction action;
    float      reward    = 0.f;
    bool       terminal  = false;
};

class ExperienceRecorder {
public:
    ExperienceRecorder() = default;
    ~ExperienceRecorder() { end_episode(); }

    void set_output_dir(const char* dir);

    // Call once at the start of a new match (kickoff).
    void begin_episode();

    // Call every frame while bot is active.
    // Computes reward from consecutive snapshots.
    void record(const GameStateSnapshot& snap,
                const NextoObs&          obs,
                const NextoAction&       action);

    // Call when the match ends (or bot is toggled off).
    // Flushes current episode to disk.
    void end_episode();

    // Returns the number of frames recorded in the current episode.
    size_t current_episode_frames() const { return frames_.size(); }

    // Returns total frames saved to disk across all sessions.
    uint64_t total_frames_saved() const { return total_frames_saved_; }

private:
    std::string  output_dir_ = "data/experience";
    std::vector<ExperienceFrame> frames_;
    uint64_t     total_frames_saved_ = 0;

    // State for reward calculation
    Vec3   last_ball_pos_{};
    int    last_score_ally_ = 0;
    int    last_score_opp_  = 0;
    bool   has_prev_state_  = false;

    float compute_reward(const GameStateSnapshot& prev,
                         const GameStateSnapshot& curr) const;

    void flush_to_disk();
    static std::string make_filename(const std::string& dir);
};
