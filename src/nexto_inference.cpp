#include "nexto_inference.h"
#include "config.h"
#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <cmath>
#include <array>

// ── Helper math ───────────────────────────────────────────────────────────────
namespace {

// Rotate vector by quaternion (passive rotation)
Vec3 quat_rotate(const Quat& q, const Vec3& v) {
    // https://en.wikipedia.org/wiki/Quaternion#Hamilton_product
    float tx = 2.0f*(q.y*v.z - q.z*v.y);
    float ty = 2.0f*(q.z*v.x - q.x*v.z);
    float tz = 2.0f*(q.x*v.y - q.y*v.x);
    return {
        v.x + q.w*tx + q.y*tz - q.z*ty,
        v.y + q.w*ty + q.z*tx - q.x*tz,
        v.z + q.w*tz + q.x*ty - q.y*tx
    };
}

// Convert quaternion to forward/right/up vectors (UE3 convention)
void quat_to_axes(const Quat& q, Vec3& fwd, Vec3& right, Vec3& up) {
    fwd   = quat_rotate(q, {1,0,0});
    right = quat_rotate(q, {0,1,0});
    up    = quat_rotate(q, {0,0,1});
}

// Normalize to Nexto's expected scale (Unreal units → ~1 unit space)
// Nexto was trained with RLGym which uses UU coordinates.
// Positions are divided by a normalisation constant matching training data.
// Source: RLGym NectoObs builder constants
static constexpr float POS_STD   = 2300.0f;  // approx half-arena width
static constexpr float VEL_STD   = 2300.0f;  // max car speed in UU/s
static constexpr float ANG_STD   = 5.5f;     // max angular speed in rad/s

// Pack a local-frame relative state block (position, vel, ang_vel = 9 floats)
// into obs starting at obs[idx]. Returns next idx.
int pack_relative_phys(float* obs, int idx,
                       const Vec3& fwd, const Vec3& right, const Vec3& up,
                       const Vec3& pos_rel, const Vec3& vel_rel, const Vec3& ang_rel)
{
    // Position in car-local frame
    obs[idx++] = fwd.dot(pos_rel)   / POS_STD;
    obs[idx++] = right.dot(pos_rel) / POS_STD;
    obs[idx++] = up.dot(pos_rel)    / POS_STD;
    // Velocity
    obs[idx++] = fwd.dot(vel_rel)   / VEL_STD;
    obs[idx++] = right.dot(vel_rel) / VEL_STD;
    obs[idx++] = up.dot(vel_rel)    / VEL_STD;
    // Angular velocity
    obs[idx++] = fwd.dot(ang_rel)   / ANG_STD;
    obs[idx++] = right.dot(ang_rel) / ANG_STD;
    obs[idx++] = up.dot(ang_rel)    / ANG_STD;
    return idx;
}

} // anon

