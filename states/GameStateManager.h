// app/src/main/cpp/states/GameStateManager.h
#pragma once

#include <memory>
#include <stack>
#include <string_view>
#include <functional>
#include <cstdint>

namespace hs {

// Forward declarations
class GameEngine;
class VulkanRenderer;
class EntityManager;
class InputManager;

// ─────────────────────────────────────────────────────────────────────────────
// GameState — abstract base for all game states (Loading, Menu, Match, Pause)
//
// The state machine uses a STACK model:
//   • push(PauseMenu) — pause menu overlays the match state
//   • pop()           — returns to match state (PauseMenu is destroyed)
//   • replace(Match)  — swap the entire stack (used for scene transitions)
//
// Each state owns its own ECS sub-registry or entity groups.
// ─────────────────────────────────────────────────────────────────────────────
class GameState {
public:
    explicit GameState(GameEngine& engine) noexcept : m_engine(engine) {}
    virtual ~GameState() = default;

    GameState(const GameState&)            = delete;
    GameState& operator=(const GameState&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────
    // onEnter:   called when pushed onto the stack (once)
    // onSuspend: called when a new state is pushed on top (not destroyed)
    // onResume:  called when the state on top is popped (state is active again)
    // onExit:    called when popped from the stack (just before destruction)
    virtual void onEnter()   {}
    virtual void onSuspend() {}
    virtual void onResume()  {}
    virtual void onExit()    {}

    // ── Per-frame ──────────────────────────────────────────────────────────
    virtual void update(float deltaTime) = 0;
    virtual void render(VulkanRenderer& renderer) = 0;

    // ── Identification ─────────────────────────────────────────────────────
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // ── Rendering control ──────────────────────────────────────────────────
    // If false, the state below this one on the stack is NOT rendered.
    // PauseMenu returns true (we still want to see the game behind the menu).
    [[nodiscard]] virtual bool isTransparent()  const noexcept { return false; }

    // If false, the state below is NOT updated.
    [[nodiscard]] virtual bool allowBelowUpdate() const noexcept { return false; }

protected:
    GameEngine& m_engine;
};

// ─────────────────────────────────────────────────────────────────────────────
// GameStateManager
//
// Controls the stack of active game states.
// Transition requests are queued and applied at the start of the next frame
// to avoid invalidating state pointers during update iteration.
// ─────────────────────────────────────────────────────────────────────────────
class GameStateManager final {
public:
    using StateFactory = std::function<std::unique_ptr<GameState>()>;

    GameStateManager() noexcept = default;
    ~GameStateManager()         = default;

    // ── State control (safe to call from within update()) ─────────────────
    void push(std::unique_ptr<GameState> state);
    void pop();
    void replace(std::unique_ptr<GameState> state);
    void clearAll();

    // ── Per-frame ──────────────────────────────────────────────────────────
    void applyPendingTransitions();
    void update(float deltaTime);
    void render(VulkanRenderer& renderer);

    // ── Queries ────────────────────────────────────────────────────────────
    [[nodiscard]] bool           isEmpty()  const noexcept { return m_stack.empty(); }
    [[nodiscard]] GameState*     top()      const noexcept {
        return m_stack.empty() ? nullptr : m_stack.back().get();
    }
    [[nodiscard]] std::string_view topName() const noexcept {
        return m_stack.empty() ? "none" : m_stack.back()->name();
    }

private:
    enum class TransitionType { Push, Pop, Replace, ClearAll };

    struct PendingTransition {
        TransitionType              type;
        std::unique_ptr<GameState>  state;  // null for Pop/ClearAll
    };

    std::vector<std::unique_ptr<GameState>> m_stack;
    std::vector<PendingTransition>          m_pending;
};

} // namespace hs
