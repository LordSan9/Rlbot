#include "vigem_input.h"
#include <ViGEm/Client.h>
#include <iostream>
#include <algorithm>
#include <cmath>

// ── Destructor ────────────────────────────────────────────────────────────────
VigemInput::~VigemInput() {
    shutdown();
}

// ── init ─────────────────────────────────────────────────────────────────────
bool VigemInput::init() {
    client_ = vigem_alloc();
    if (!client_) {
        std::cerr << "[ViGEm] vigem_alloc failed\n";
        return false;
    }

    VIGEM_ERROR ret = vigem_connect(client_);
    if (!VIGEM_SUCCESS(ret)) {
        std::cerr << "[ViGEm] vigem_connect failed: 0x" << std::hex << ret << "\n";
        std::cerr << "[ViGEm] Make sure ViGEmBus is installed: "
                  << "https://github.com/nefarius/ViGEmBus/releases\n";
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }

    target_ = vigem_target_x360_alloc();
    if (!target_) {
        std::cerr << "[ViGEm] vigem_target_x360_alloc failed\n";
        vigem_disconnect(client_);
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }

    ret = vigem_target_add(client_, target_);
    if (!VIGEM_SUCCESS(ret)) {
        std::cerr << "[ViGEm] vigem_target_add failed: 0x" << std::hex << ret << "\n";
        vigem_target_free(target_);
        target_ = nullptr;
        vigem_disconnect(client_);
        vigem_free(client_);
        client_ = nullptr;
        return false;
    }

    ready_ = true;
    std::cout << "[ViGEm] Virtual Xbox 360 controller plugged in.\n";
    return true;
}

// ── to_axis ───────────────────────────────────────────────────────────────────
int16_t VigemInput::to_axis(float v) {
    v = std::clamp(v, -1.0f, 1.0f);
    return static_cast<int16_t>(v * 32767.0f);
}

bool VigemInput::to_btn(float v) {
    return v > 0.5f;
}

// ── send ─────────────────────────────────────────────────────────────────────
void VigemInput::send(const NextoAction& a) {
    if (!ready_) return;

    XUSB_REPORT report{};
    XUSB_REPORT_INIT(&report);

    // Left stick: steer=X, throttle=Y
    report.sThumbLX = to_axis(a.steer);
    // Throttle: positive = forward (Y up = forward in XInput for RL)
    report.sThumbLY = to_axis(a.throttle);

    // Right stick: pitch=Y (inverted: pull back = pitch up), yaw is handled by steer in RL
    report.sThumbRX = to_axis(a.yaw);
    report.sThumbRY = to_axis(-a.pitch);  // RL convention: stick up = nose up

    // Throttle: also map to triggers for reverse vs forward
    if (a.throttle >= 0.0f) {
        report.bRightTrigger = static_cast<uint8_t>(a.throttle * 255.0f);
    } else {
        report.bLeftTrigger  = static_cast<uint8_t>(-a.throttle * 255.0f);
    }

    // Buttons
    if (to_btn(a.jump))      report.wButtons |= XUSB_GAMEPAD_A;
    if (to_btn(a.boost))     report.wButtons |= XUSB_GAMEPAD_X;
    if (to_btn(a.handbrake)) report.wButtons |= XUSB_GAMEPAD_B;

    // Roll: map to bumpers (RL default) or right stick X depending on config
    // RL default: Left bumper = roll left, Right bumper = roll right
    // We use a deadzone so small roll values don't trigger buttons spuriously
    if (a.roll < -0.5f) report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (a.roll >  0.5f) report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;

    vigem_target_x360_update(client_, target_, report);
}

// ── release_all ───────────────────────────────────────────────────────────────
void VigemInput::release_all() {
    if (!ready_) return;
    XUSB_REPORT report{};
    XUSB_REPORT_INIT(&report);
    vigem_target_x360_update(client_, target_, report);
}

// ── shutdown ──────────────────────────────────────────────────────────────────
void VigemInput::shutdown() {
    if (!ready_) return;
    release_all();
    if (target_) {
        vigem_target_remove(client_, target_);
        vigem_target_free(target_);
        target_ = nullptr;
    }
    if (client_) {
        vigem_disconnect(client_);
        vigem_free(client_);
        client_ = nullptr;
    }
    ready_ = false;
    std::cout << "[ViGEm] Virtual controller unplugged.\n";
}
