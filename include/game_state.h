#pragma once
#include "rl_structs.h"
#include "process_reader.h"
#include "offset_resolver.h"
#include <vector>
#include <chrono>

// ============================================================
//  Reads a full game state snapshot from the game process.
//  Call snapshot() once per bot frame (120Hz).
//  All returned values are validated; invalid data returns
//  default-constructed structs with is_valid = false.
// ============================================================

struct GameStateSnapshot {
    BallState               ball;
    CarState                local_car;
    std::vector<CarState>   opponents;    // up to 5 in 3v3
    bool                    game_active = false;
    std::chrono::steady_clock::time_point timestamp;
};

class GameStateReader {
public:
    GameStateReader() = default;

    // Must be called after the offset resolver has succeeded.
    void init(const ProcessReader* reader, const OffsetResolver* resolver);

    // Takes a snapshot of the current game state.
    // Returns false if the game is not in a playable state (menu, replay, etc.)
    bool snapshot(GameStateSnapshot& out);

    // Returns the last snapshot without re-reading memory.
    const GameStateSnapshot& last() const { return last_; }

private:
    const ProcessReader*  reader_   = nullptr;
    const OffsetResolver* resolver_ = nullptr;

    GameStateSnapshot last_;

    bool read_rb_state(uintptr_t rbactor_ptr, RBState& out) const;
    bool read_car(uintptr_t car_ptr, CarState& out) const;
    bool read_ball(uintptr_t ball_ptr, BallState& out) const;
};
