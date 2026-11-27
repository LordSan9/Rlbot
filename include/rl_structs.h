#pragma once
#include <cstdint>
#include <cmath>

// ============================================================
//  Rocket League / UE3 data structures
//  Coordinate system: Z-up, left-handed, Unreal units (1 uu = 1 cm)
//  Quaternion layout: x, y, z, w
// ============================================================

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator*(float s)       const { return {x*s,   y*s,   z*s};   }

    float dot(const Vec3& o)   const { return x*o.x + y*o.y + z*o.z; }
    float length_sq()          const { return dot(*this); }
    float length()             const { return std::sqrtf(length_sq()); }

    Vec3 normalized() const {
        float l = length();
        return l > 1e-6f ? Vec3{x/l, y/l, z/l} : Vec3{};
    }

    bool is_finite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }

    // Plausibility: within the RL arena plus margin (~10800 uu x ~8200 uu x ~2100 uu)
    bool in_arena_bounds() const {
        return std::fabsf(x) < 12000.f &&
               std::fabsf(y) < 10000.f &&
               z > -100.f && z < 3000.f;
    }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    bool is_finite() const {
        return std::isfinite(x) && std::isfinite(y) &&
               std::isfinite(z) && std::isfinite(w);
    }

    // Rough unit-quaternion check
    bool is_valid() const {
        if (!is_finite()) return false;
        float len2 = x*x + y*y + z*z + w*w;
        return len2 > 0.9f && len2 < 1.1f;
    }
};

// RBActor_TA::RBState — 52 bytes, read atomically
// Source: PrepuBot v1.1 / BakkesMod SDK
#pragma pack(push, 1)
struct RBState {
    Vec3  position;        // +0x00
    Quat  rotation;        // +0x0C
    Vec3  linear_vel;      // +0x1C
    Vec3  angular_vel;     // +0x28
};                         // total: 0x34 = 52 bytes
#pragma pack(pop)
static_assert(sizeof(RBState) == 52, "RBState size mismatch");

struct CarState {
    RBState  phys;
    float    boost_amount = 0.f;   // 0–100; valid only when BOOST offsets confirmed
    bool     is_alive     = false;
    bool     boost_valid  = false; // false until boost offsets verified
};

struct BallState {
    RBState  phys;
    bool     is_valid = false;
};

// Nexto observation vector (107 floats) built from game state.
// Layout matches the RLGym Nexto observation builder exactly.
// See: https://github.com/rolv-arild/necto/blob/master/training/obs.py
struct NextoObs {
    static constexpr int SIZE = 107;
    float data[SIZE] = {};
};

// Nexto action output (8 floats, all in [-1, 1] except booleans)
struct NextoAction {
    float throttle  = 0;
    float steer     = 0;
    float pitch     = 0;
    float yaw       = 0;
    float roll      = 0;
    float jump      = 0;   // >0 = pressed
    float boost     = 0;   // >0 = pressed
    float handbrake = 0;   // >0 = pressed
};

// UE3 FName (8 bytes: index + number)
struct FName {
    int32_t index  = -1;
    int32_t number = 0;
    bool is_valid() const { return index >= 0; }
};

// UE3 UObject minimal header (offsets from Config)
struct UObjectHeader {
    uintptr_t vftable;
    uint32_t  flags;
    int32_t   obj_index;
    uintptr_t uclass;
    FName     name;
    uintptr_t outer;
};
