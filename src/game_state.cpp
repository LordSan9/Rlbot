#include "game_state.h"
#include "config.h"
#include <iostream>

void GameStateReader::init(const ProcessReader* reader, const OffsetResolver* resolver) {
    reader_   = reader;
    resolver_ = resolver;
}

// ── read_rb_state ─────────────────────────────────────────────────────────────
bool GameStateReader::read_rb_state(uintptr_t rbactor_ptr, RBState& out) const {
    if (!rbactor_ptr) return false;
    // RBState is 52 bytes at RBSTATE_OFFSET from the RBActor_TA pointer.
    // We read it atomically in one RPM call.
    if (!reader_->read_raw(rbactor_ptr + Config::RBSTATE_OFFSET, &out, sizeof(RBState)))
        return false;

    // Validate all fields
    if (!out.position.is_finite()   || !out.position.in_arena_bounds()) return false;
    if (!out.linear_vel.is_finite()  ) return false;
    if (!out.angular_vel.is_finite() ) return false;
    if (!out.rotation.is_valid()     ) return false;
    return true;
}

// ── read_ball ─────────────────────────────────────────────────────────────────
bool GameStateReader::read_ball(uintptr_t ball_ptr, BallState& out) const {
    out = {};
    if (!read_rb_state(ball_ptr, out.phys)) return false;
    out.is_valid = true;
    return true;
}

// ── read_car ──────────────────────────────────────────────────────────────────
bool GameStateReader::read_car(uintptr_t car_ptr, CarState& out) const {
    out = {};
    if (!read_rb_state(car_ptr, out.phys)) return false;
    out.is_alive = true;

    // Boost: gated until offsets are verified
    if (Config::BOOST_COMPONENT_OFFSET != 0 && Config::BOOST_AMOUNT_OFFSET != 0) {
        uintptr_t boost_comp = 0;
        if (reader_->read(car_ptr + Config::BOOST_COMPONENT_OFFSET, boost_comp) && boost_comp) {
            uint8_t raw_boost = 0;
            if (reader_->read(boost_comp + Config::BOOST_AMOUNT_OFFSET, raw_boost)) {
                // Raw boost is 0-255 in Rocket League, normalize to 0-100
                out.boost_amount = raw_boost / 255.0f * 100.0f;
                out.boost_valid  = true;
            }
        }
    }
    return true;
}

// ── snapshot ──────────────────────────────────────────────────────────────────
bool GameStateReader::snapshot(GameStateSnapshot& out) {
    out = {};
    out.timestamp = std::chrono::steady_clock::now();

    if (!reader_ || !resolver_) return false;

    // Find ball
    uintptr_t ball_ptr = resolver_->find_first_of_class(*reader_, Config::CLASS_BALL);
    if (!ball_ptr) return false;
    if (!read_ball(ball_ptr, out.ball)) return false;

    // Find cars
    auto car_ptrs = resolver_->find_all_of_class(*reader_, Config::CLASS_CAR, 6);
    if (car_ptrs.empty()) return false;

    // First car is assumed to be the local player.
    // TODO: proper local player identification via GWorld → GameInstance → LocalPlayers
    if (!read_car(car_ptrs[0], out.local_car)) return false;

    for (size_t i = 1; i < car_ptrs.size(); ++i) {
        CarState cs;
        if (read_car(car_ptrs[i], cs)) out.opponents.push_back(cs);
    }

    out.game_active = true;
    last_ = out;
    return true;
}
