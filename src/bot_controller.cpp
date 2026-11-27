#include "bot_controller.h"
#include "config.h"
#include <iostream>
#include <chrono>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;

// ── init ─────────────────────────────────────────────────────────────────────
bool BotController::init() {
    std::cout << "[BotController] Initializing...\n";

    // ViGEm must be first — if driver not installed, abort early
    if (!input_.init()) {
        std::cerr << "[BotController] ViGEmBus not available. "
                  << "Install from https://github.com/nefarius/ViGEmBus/releases\n";
        return false;
    }

    // Load Nexto model
    if (!nexto_.load(Config::MODEL_PATH)) {
        std::cerr << "[BotController] Nexto model not found at "
                  << Config::MODEL_PATH << "\n"
                  << "  See scripts/export_nexto_onnx.py to export it.\n";
        return false;
    }

    // Set up experience recorder
    recorder_.set_output_dir(Config::EXPERIENCE_DIR);

    // Open console overlay
    overlay_.init();

    std::cout << "[BotController] Ready. F6 toggles bot. Close window to exit.\n";
    return true;
}

// ── run ───────────────────────────────────────────────────────────────────────
void BotController::run() {
    running_ = true;

    // Start bot loop thread
    bot_thread_ = std::thread([this]{ bot_loop(); });

    // Start overlay update thread (10Hz, cheap)
    overlay_thread_ = std::thread([this]{ overlay_loop(); });

    // Main thread: hotkey polling (Windows message loop not needed for WM_KEYDOWN polling)
    while (running_) {
        handle_hotkeys();
        std::this_thread::sleep_for(50ms);
    }

    bot_thread_.join();
    overlay_thread_.join();
}

// ── stop ─────────────────────────────────────────────────────────────────────
void BotController::stop() {
    running_    = false;
    bot_active_ = false;
    input_.release_all();
    recorder_.end_episode();
}

// ── wait_for_game ─────────────────────────────────────────────────────────────
bool BotController::wait_for_game() {
    std::cout << "[BotController] Waiting for " << Config::PROCESS_NAME << "...\n";
    while (running_) {
        if (reader_.open(Config::PROCESS_NAME)) {
            // Validate module size
            if (reader_.module_size() < Config::EXPECTED_MOD_MIN ||
                reader_.module_size() > Config::EXPECTED_MOD_MAX) {
                std::cerr << "[BotController] Module size 0x" << std::hex
                          << reader_.module_size() << " outside expected range.\n";
                reader_.close();
                std::this_thread::sleep_for(3s);
                continue;
            }
            std::cout << "[BotController] Attached to " << Config::PROCESS_NAME
                      << " (base=0x" << std::hex << reader_.module_base()
                      << ", size=0x" << reader_.module_size() << ")\n";
            return true;
        }
        std::this_thread::sleep_for(2s);
    }
    return false;
}

// ── reconnect_if_needed ───────────────────────────────────────────────────────
bool BotController::reconnect_if_needed() {
    if (reader_.is_alive()) return true;
    std::cout << "[BotController] Game closed, waiting for restart...\n";
    bot_active_ = false;
    input_.release_all();
    recorder_.end_episode();
    reader_.close();
    return wait_for_game() && resolver_.resolve(reader_);
}

// ── bot_loop ──────────────────────────────────────────────────────────────────
void BotController::bot_loop() {
    if (!wait_for_game()) return;

    if (!resolver_.resolve(reader_)) {
        std::cerr << "[BotController] Offset resolution failed — cannot read game state.\n";
        std::cerr << "  Run with the game on the main menu, then try again.\n";
        return;
    }

    game_reader_.init(&reader_, &resolver_);

    bool episode_started = false;

    while (running_) {
        auto frame_start = steady_clock::now();

        if (!reconnect_if_needed()) break;

        // Read game state
        GameStateSnapshot snap;
        bool state_ok = game_reader_.snapshot(snap);

        if (bot_active_ && state_ok) {
            if (!episode_started) {
                recorder_.begin_episode();
                episode_started = true;
            }

            // Build observation and run inference
            NextoObs obs{};
            NextoAction action{};
            if (NextoInference::build_obs(snap, obs) &&
                nexto_.infer(snap, action)) {
                input_.send(action);
                recorder_.record(snap, obs, action);
            } else {
                input_.release_all();
            }
        } else {
            if (episode_started && !bot_active_) {
                recorder_.end_episode();
                episode_started = false;
            }
            if (!bot_active_) {
                input_.release_all();
            }
        }

        // Sleep to maintain 120Hz
        auto elapsed = steady_clock::now() - frame_start;
        auto frame_budget = duration<double, std::milli>(Config::BOT_FRAME_MS);
        if (elapsed < frame_budget) {
            std::this_thread::sleep_for(frame_budget - elapsed);
        }
    }

    input_.release_all();
    recorder_.end_episode();
}

// ── overlay_loop ──────────────────────────────────────────────────────────────
void BotController::overlay_loop() {
    while (running_) {
        overlay_.update(bot_active_, game_reader_.last(), recorder_);
        std::this_thread::sleep_for(100ms);  // 10Hz
    }
}

// ── handle_hotkeys ────────────────────────────────────────────────────────────
void BotController::handle_hotkeys() {
    // GetAsyncKeyState works from any thread on the same desktop
    static bool f6_was_down = false;
    bool f6_down = (GetAsyncKeyState(Config::HOTKEY_TOGGLE) & 0x8000) != 0;

    if (f6_down && !f6_was_down) {
        bot_active_ = !bot_active_;
        std::cout << "[BotController] Bot " << (bot_active_ ? "ENABLED" : "DISABLED") << "\n";
        if (!bot_active_) {
            input_.release_all();
        }
    }
    f6_was_down = f6_down;
}
