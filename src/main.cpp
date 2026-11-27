#include "bot_controller.h"
#include <iostream>
#include <csignal>

static BotController* g_bot = nullptr;

static void signal_handler(int) {
    std::cout << "\n[Main] Shutting down...\n";
    if (g_bot) g_bot->stop();
}

int main() {
    std::cout << "RLBot-Win v1.0\n"
              << "Requirements: ViGEmBus driver, Rocket League running\n"
              << "Toggle: F6\n\n";

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    BotController bot;
    g_bot = &bot;

    if (!bot.init()) {
        std::cerr << "[Main] Initialization failed.\n";
        return 1;
    }

    bot.run();

    std::cout << "[Main] Exited cleanly.\n";
    return 0;
}
