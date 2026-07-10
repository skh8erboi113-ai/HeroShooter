// app/src/main/cpp/input/InputManager.h
#pragma once

// AGDK input headers
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <game-activity/GameActivity.h>

#include "../threading/AtomicQueue.h"
#include "../utils/Logger.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <bitset>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Virtual button layout for a twin-stick shooter.
// These IDs are device-agnostic — the input system maps raw touch/gamepad
// events onto these virtual buttons, and gameplay code reads virtual buttons.
//
//   Left stick   ──→ movement
//   Right stick  ──→ aim / camera
//   Button A     ──→ jump
//   Button B     ──→ crouch / slide
//   Button X     ──→ reload
//   Button Y     ──→ melee
//   L1/R1        ──→ ability 1 / ability 2
//   L2/R2        ──→ aim-down-sights / fire (analog)
// ─────────────────────────────────────────────────────────────────────────────
enum class VirtualButton : uint8_t {
    Jump            = 0,
    Crouch          = 1,
    Reload          = 2,
    Melee           = 3,
    Ability1        = 4,
    Ability2        = 5,
    Fire            = 6,
    AimDownSights   = 7,
    Sprint          = 8,
    Interact        = 9,
    Scoreboard      = 10,
    Pause           = 11,
    Count           = 12
};

// ─────────────────────────────────────────────────────────────────────────────
// TouchZone — defines screen regions for on-screen virtual controls.
// Coordinates are in normalised device coordinates [0.0, 1.0].
// ─────────────────────────────────────────────────────────────────────────────
enum class TouchZoneId : uint8_t {
    LeftStick   = 0,    // Left 40% of screen — movement joystick
    RightStick  = 1,    // Right 40% of screen — aim joystick
    FireButton  = 2,    // Bottom-right area
    JumpButton  = 3,
    AbilityBar  = 4,    // Row of ability buttons
    Count       = 5
};

struct TouchZone {
    float   minX, maxX;
    float   minY, maxY;
    bool    isActive    = false;
    int32_t pointerId   = -1;
    float   originX     = 0.0f;
    float   originY     = 0.0f;
    float   currentX    = 0.0f;
    float   currentY    = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// AnalogInput — normalised [-1, 1] axis value with deadzone applied
// ─────────────────────────────────────────────────────────────────────────────
struct AnalogInput {
    float x = 0.0f;
    float y = 0.0f;

    [[nodiscard]] float magnitude() const noexcept {
        return x * x + y * y;   // Returns magnitude squared; caller can sqrt
    }
    [[nodiscard]] bool  isActive()  const noexcept {
        return magnitude() > 0.01f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// InputSnapshot — complete input state for one frame.
// This is the structure that ECS systems (MovementSystem, etc.) read from.
// Designed to be trivially copyable for lock-free passing between threads.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(64) InputSnapshot {
    AnalogInput     moveStick;      // Left stick: movement direction
    AnalogInput     aimStick;       // Right stick: aim direction
    float           fireTrigger;    // Right trigger: [0.0, 1.0]
    float           aimTrigger;     // Left trigger:  [0.0, 1.0]

    // Digital button state — bitset for cache efficiency
    std::bitset<static_cast<size_t>(VirtualButton::Count)> buttonsHeld;
    std::bitset<static_cast<size_t>(VirtualButton::Count)> buttonsPressed;  // Edge: just pressed
    std::bitset<static_cast<size_t>(VirtualButton::Count)> buttonsReleased; // Edge: just released

    uint64_t    frameIndex  = 0;    // Which frame this snapshot belongs to
    float       timestamp   = 0.0f;

    [[nodiscard]] bool isButtonHeld(VirtualButton b) const noexcept {
        return buttonsHeld.test(static_cast<size_t>(b));
    }
    [[nodiscard]] bool isButtonPressed(VirtualButton b) const noexcept {
        return buttonsPressed.test(static_cast<size_t>(b));
    }
    [[nodiscard]] bool isButtonReleased(VirtualButton b) const noexcept {
        return buttonsReleased.test(static_cast<size_t>(b));
    }
};

static_assert(std::is_trivially_copyable_v<AnalogInput>,
    "AnalogInput must be trivially copyable for lock-free snapshot passing");

// ─────────────────────────────────────────────────────────────────────────────
// InputManager
//
// Single point of truth for all input on the game thread.
// Processes raw AGDK touch/key events → produces InputSnapshot each frame.
//
// Architecture:
//   • Called once per frame with the raw event buffer from android_main
//   • Applies deadzone, normalisation, and button edge detection
//   • Produces a single InputSnapshot accessible to all ECS systems
//   • Also handles physical gamepad (via Android KeyEvent / MotionEvent)
//
// Touch Control Strategy (hero shooter specific):
//   • Left half of screen  = floating joystick (origin = first touch point)
//   • Right half of screen = floating aim joystick + fire on tap
//   • Dedicated button zones for abilities
// ─────────────────────────────────────────────────────────────────────────────
class InputManager final {
public:
    InputManager() noexcept;
    ~InputManager() = default;

