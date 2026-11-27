#pragma once
#include "process_reader.h"
#include "offset_resolver.h"
#include "game_state.h"
#include "nexto_inference.h"
#include "vigem_input.h"
#include "experience_recorder.h"
#include "overlay.h"
#include <atomic>
#include <thread>

// ============================================================
//  Top-level controller: owns all subsystems, runs the 120Hz
//  bot loop on a dedicated thread, handles F6 toggle.
// ============================================================

class BotController {
public:
    BotController() = default;
    ~BotController() { stop(); }

    BotController(const BotController&) = delete;
    BotController& operator=(const BotController&) = delete;

    // Initializes all subsystems. Returns false on hard failure.
    bool init();

    // Blocks until the user closes the console or presses Ctrl+C.
    void run();

    // Signal stop from any thread.
    void stop();

private:
    ProcessReader     reader_;
    OffsetResolver    resolver_;
    GameStateReader   game_reader_;
    NextoInference    nexto_;
    VigemInput        input_;
    ExperienceRecorder recorder_;
    ConsoleOverlay    overlay_;

    std::atomic<bool> running_      {false};
    std::atomic<bool> bot_active_   {false};

    std::thread bot_thread_;
    std::thread overlay_thread_;

    void bot_loop();
    void overlay_loop();
    void handle_hotkeys();

    bool wait_for_game();
    bool reconnect_if_needed();
};