// ── build_obs ─────────────────────────────────────────────────────────────────
// Builds the 107-float observation vector matching Nexto's training obs builder.
// Layout (all floats, indices 0-106):
//   [0..8]    car position/vel/ang in world frame (9)
//   [9..11]   car forward vector (3)
//   [12..14]  car up vector (3)
//   [15..17]  boost amount, is_on_ground placeholder, is_alive (3)
//   [18..26]  ball position/vel/ang relative to car (9)
//   [27..35]  ball position/vel/ang in world frame (9)
//   [36..44]  relative to each opponent car (up to 1 opp for 1v1) (9)
//   [45..106] padded zeros for unused opponent slots
//
// NOTE: This layout is an approximation of Nexto's obs builder.
// If inference produces garbage, compare against the Python obs builder
// output with the same state using the validation script in scripts/.
bool NextoInference::build_obs(const GameStateSnapshot& snap, NextoObs& obs_out) {
    if (!snap.game_active || !snap.ball.is_valid || !snap.local_car.is_alive)
        return false;

    float* obs = obs_out.data;
    std::fill(obs, obs + NextoObs::SIZE, 0.0f);

    const RBState& car  = snap.local_car.phys;
    const RBState& ball = snap.ball.phys;

    Vec3 car_fwd, car_right, car_up;
    quat_to_axes(car.rotation, car_fwd, car_right, car_up);

    // ── [0..8] Car own state in world frame ──────────────────────────────────
    int idx = 0;
    obs[idx++] = car.position.x    / POS_STD;
    obs[idx++] = car.position.y    / POS_STD;
    obs[idx++] = car.position.z    / POS_STD;
    obs[idx++] = car.linear_vel.x  / VEL_STD;
    obs[idx++] = car.linear_vel.y  / VEL_STD;
    obs[idx++] = car.linear_vel.z  / VEL_STD;
    obs[idx++] = car.angular_vel.x / ANG_STD;
    obs[idx++] = car.angular_vel.y / ANG_STD;
    obs[idx++] = car.angular_vel.z / ANG_STD;

    // ── [9..14] Orientation axes ─────────────────────────────────────────────
    obs[idx++] = car_fwd.x;  obs[idx++] = car_fwd.y;  obs[idx++] = car_fwd.z;
    obs[idx++] = car_up.x;   obs[idx++] = car_up.y;   obs[idx++] = car_up.z;

    // ── [15..17] Boost / ground / alive ──────────────────────────────────────
    obs[idx++] = snap.local_car.boost_valid
                   ? snap.local_car.boost_amount / 100.0f
                   : 0.5f;   // assume 50% if unknown
    obs[idx++] = (car.position.z < 50.0f) ? 1.0f : 0.0f;  // rough on-ground heuristic
    obs[idx++] = 1.0f;  // is_alive

    // ── [18..26] Ball relative to car (car-local frame) ──────────────────────
    Vec3 ball_pos_rel = ball.position - car.position;
    Vec3 ball_vel_rel = ball.linear_vel - car.linear_vel;
    Vec3 ball_ang_rel = ball.angular_vel;
    idx = pack_relative_phys(obs, idx, car_fwd, car_right, car_up,
                             ball_pos_rel, ball_vel_rel, ball_ang_rel);

    // ── [27..35] Ball world frame ─────────────────────────────────────────────
    obs[idx++] = ball.position.x    / POS_STD;
    obs[idx++] = ball.position.y    / POS_STD;
    obs[idx++] = ball.position.z    / POS_STD;
    obs[idx++] = ball.linear_vel.x  / VEL_STD;
    obs[idx++] = ball.linear_vel.y  / VEL_STD;
    obs[idx++] = ball.linear_vel.z  / VEL_STD;
    obs[idx++] = ball.angular_vel.x / ANG_STD;
    obs[idx++] = ball.angular_vel.y / ANG_STD;
    obs[idx++] = ball.angular_vel.z / ANG_STD;

    // ── [36..] Opponent cars (up to 5 slots, padded with zeros) ──────────────
    // Each opponent slot: 9 floats (relative phys) + 3 (orientation) + 3 (boost/gnd/alive) = 15
    // Remaining slots (up to 5) stay zero-padded.
    for (size_t oi = 0; oi < std::min(snap.opponents.size(), size_t(5)); ++oi) {
        const CarState& opp = snap.opponents[oi];
        if (!opp.is_alive) { idx += 15; continue; }

        Vec3 opp_fwd, opp_right, opp_up;
        quat_to_axes(opp.phys.rotation, opp_fwd, opp_right, opp_up);

        Vec3 opp_pos_rel = opp.phys.position - car.position;
        Vec3 opp_vel_rel = opp.phys.linear_vel - car.linear_vel;
        Vec3 opp_ang_rel = opp.phys.angular_vel;
        idx = pack_relative_phys(obs, idx, car_fwd, car_right, car_up,
                                 opp_pos_rel, opp_vel_rel, opp_ang_rel);
        obs[idx++] = opp_fwd.x; obs[idx++] = opp_fwd.y; obs[idx++] = opp_fwd.z;
        obs[idx++] = opp.boost_valid ? opp.boost_amount/100.f : 0.5f;
        obs[idx++] = (opp.phys.position.z < 50.f) ? 1.f : 0.f;
        obs[idx++] = 1.0f;

        if (idx >= NextoObs::SIZE) break;
    }

    return true;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────
NextoInference::NextoInference()
    : env_(std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "rlbot_win"))
    , opts_(std::make_unique<Ort::SessionOptions>())
    , mem_info_(std::make_unique<Ort::MemoryInfo>(
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)))
{}

NextoInference::~NextoInference() = default;

// ── load ─────────────────────────────────────────────────────────────────────
bool NextoInference::load(const char* model_path) {
    loaded_ = false;
    try {
        opts_->SetIntraOpNumThreads(1);
        opts_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // ONNX Runtime requires wide path on Windows
        std::wstring wpath(model_path, model_path + strlen(model_path));
        session_ = std::make_unique<Ort::Session>(*env_, wpath.c_str(), *opts_);
        loaded_ = true;
        std::cout << "[Nexto] Model loaded from " << model_path << "\n";
    } catch (const Ort::Exception& e) {
        std::cerr << "[Nexto] Load failed: " << e.what() << "\n";
        return false;
    }
    return true;
}

// ── infer ─────────────────────────────────────────────────────────────────────
bool NextoInference::infer(const GameStateSnapshot& snap, NextoAction& action_out) {
    if (!loaded_) return false;

    NextoObs obs{};
    if (!build_obs(snap, obs)) return false;

    try {
        // Input tensor [1, 107]
        std::array<int64_t,2> input_shape{1, Config::OBS_SIZE};
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            *mem_info_, obs.data, Config::OBS_SIZE,
            input_shape.data(), input_shape.size());

        // Run
        const char* input_names[]  = {Config::OBS_INPUT_NAME};
        const char* output_names[] = {Config::ACT_OUTPUT_NAME};

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names,  &input_tensor, 1,
            output_names, 1);

        // Output tensor [1, 8]
        float* out = outputs[0].GetTensorMutableData<float>();
        action_out.throttle  = out[0];
        action_out.steer     = out[1];
        action_out.pitch     = out[2];
        action_out.yaw       = out[3];
        action_out.roll      = out[4];
        action_out.jump      = out[5];
        action_out.boost     = out[6];
        action_out.handbrake = out[7];
    } catch (const Ort::Exception& e) {
        std::cerr << "[Nexto] Inference failed: " << e.what() << "\n";
        return false;
    }
    return true;
}