    InputManager(const InputManager&)            = delete;
    InputManager& operator=(const InputManager&) = delete;

    // ── Initialisation ─────────────────────────────────────────────────────
    void init(int32_t screenWidth, int32_t screenHeight) noexcept;

    // ── Per-frame processing ───────────────────────────────────────────────
    // Call once per frame with the AGDK input buffer before ECS update.
    void processEvents(android_app* app);

    // ── Snapshot access ────────────────────────────────────────────────────
    // Returns the current frame's input snapshot.
    // Valid between processEvents() and the next processEvents() call.
    [[nodiscard]] const InputSnapshot& snapshot() const noexcept {
        return m_currentSnapshot;
    }

    // ── Screen resolution update ───────────────────────────────────────────
    void onScreenResize(int32_t width, int32_t height) noexcept;

    // ── Gamepad detection ──────────────────────────────────────────────────
    [[nodiscard]] bool hasGamepad() const noexcept { return m_gamepadConnected; }

    // ── Haptic feedback ────────────────────────────────────────────────────
    // Request a vibration pulse. Intensity: [0.0, 1.0]. Duration in ms.
    void requestHaptic(float intensity, uint32_t durationMs) noexcept;

private:
    // ── Touch processing ───────────────────────────────────────────────────
    void processTouchEvent(const GameActivityMotionEvent& event);
    void processPointerDown(int32_t pointerId, float x, float y);
    void processPointerMove(int32_t pointerId, float x, float y);
    void processPointerUp(int32_t pointerId);

    // ── Gamepad processing ─────────────────────────────────────────────────
    void processKeyEvent(const GameActivityKeyEvent& event);
    void processGamepadAxis(const GameActivityMotionEvent& event);

    // ── Helpers ────────────────────────────────────────────────────────────
    [[nodiscard]] TouchZoneId identifyTouchZone(float nx, float ny) const noexcept;
    [[nodiscard]] AnalogInput computeJoystickOutput(
        const TouchZone& zone, float deadzone) const noexcept;
    void applyDeadzone(AnalogInput& input, float deadzone) const noexcept;

    void setButtonHeld(VirtualButton b, bool held) noexcept;

    // Convert raw screen pixels to normalised [0,1] coordinates
    [[nodiscard]] float normX(float px) const noexcept { return px / m_screenWidth;  }
    [[nodiscard]] float normY(float py) const noexcept { return py / m_screenHeight; }

private:
    // ── Screen dimensions ──────────────────────────────────────────────────
    float       m_screenWidth   = 1920.0f;
    float       m_screenHeight  = 1080.0f;

    // ── Touch zones ────────────────────────────────────────────────────────
    static constexpr float kLeftStickDeadzone   = 0.12f;
    static constexpr float kRightStickDeadzone  = 0.08f;

    std::array<TouchZone, static_cast<size_t>(TouchZoneId::Count)> m_zones;

    // Active touch tracking — up to 10 simultaneous touches
    static constexpr int32_t kMaxPointers = 10;
    struct ActivePointer {
        bool    active      = false;
        float   startX      = 0.0f;
        float   startY      = 0.0f;
        float   currentX    = 0.0f;
        float   currentY    = 0.0f;
        TouchZoneId zone    = TouchZoneId::Count;   // Count = unassigned
    };
    std::array<ActivePointer, kMaxPointers> m_pointers {};

    // ── Snapshots ──────────────────────────────────────────────────────────
    InputSnapshot   m_currentSnapshot;
    InputSnapshot   m_previousSnapshot;  // For edge detection (pressed/released)

    // ── Button state (raw, before edge detection) ──────────────────────────
    std::bitset<static_cast<size_t>(VirtualButton::Count)> m_rawButtonsHeld;

    // ── Gamepad ────────────────────────────────────────────────────────────
    bool    m_gamepadConnected  = false;
    int32_t m_gamepadDeviceId   = -1;

    // ── Frame counter ──────────────────────────────────────────────────────
    uint64_t m_frameIndex = 0;
};

} // namespace hs
