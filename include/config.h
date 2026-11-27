#pragma once
#include <cstdint>
#include <string>

// ============================================================
//  BUILD-DEPENDENT OFFSETS
//  Source: PrepuBot v1.2 Linux build (Sep 2025) + BakkesMod SDK
//  These are RVAs from RocketLeague.exe base unless noted.
//
//  VERIFY BEFORE USE:
//    1. Attach x64dbg to RocketLeague.exe
//    2. Run offset_resolver which will AOB-scan and confirm/replace these
//    3. Check ~/.rlbot_win/offsets.json for the last confirmed values
// ============================================================

namespace Config {

// ── Process ──────────────────────────────────────────────────────────────────
inline constexpr const char* PROCESS_NAME     = "RocketLeague.exe";
inline constexpr uint32_t    EXPECTED_MOD_MIN = 0x2000000;   // ~32 MB sanity
inline constexpr uint32_t    EXPECTED_MOD_MAX = 0x8000000;   // ~128 MB sanity

// ── UE3 Global Tables ─────────────────────────────────────────────────────────
// RVA from module base.
// UNVERIFIED on Windows — PrepuBot values were Linux ELF offsets.
// The AOB scanner in offset_resolver.cpp will find them on first run
// and write confirmed RVAs to offsets.json.
inline constexpr uintptr_t GNAMES_RVA_HINT   = 0x02418148;  // UNVERIFIED WIN
inline constexpr uintptr_t GOBJECTS_RVA_HINT = 0x02418190;  // UNVERIFIED WIN

// GNames validation: GNames[1] must start with this string
inline constexpr const char* GNAMES_MAGIC = "ByteProperty";

// ── UE3 UObject layout ───────────────────────────────────────────────────────
// Confirmed from BakkesMod SDK headers (UObject structure UE3)
inline constexpr uint32_t UOBJECT_VFTABLE    = 0x00;
inline constexpr uint32_t UOBJECT_FLAGS      = 0x08;
inline constexpr uint32_t UOBJECT_INDEX      = 0x0C;
inline constexpr uint32_t UOBJECT_CLASS      = 0x10;
inline constexpr uint32_t UOBJECT_NAME       = 0x18;  // FName { int32 index, int32 number }
inline constexpr uint32_t UOBJECT_OUTER      = 0x20;

// ── RBActor_TA physics state ──────────────────────────────────────────────────
// Source: PrepuBot v1.1 — confirmed in BakkesMod SDK RBActor_TA::RBState
// RBState is a 52-byte struct at this offset from the RBActor_TA pointer.
// Layout (all float, little-endian):
//   +0x00  position     vec3  (12 bytes)
//   +0x0C  quaternion   quat  (16 bytes) — x,y,z,w
//   +0x1C  linear_vel   vec3  (12 bytes)
//   +0x28  angular_vel  vec3  (12 bytes)
//   total: 0x34 = 52 bytes
inline constexpr uint32_t RBSTATE_OFFSET     = 0x5D0;  // from RBActor_TA*
inline constexpr uint32_t RBSTATE_SIZE       = 52;

// ── CarComponent_Boost_TA ─────────────────────────────────────────────────────
// UNKNOWN — must be found via BakkesMod SDK dump or ReClass on a live process.
// Set to 0 to gate the boost reader until verified.
inline constexpr uint32_t BOOST_COMPONENT_OFFSET = 0;  // UNKNOWN — gate active
inline constexpr uint32_t BOOST_AMOUNT_OFFSET    = 0;  // UNKNOWN

// ── Ball / Car UObject class names (GNames lookup) ───────────────────────────
inline constexpr const char* CLASS_BALL  = "Ball_TA";
inline constexpr const char* CLASS_CAR   = "Car_TA";
inline constexpr const char* CLASS_BOOST = "CarComponent_Boost_TA";

// ── Nexto ONNX model ─────────────────────────────────────────────────────────
inline constexpr const char* MODEL_PATH  = "models/nexto.onnx";
// Input tensor name as exported from the RLGym/Nexto training code.
// Verify with: python -c "import onnx; m=onnx.load('nexto.onnx'); [print(i.name) for i in m.graph.input]"
inline constexpr const char* OBS_INPUT_NAME  = "obs";
inline constexpr const char* ACT_OUTPUT_NAME = "output";
inline constexpr int64_t     OBS_SIZE        = 107;  // Nexto observation vector length
inline constexpr int64_t     ACT_SIZE        = 8;    // throttle,steer,pitch,yaw,roll,jump,boost,handbrake

// ── Bot loop ─────────────────────────────────────────────────────────────────
inline constexpr int    BOT_HZ           = 120;   // physics tick rate
inline constexpr double BOT_FRAME_MS     = 1000.0 / BOT_HZ;
inline constexpr int    HOTKEY_TOGGLE    = VK_F6;  // toggle bot on/off

// ── Experience recorder ───────────────────────────────────────────────────────
inline constexpr const char* EXPERIENCE_DIR  = "data/experience";
inline constexpr int    MAX_EPISODE_FRAMES   = 72000;  // 10 min @ 120Hz

// ── Offset cache ─────────────────────────────────────────────────────────────
inline constexpr const char* OFFSET_CACHE_PATH = "config/offsets.json";

} // namespace Config
