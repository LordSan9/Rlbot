#include "overlay.h"
#include "config.h"
#include <cstdio>
#include <cstring>

void ConsoleOverlay::init() {
    AllocConsole();
    console_ = GetStdHandle(STD_OUTPUT_HANDLE);
    // Hide cursor
    CONSOLE_CURSOR_INFO ci{ .dwSize = 1, .bVisible = FALSE };
    SetConsoleCursorInfo(console_, &ci);
    // Set title
    SetConsoleTitleA("RLBot-Win | Tactical HUD");
    // Resize
    SMALL_RECT rect{ 0,0,79,24 };
    SetConsoleWindowInfo(console_, TRUE, &rect);
}

void ConsoleOverlay::goto_xy(int x, int y) {
    COORD c{ (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(console_, c);
}

void ConsoleOverlay::update(bool bot_active,
                            const GameStateSnapshot& snap,
                            const ExperienceRecorder& recorder)
{
    goto_xy(0, 0);

    printf("┌────────────────────────────────────────────┐\n");
    printf("│  RLBot-Win v1.0  |  F6 = Toggle            │\n");
    printf("├────────────────────────────────────────────┤\n");
    printf("│  Bot:  %-6s                               │\n",
           bot_active ? "ON  ▶" : "OFF ■");
    printf("│  Game: %-6s                               │\n",
           snap.game_active ? "Active" : "Idle  ");
    printf("├────────────────────────────────────────────┤\n");

    if (snap.game_active && snap.local_car.is_alive) {
        const auto& p = snap.local_car.phys.position;
        const auto& v = snap.local_car.phys.linear_vel;
        float speed = v.length();
        printf("│  Car:  X%7.0f Y%7.0f Z%5.0f            │\n", p.x, p.y, p.z);
        printf("│  Speed:%6.0f uu/s                         │\n", speed);

        if (snap.local_car.boost_valid)
            printf("│  Boost:%6.1f%%                             │\n",
                   snap.local_car.boost_amount);
        else
            printf("│  Boost: (unverified)                        │\n");
    } else {
        printf("│  Car:  ---                                  │\n");
        printf("│  Speed:---                                  │\n");
        printf("│  Boost:---                                  │\n");
    }

    printf("├────────────────────────────────────────────┤\n");

    if (snap.ball.is_valid) {
        const auto& b = snap.ball.phys.position;
        printf("│  Ball: X%7.0f Y%7.0f Z%5.0f            │\n", b.x, b.y, b.z);
    } else {
        printf("│  Ball: ---                                  │\n");
    }

    printf("├────────────────────────────────────────────┤\n");
    printf("│  Frames recorded: %-8zu                  │\n",
           recorder.current_episode_frames());
    printf("│  Total saved:     %-8llu                  │\n",
           recorder.total_frames_saved());
    printf("└────────────────────────────────────────────┘\n");
    printf("\n  Opponents: %zu\n", snap.opponents.size());
}
