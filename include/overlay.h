#pragma once
#include "game_state.h"
#include "experience_recorder.h"
#include <cstdint>

// ============================================================
//  Console-based HUD overlay — prints telemetry to a dedicated
//  console window. No D3D, no injection.
//  Refreshes at ~10Hz (console update is cheap, not 120Hz).
// ============================================================

class ConsoleOverlay {
public:
    void init();
    void update(bool bot_active,
                const GameStateSnapshot& snap,
                const ExperienceRecorder& recorder);
private:
    HANDLE console_ = INVALID_HANDLE_VALUE;
    void goto_xy(int x, int y);
    void clear_screen();
};
