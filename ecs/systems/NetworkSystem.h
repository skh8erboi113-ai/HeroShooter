// app/src/main/cpp/ecs/systems/NetworkSystem.h
#pragma once

#include <entt/entt.hpp>
#include "../../network/GamePacket.h"
#include "../../threading/AtomicQueue.h"

#include <cstdint>
#include <vector>

namespace hs {

class NetworkManager;

// ─────────────────────────────────────────────────────────────────────────────
// NetworkSystem
//
// Processes received network packets and applies them to ECS entities.
// Also generates and queues outbound packets (player state snapshots).
//
// Snapshot interpolation:
//   Remote entities (isLocallyOwned=false) are rendered at an interpolated
//   position between the two most recent received snapshots. This hides
//   jitter from variable network latency.
//
//   Interpolation delay = 2 × snapshot interval (100ms for 10Hz snapshots)
//   This trades latency for smoothness — acceptable for non-local entities.
//
// Client-side prediction:
//   The local player entity is updated immediately from input (in MovementSystem)
//   without waiting for server confirmation. When the server sends back the
//   authoritative state, we reconcile if there's a significant discrepancy.
// ─────────────────────────────────────────────────────────────────────────────
class NetworkSystem final {
public:
    NetworkSystem(entt::registry& registry) noexcept;

    void setNetworkManager(NetworkManager* manager) noexcept {
        m_networkManager = manager;
    }

    void update(float deltaTime);

    // Called by NetworkManager when a packet arrives (from network thread via queue)
    void enqueueInboundPacket(const GamePacket& packet);

private:
    void processInboundPackets();
    void applySnapshot(const GamePacket& packet);
    void interpolateRemoteEntities(float deltaTime);
    void sendLocalPlayerSnapshot(float deltaTime);

    entt::registry&     m_registry;
    NetworkManager*     m_networkManager    = nullptr;

    // Lock-free queue: network thread → game thread
    AtomicQueue<GamePacket, 512> m_inboundQueue;

    float   m_snapshotTimer     = 0.0f;
    float   m_snapshotInterval  = 0.050f;   // Send snapshot every 50ms (20Hz)

    // Client-side prediction: input sequence number
    uint16_t m_inputSequence = 0;
};

} // namespace hs
