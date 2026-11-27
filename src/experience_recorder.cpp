#include "experience_recorder.h"
#include "nexto_inference.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;

void ExperienceRecorder::set_output_dir(const char* dir) {
    output_dir_ = dir;
    fs::create_directories(dir);
}

void ExperienceRecorder::begin_episode() {
    end_episode();
    frames_.clear();
    frames_.reserve(Config::MAX_EPISODE_FRAMES);
    has_prev_state_ = false;
    std::cout << "[Experience] Episode started.\n";
}

// ── compute_reward ────────────────────────────────────────────────────────────
float ExperienceRecorder::compute_reward(
    const GameStateSnapshot& prev,
    const GameStateSnapshot& curr) const
{
    if (!curr.game_active || !curr.ball.is_valid) return 0.f;

    float reward = 0.f;

    // Closeness to ball
    if (curr.local_car.is_alive) {
        Vec3 to_ball = curr.ball.phys.position - curr.local_car.phys.position;
        float dist = to_ball.length();
        if (dist < 500.f) reward += 0.1f;

        // Ball moving toward opponent goal (Y+ for team 0, Y- for team 1)
        // We assume local player is team 0 (Y+). Adjust if needed.
        if (curr.ball.phys.linear_vel.y > 0) {
            // Ball toward opponent goal
            float speed_toward = curr.ball.phys.linear_vel.y;
            // Extra reward only if we just touched the ball (ball vel changed and we're close)
            if (prev.ball.is_valid) {
                Vec3 vel_delta = curr.ball.phys.linear_vel - prev.ball.phys.linear_vel;
                if (vel_delta.length() > 50.f && dist < 300.f) {
                    reward += 0.5f;
                }
            }
        }
    }

    // Goals: we don't have score directly from memory yet (offset unknown),
    // so we infer goals from ball position reset (ball teleports to center after goal)
    if (prev.ball.is_valid) {
        Vec3 delta = curr.ball.phys.position - prev.ball.phys.position;
        bool teleport = delta.length() > 1000.f;  // ball jumped far in one frame = reset
        if (teleport) {
            // Heuristic: if ball was near opponent end before reset, we scored
            if (prev.ball.phys.position.y > 4500.f) {
                reward += 1.0f;
                std::cout << "[Experience] Goal scored (heuristic)! +1.0\n";
            } else if (prev.ball.phys.position.y < -4500.f) {
                reward -= 1.0f;
                std::cout << "[Experience] Goal conceded (heuristic)! -1.0\n";
            }
        }
    }

    return reward;
}

// ── record ────────────────────────────────────────────────────────────────────
void ExperienceRecorder::record(const GameStateSnapshot& snap,
                                const NextoObs&           obs,
                                const NextoAction&        action)
{
    if (frames_.size() >= static_cast<size_t>(Config::MAX_EPISODE_FRAMES)) {
        // Episode too long — flush and restart
        end_episode();
        begin_episode();
    }

    ExperienceFrame frame;
    frame.obs    = obs;
    frame.action = action;
    frame.terminal = false;

    if (has_prev_state_) {
        frame.reward = compute_reward(last_, snap);
    }

    frames_.push_back(frame);
    last_            = snap;
    has_prev_state_  = true;
}

// ── end_episode ───────────────────────────────────────────────────────────────
void ExperienceRecorder::end_episode() {
    if (frames_.empty()) return;

    // Mark last frame as terminal
    frames_.back().terminal = true;

    flush_to_disk();
    frames_.clear();
    has_prev_state_ = false;
}

// ── flush_to_disk ─────────────────────────────────────────────────────────────
void ExperienceRecorder::flush_to_disk() {
    std::string path = make_filename(output_dir_);
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[Experience] Cannot write to " << path << "\n";
        return;
    }

    for (const auto& fr : frames_) {
        // obs: 107 floats
        f.write(reinterpret_cast<const char*>(fr.obs.data),
                sizeof(float) * NextoObs::SIZE);
        // action: 8 floats
        float act[8] = {
            fr.action.throttle, fr.action.steer,  fr.action.pitch,
            fr.action.yaw,      fr.action.roll,   fr.action.jump,
            fr.action.boost,    fr.action.handbrake
        };
        f.write(reinterpret_cast<const char*>(act), sizeof(act));
        // reward: 1 float
        f.write(reinterpret_cast<const char*>(&fr.reward), sizeof(float));
        // terminal: 1 byte
        uint8_t term = fr.terminal ? 1 : 0;
        f.write(reinterpret_cast<const char*>(&term), 1);
    }

    total_frames_saved_ += frames_.size();
    std::cout << "[Experience] Episode saved: " << path
              << " (" << frames_.size() << " frames, "
              << total_frames_saved_ << " total)\n";
}

// ── make_filename ─────────────────────────────────────────────────────────────
std::string ExperienceRecorder::make_filename(const std::string& dir) {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char buf[64]{};
    struct tm tm_info{};
    localtime_s(&tm_info, &t);
    strftime(buf, sizeof(buf), "ep_%Y%m%d_%H%M%S.bin", &tm_info);
    return dir + "/" + buf;
}
