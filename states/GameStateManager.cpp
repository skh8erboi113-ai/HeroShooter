// app/src/main/cpp/states/GameStateManager.cpp
#include "GameStateManager.h"
#include "../utils/Logger.h"

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
void GameStateManager::push(std::unique_ptr<GameState> state) {
    m_pending.push_back({
        TransitionType::Push,
        std::move(state)
    });
}

void GameStateManager::pop() {
    m_pending.push_back({ TransitionType::Pop, nullptr });
}

void GameStateManager::replace(std::unique_ptr<GameState> state) {
    m_pending.push_back({
        TransitionType::Replace,
        std::move(state)
    });
}

void GameStateManager::clearAll() {
    m_pending.push_back({ TransitionType::ClearAll, nullptr });
}

// ─────────────────────────────────────────────────────────────────────────────
// Deferred transition processing — safe to call at start of each frame
// ─────────────────────────────────────────────────────────────────────────────
void GameStateManager::applyPendingTransitions() {
    for (auto& transition : m_pending) {
        switch (transition.type) {
            case TransitionType::Push: {
                // Suspend the current top state
                if (!m_stack.empty()) {
                    LOG_INFO("GameState: suspending '%s'",
                             m_stack.back()->name().data());
                    m_stack.back()->onSuspend();
                }
                LOG_INFO("GameState: entering '%s'",
                         transition.state->name().data());
                transition.state->onEnter();
                m_stack.push_back(std::move(transition.state));
                break;
            }

            case TransitionType::Pop: {
                if (m_stack.empty()) {
                    LOG_WARN("GameStateManager::pop: stack is empty");
                    break;
                }
                LOG_INFO("GameState: exiting '%s'",
                         m_stack.back()->name().data());
                m_stack.back()->onExit();
                m_stack.pop_back();

                if (!m_stack.empty()) {
                    LOG_INFO("GameState: resuming '%s'",
                             m_stack.back()->name().data());
                    m_stack.back()->onResume();
                }
                break;
            }

            case TransitionType::Replace: {
                // Exit all current states then push the new one
                while (!m_stack.empty()) {
                    m_stack.back()->onExit();
                    m_stack.pop_back();
                }
                LOG_INFO("GameState: replacing with '%s'",
                         transition.state->name().data());
                transition.state->onEnter();
                m_stack.push_back(std::move(transition.state));
                break;
            }

            case TransitionType::ClearAll: {
                while (!m_stack.empty()) {
                    m_stack.back()->onExit();
                    m_stack.pop_back();
                }
                LOG_INFO("GameStateManager: stack cleared");
                break;
            }
        }
    }
    m_pending.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
void GameStateManager::update(float deltaTime) {
    applyPendingTransitions();

    if (m_stack.empty()) return;

    // Update from top of stack downward, stopping when a state says
    // allowBelowUpdate() == false
    for (int i = static_cast<int>(m_stack.size()) - 1; i >= 0; --i) {
        m_stack[i]->update(deltaTime);
        if (!m_stack[i]->allowBelowUpdate()) break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void GameStateManager::render(VulkanRenderer& renderer) {
    if (m_stack.empty()) return;

    // Find the lowest state that needs rendering
    // (skip states below an opaque state)
    int renderFrom = static_cast<int>(m_stack.size()) - 1;
    while (renderFrom > 0 && m_stack[renderFrom]->isTransparent()) {
        --renderFrom;
    }

    // Render from bottom to top so overlays draw on top
    for (int i = renderFrom; i < static_cast<int>(m_stack.size()); ++i) {
        m_stack[i]->render(renderer);
    }
}

} // namespace hs
