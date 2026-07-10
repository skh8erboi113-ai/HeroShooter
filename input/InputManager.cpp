// app/src/main/cpp/input/InputManager.cpp
#include "InputManager.h"
#include "../utils/Timer.h"

#include <cmath>
#include <cstring>

// Android gamepad key codes
#include <android/keycodes.h>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
InputManager::InputManager() noexcept {
    // Initialise touch zones with normalised screen-space regions
    // These values target landscape orientation (typical for shooters)

    // Left stick zone: left 45% of screen
    m_zones[static_cast<size_t>(TouchZoneId::LeftStick)] = {
        .minX = 0.0f,  .maxX = 0.45f,
        .minY = 0.0f,  .maxY = 1.0f,
    };
    // Right stick zone: middle-right 40% of screen
    m_zones[static_cast<size_t>(TouchZoneId::RightStick)] = {
        .minX = 0.45f, .maxX = 0.82f,
        .minY = 0.0f,  .maxY = 1.0f,
    };
    // Fire button: bottom-right corner
    m_zones[static_cast<size_t>(TouchZoneId::FireButton)] = {
        .minX = 0.82f, .maxX = 1.0f,
        .minY = 0.4f,  .maxY = 0.75f,
    };
    // Jump button: right side, above fire
    m_zones[static_cast<size_t>(TouchZoneId::JumpButton)] = {
        .minX = 0.82f, .maxX = 1.0f,
        .minY = 0.1f,  .maxY = 0.4f,
    };
    // Ability bar: right side, bottom strip
    m_zones[static_cast<size_t>(TouchZoneId::AbilityBar)] = {
        .minX = 0.5f,  .maxX = 1.0f,
        .minY = 0.75f, .maxY = 1.0f,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::init(int32_t screenWidth, int32_t screenHeight) noexcept {
    m_screenWidth  = static_cast<float>(screenWidth);
    m_screenHeight = static_cast<float>(screenHeight);
    LOG_INFO("InputManager: initialised for %dx%d", screenWidth, screenHeight);
}

void InputManager::onScreenResize(int32_t width, int32_t height) noexcept {
    m_screenWidth  = static_cast<float>(width);
    m_screenHeight = static_cast<float>(height);
    // Reset all active touches to prevent stuck pointers after resize
    m_pointers.fill({});
    for (auto& zone : m_zones) {
        zone.isActive  = false;
        zone.pointerId = -1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::processEvents(android_app* app) {
    ++m_frameIndex;

    // Snapshot previous state for edge detection
    m_previousSnapshot = m_currentSnapshot;
    m_currentSnapshot  = {};
    m_currentSnapshot.frameIndex = m_frameIndex;
    m_currentSnapshot.timestamp  = Timer::nowNs() * 1e-9f;

    // Swap AGDK input buffers — gives us a stable snapshot to iterate
    android_input_buffer* inputBuf = android_app_swap_input_buffers(app);
    if (!inputBuf) {
        // No new events — carry forward stick state from last frame
        m_currentSnapshot.moveStick = m_previousSnapshot.moveStick;
        m_currentSnapshot.aimStick  = m_previousSnapshot.aimStick;
        m_currentSnapshot.buttonsHeld = m_rawButtonsHeld;
        return;
    }

    // ── Process touch / pointer events ────────────────────────────────────
    for (uint64_t i = 0; i < inputBuf->motionEventsCount; ++i) {
        const GameActivityMotionEvent& event = inputBuf->motionEvents[i];

        // Check if this is a gamepad axis event
        if (event.source & AINPUT_SOURCE_GAMEPAD ||
            event.source & AINPUT_SOURCE_JOYSTICK) {
            processGamepadAxis(event);
        } else {
            processTouchEvent(event);
        }
    }

    // ── Process key events ─────────────────────────────────────────────────
    for (uint64_t i = 0; i < inputBuf->keyEventsCount; ++i) {
        processKeyEvent(inputBuf->keyEvents[i]);
    }

    android_app_clear_motion_events(inputBuf);
    android_app_clear_key_events(inputBuf);

    // ── Build snapshot from current touch zone state ───────────────────────
    // Left stick: movement
    m_currentSnapshot.moveStick = computeJoystickOutput(
        m_zones[static_cast<size_t>(TouchZoneId::LeftStick)],
        kLeftStickDeadzone);

    // Right stick: aiming
    m_currentSnapshot.aimStick = computeJoystickOutput(
        m_zones[static_cast<size_t>(TouchZoneId::RightStick)],
        kRightStickDeadzone);

    // Copy button state
    m_currentSnapshot.buttonsHeld = m_rawButtonsHeld;

    // ── Edge detection: pressed = just became held; released = just became not held
    m_currentSnapshot.buttonsPressed  =
        m_currentSnapshot.buttonsHeld & ~m_previousSnapshot.buttonsHeld;
    m_currentSnapshot.buttonsReleased =
        ~m_currentSnapshot.buttonsHeld & m_previousSnapshot.buttonsHeld;
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::processTouchEvent(const GameActivityMotionEvent& event) {
    // AGDK provides the action and pointer index packed into actionMasked
    const int32_t action        = event.action & AMOTION_EVENT_ACTION_MASK;
    const int32_t pointerIdx    = (event.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    // Extract pointer ID and position for the relevant pointer
    // AGDK stores pointer coordinates in the pointers array
    if (pointerIdx < 0 ||
        pointerIdx >= static_cast<int32_t>(event.pointerCount)) return;

    const int32_t pointerId = event.pointers[pointerIdx].id;
    const float   rawX      = GameActivityPointerAxes_getX(&event.pointers[pointerIdx]);
    const float   rawY      = GameActivityPointerAxes_getY(&event.pointers[pointerIdx]);

    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            processPointerDown(pointerId, rawX, rawY);
            break;

        case AMOTION_EVENT_ACTION_MOVE:
            // MOVE events contain ALL active pointers, not just the changed one
            for (int32_t p = 0; p < static_cast<int32_t>(event.pointerCount); ++p) {
                const int32_t pid = event.pointers[p].id;
                const float   mx  = GameActivityPointerAxes_getX(&event.pointers[p]);
                const float   my  = GameActivityPointerAxes_getY(&event.pointers[p]);
                processPointerMove(pid, mx, my);
            }
            break;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            processPointerUp(pointerId);
            break;

        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::processPointerDown(int32_t pointerId, float rawX, float rawY) {
    if (pointerId < 0 || pointerId >= kMaxPointers) return;

    const float nx = normX(rawX);
    const float ny = normY(rawY);
    const TouchZoneId zone = identifyTouchZone(nx, ny);

    // Store active pointer
    m_pointers[pointerId] = {
        .active   = true,
        .startX   = nx, .startY = ny,
        .currentX = nx, .currentY = ny,
        .zone     = zone,
    };

    // Activate the zone and set its joystick origin to the touch-down point
    if (zone != TouchZoneId::Count) {
        auto& z     = m_zones[static_cast<size_t>(zone)];
        z.isActive  = true;
        z.pointerId = pointerId;
        z.originX   = nx;
        z.originY   = ny;
        z.currentX  = nx;
        z.currentY  = ny;

        // Immediate button presses for non-stick zones
        switch (zone) {
            case TouchZoneId::FireButton:
                setButtonHeld(VirtualButton::Fire, true);
                break;
            case TouchZoneId::JumpButton:
                setButtonHeld(VirtualButton::Jump, true);
                break;
            default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::processPointerMove(int32_t pointerId, float rawX, float rawY) {
    if (pointerId < 0 || pointerId >= kMaxPointers) return;
    if (!m_pointers[pointerId].active) return;

    const float nx = normX(rawX);
    const float ny = normY(rawY);
    m_pointers[pointerId].currentX = nx;
    m_pointers[pointerId].currentY = ny;

    const TouchZoneId zone = m_pointers[pointerId].zone;
    if (zone != TouchZoneId::Count) {
        auto& z     = m_zones[static_cast<size_t>(zone)];
        z.currentX  = nx;
        z.currentY  = ny;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::processPointerUp(int32_t pointerId) {
    if (pointerId < 0 || pointerId >= kMaxPointers) return;
    if (!m_pointers[pointerId].active) return;

    const TouchZoneId zone = m_pointers[pointerId].zone;
    m_pointers[pointerId] = {};

    if (zone != TouchZoneId::Count) {
        auto& z     = m_zones[static_cast<size_t>(zone)];
        z.isActive  = false;
        z.pointerId = -1;
        z.originX   = z.currentX = 0.0f;
        z.originY   = z.currentY = 0.0f;

        switch (zone) {
            case TouchZoneId::FireButton:
                setButtonHeld(VirtualButton::Fire, false);
                break;
            case TouchZoneId::JumpButton:
                setButtonHeld(VirtualButton::Jump, false);
                break;
            default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::processKeyEvent(const GameActivityKeyEvent& event) {
    const bool isDown = (event.action == AKEY_EVENT_ACTION_DOWN);

    switch (event.keyCode) {
        // Physical gamepad / keyboard mappings
        case AKEYCODE_BUTTON_A:     setButtonHeld(VirtualButton::Jump,   isDown); break;
        case AKEYCODE_BUTTON_B:     setButtonHeld(VirtualButton::Crouch, isDown); break;
        case AKEYCODE_BUTTON_X:     setButtonHeld(VirtualButton::Reload, isDown); break;
        case AKEYCODE_BUTTON_Y:     setButtonHeld(VirtualButton::Melee,  isDown); break;
        case AKEYCODE_BUTTON_L1:    setButtonHeld(VirtualButton::Ability1, isDown); break;
        case AKEYCODE_BUTTON_R1:    setButtonHeld(VirtualButton::Ability2, isDown); break;
        case AKEYCODE_BUTTON_START: setButtonHeld(VirtualButton::Pause,  isDown); break;
        case AKEYCODE_SPACE:        setButtonHeld(VirtualButton::Jump,   isDown); break;
        case AKEYCODE_R:            setButtonHeld(VirtualButton::Reload, isDown); break;
        case AKEYCODE_E:            setButtonHeld(VirtualButton::Interact, isDown); break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::processGamepadAxis(const GameActivityMotionEvent& event) {
    if (event.pointerCount == 0) return;
    const auto& ptr = event.pointers[0];

    // Left stick
    const float lx = GameActivityPointerAxes_getAxisValue(&ptr, AMOTION_EVENT_AXIS_X);
    const float ly = GameActivityPointerAxes_getAxisValue(&ptr, AMOTION_EVENT_AXIS_Y);

    // Right stick
    const float rx = GameActivityPointerAxes_getAxisValue(&ptr, AMOTION_EVENT_AXIS_Z);
    const float ry = GameActivityPointerAxes_getAxisValue(&ptr, AMOTION_EVENT_AXIS_RZ);

    // Triggers
    const float lt = GameActivityPointerAxes_getAxisValue(&ptr, AMOTION_EVENT_AXIS_LTRIGGER);
    const float rt = GameActivityPointerAxes_getAxisValue(&ptr, AMOTION_EVENT_AXIS_RTRIGGER);

    m_currentSnapshot.moveStick  = { lx, -ly };   // Invert Y: up = positive
    m_currentSnapshot.aimStick   = { rx, -ry };
    m_currentSnapshot.aimTrigger = lt;
    m_currentSnapshot.fireTrigger= rt;

    applyDeadzone(m_currentSnapshot.moveStick, kLeftStickDeadzone);
    applyDeadzone(m_currentSnapshot.aimStick,  kRightStickDeadzone);

    // Fire from trigger
    setButtonHeld(VirtualButton::Fire,
                  m_currentSnapshot.fireTrigger > 0.5f);
    setButtonHeld(VirtualButton::AimDownSights,
                  m_currentSnapshot.aimTrigger  > 0.5f);

    m_gamepadConnected = true;
}

// ─────────────────────────────────────────────────────────────────────────────
TouchZoneId InputManager::identifyTouchZone(float nx, float ny) const noexcept {
    for (size_t i = 0; i < m_zones.size(); ++i) {
        const auto& z = m_zones[i];
        if (nx >= z.minX && nx <= z.maxX &&
            ny >= z.minY && ny <= z.maxY)
        {
            return static_cast<TouchZoneId>(i);
        }
    }
    return TouchZoneId::Count;  // No zone matched
}

// ─────────────────────────────────────────────────────────────────────────────
AnalogInput InputManager::computeJoystickOutput(
    const TouchZone& zone, float deadzone) const noexcept
{
    if (!zone.isActive) return {};

    // Compute displacement from joystick origin
    // The floating joystick follows the touch point — ergonomic for thumbs
    constexpr float kJoystickRadius = 0.12f; // Maximum displacement in normalised units

    float dx = zone.currentX - zone.originX;
    float dy = zone.currentY - zone.originY;

    // Clamp to unit circle
    const float dist = std::sqrt(dx*dx + dy*dy);
    if (dist > kJoystickRadius) {
        const float scale = kJoystickRadius / dist;
        dx *= scale;
        dy *= scale;
    }

    // Normalise to [-1, 1]
    AnalogInput out;
    out.x =  dx / kJoystickRadius;
    out.y = -dy / kJoystickRadius;   // Screen Y increases downward; flip for world space

    applyDeadzone(out, deadzone);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::applyDeadzone(AnalogInput& input, float deadzone) const noexcept {
    const float mag = std::sqrt(input.x * input.x + input.y * input.y);
    if (mag < deadzone) {
        input.x = 0.0f;
        input.y = 0.0f;
        return;
    }
    // Rescale so that deadzone edge maps to 0.0 (avoids sudden jump)
    const float rescaled = (mag - deadzone) / (1.0f - deadzone);
    const float invMag   = 1.0f / mag;
    input.x = input.x * invMag * rescaled;
    input.y = input.y * invMag * rescaled;
}

// ─────────────────────────────────────────────────────────────────────────────
void InputManager::setButtonHeld(VirtualButton b, bool held) noexcept {
    m_rawButtonsHeld.set(static_cast<size_t>(b), held);
}

void InputManager::requestHaptic(float /*intensity*/, uint32_t /*durationMs*/) noexcept {
    // Haptic feedback requires a JNI call to Vibrator.vibrate().
    // In production: store the request in an atomic variable and
    // flush it on the Java thread via a GameActivity helper method.
    LOG_DEBUG("InputManager: haptic requested");
}

} // namespace hs
