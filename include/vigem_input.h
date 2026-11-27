#pragma once
#include "rl_structs.h"
#include <memory>

// ViGEmClient forward decls
struct _VIGEM_CLIENT_T;
struct _VIGEM_TARGET_T;

// ============================================================
//  Sends controller input to Rocket League via a virtual Xbox
//  controller. Requires ViGEmBus driver to be installed.
//  https://github.com/nefarius/ViGEmBus/releases
//
//  Input is sent as an XUSB_REPORT (Xbox 360 controller format):
//    Left stick  X → steer / yaw
//    Left stick  Y → throttle (inverted for reverse)
//    Right stick X → pitch
//    Right stick Y → roll
//    A button      → jump
//    X button      → boost
//    B button      → handbrake (powerslide)
// ============================================================

class VigemInput {
public:
    VigemInput() = default;
    ~VigemInput();

    VigemInput(const VigemInput&) = delete;
    VigemInput& operator=(const VigemInput&) = delete;

    // Connect to ViGEmBus and plug in a virtual Xbox 360 pad.
    // Returns false if the driver is not installed.
    bool init();

    // Send a controller state derived from a Nexto action.
    void send(const NextoAction& action);

    // Release all buttons and center all sticks.
    void release_all();

    // Unplug the virtual controller and disconnect from ViGEmBus.
    void shutdown();

    bool is_ready() const { return ready_; }

private:
    _VIGEM_CLIENT_T* client_ = nullptr;
    _VIGEM_TARGET_T* target_ = nullptr;
    bool ready_ = false;

    // Convert [-1, 1] float to int16 axis value
    static int16_t to_axis(float v);
    // Convert bool-ish float to button mask bit
    static bool    to_btn(float v);
};
