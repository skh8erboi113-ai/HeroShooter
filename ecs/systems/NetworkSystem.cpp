// app/src/main/cpp/ecs/systems/NetworkSystem.cpp
#include "NetworkSystem.h"

#include "../components/TransformComponent.h"
#include "../components/NetworkComponent.h"
#include "../components/MovementComponent.h"
#include "../../network/NetworkManager.h"
#include "../../utils/Logger.h"
#include "../../utils/Timer.h"

#include <cmath>    // std::sqrt, std::abs

namespace hs {

NetworkSystem::NetworkSystem(entt::registry& registry) noexcept
    : m_registry(registry)
{}

void NetworkSystem::update(float deltaTime) {
    processInboundPackets();
    interpolateRemoteEntities(deltaTime);
    sendLocalPlayerSnapshot(deltaTime);
}

// ─────────────────────────────────────────────────────────────────────────────
// Called by GameEngine's input processing path (game thread)
// Drains the lock-free queue populated by the network thread
// ─────────────────────────────────────────────────────────────────────────────
void NetworkSystem::processInboundPackets() {
    while (auto optPacket = m_inboundQueue.pop()) {
        const GamePacket& packet = *optPacket;
        switch (packet.type) {
            case PacketType::PlayerStateSnapshot:
                applySnapshot(packet);
                break;

            case PacketType::WeaponFired:
                // Spawn projectile entity using server-authoritative origin
                LOG_DEBUG("NetworkSystem: weapon fired by entity %u",
                          packet.senderId);
                break;

            case PacketType::DamageDealt:
                // Apply damage to target entity's health component
                LOG_DEBUG("NetworkSystem: %u damage to entity %u",
                          packet.damageDealt.damage,
                          packet.damageDealt.targetId);
                break;

            case PacketType::PlayerJoined:
                LOG_INFO("NetworkSystem: player %u joined", packet.senderId);
                break;

            case PacketType::PlayerLeft:
                LOG_INFO("NetworkSystem: player %u left", packet.senderId);
                // TODO: destroy entity with matching networkId
                break;

            default:
                LOG_WARN("NetworkSystem: unhandled packet type %d",
                         static_cast<int>(packet.type));
                break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkSystem::applySnapshot(const GamePacket& packet) {
    const float currentTime = Timer::nowNs() * 1e-9f;

    // Find entity with matching networkId
    auto view = m_registry.view<NetworkComponent, TransformComponent>();
    view.each([&](NetworkComponent& net, TransformComponent& /*xform*/) {
        if (net.networkId != packet.senderId) return;
        if (net.isLocallyOwned) return;  // Don't override local prediction

        // Sequence number check — discard stale packets
        // (Yojimbo reliable channel already handles ordering for reliable msgs;
        //  unreliable snapshots may arrive out-of-order)
        const int16_t seqDelta = static_cast<int16_t>(
            packet.sequenceNum - net.lastSnapshotSeq);
        if (seqDelta <= 0) {
            LOG_DEBUG("NetworkSystem: discarding stale snapshot seq=%u",
                      packet.sequenceNum);
            return;
        }
        net.lastSnapshotSeq = packet.sequenceNum;

        // Store in circular snapshot buffer
        const uint8_t writeIdx = net.snapshotWriteIdx;
        auto& snap          = net.snapshotBuffer[writeIdx];
        snap.timestamp      = currentTime;
        snap.px             = packet.playerState.px;
        snap.py             = packet.playerState.py;
        snap.pz             = packet.playerState.pz;
        snap.qx             = packet.playerState.qx;
        snap.qy             = packet.playerState.qy;
        snap.qz             = packet.playerState.qz;
        snap.qw             = packet.playerState.qw;
        net.snapshotWriteIdx = (writeIdx + 1) % NetworkComponent::kSnapshotBufferSize;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot interpolation for remote entities.
// We render entities at (currentTime - interpolationDelay), interpolating
// between the two snapshots that bracket that target time.
// ─────────────────────────────────────────────────────────────────────────────
void NetworkSystem::interpolateRemoteEntities(float /*deltaTime*/) {
    constexpr float kInterpolationDelay = 0.10f;   // 100ms behind server time
    const float renderTime = Timer::nowNs() * 1e-9f - kInterpolationDelay;

    auto view = m_registry.view<NetworkComponent, TransformComponent>();
    view.each([&](const NetworkComponent& net, TransformComponent& xform) {
        if (net.isLocallyOwned) return;

        // Find the two snapshots that bracket renderTime
        const NetworkComponent::Snapshot* prev = nullptr;
        const NetworkComponent::Snapshot* next = nullptr;

        // Iterate the circular buffer in chronological order
        for (uint32_t i = 0; i < NetworkComponent::kSnapshotBufferSize; ++i) {
            const auto& s = net.snapshotBuffer[i];
            if (s.timestamp <= 0.0f) continue;   // Slot not yet written

            if (s.timestamp <= renderTime) {
                if (!prev || s.timestamp > prev->timestamp) prev = &s;
            } else {
                if (!next || s.timestamp < next->timestamp) next = &s;
            }
        }

        if (!prev) return;  // No data yet

        if (!next) {
            // Only have past data — extrapolate using last known velocity
            // For simplicity: use last position (no extrapolation)
            xform.setPosition({ prev->px, prev->py, prev->pz });
            xform.setRotation({ prev->qx, prev->qy, prev->qz, prev->qw });
            return;
        }

        // ── Linear interpolation between prev and next ─────────────────────
        const float span = next->timestamp - prev->timestamp;
        const float t    = (span > 1e-6f)
                         ? (renderTime - prev->timestamp) / span
                         : 0.0f;

        const float lerped_px = prev->px + (next->px - prev->px) * t;
        const float lerped_py = prev->py + (next->py - prev->py) * t;
        const float lerped_pz = prev->pz + (next->pz - prev->pz) * t;

        // ── SLERP for rotation ──────────────────────────────────────────────
        // Dot product to check hemisphere
        float dot = prev->qx*next->qx + prev->qy*next->qy
                  + prev->qz*next->qz + prev->qw*next->qw;

        // Ensure shortest-arc interpolation
        float nqx = next->qx, nqy = next->qy, nqz = next->qz, nqw = next->qw;
        if (dot < 0.0f) {
            nqx = -nqx; nqy = -nqy; nqz = -nqz; nqw = -nqw;
            dot = -dot;
        }

        // For nearly identical rotations, use linear interpolation (avoids division by zero)
        float slerp_qx, slerp_qy, slerp_qz, slerp_qw;
        if (dot > 0.9995f) {
            slerp_qx = prev->qx + (nqx - prev->qx) * t;
            slerp_qy = prev->qy + (nqy - prev->qy) * t;
            slerp_qz = prev->qz + (nqz - prev->qz) * t;
            slerp_qw = prev->qw + (nqw - prev->qw) * t;
        } else {
            const float angle    = std::acos(dot);
            const float sinAngle = std::sin(angle);
            const float w1       = std::sin((1.0f - t) * angle) / sinAngle;
            const float w2       = std::sin(t * angle)          / sinAngle;
            slerp_qx = w1 * prev->qx + w2 * nqx;
            slerp_qy = w1 * prev->qy + w2 * nqy;
            slerp_qz = w1 * prev->qz + w2 * nqz;
            slerp_qw = w1 * prev->qw + w2 * nqw;
        }

        // Normalise to combat float drift
        const float qLen = std::sqrt(slerp_qx*slerp_qx + slerp_qy*slerp_qy
                                   + slerp_qz*slerp_qz + slerp_qw*slerp_qw);
        if (qLen > 1e-6f) {
            slerp_qx /= qLen; slerp_qy /= qLen;
            slerp_qz /= qLen; slerp_qw /= qLen;
        }

        xform.setPosition({ lerped_px, lerped_py, lerped_pz });
        xform.setRotation({ slerp_qx, slerp_qy, slerp_qz, slerp_qw });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkSystem::sendLocalPlayerSnapshot(float deltaTime) {
    if (!m_networkManager) return;

    m_snapshotTimer += deltaTime;
    if (m_snapshotTimer < m_snapshotInterval) return;
    m_snapshotTimer = 0.0f;

    // Find the locally-owned player entity
    auto view = m_registry.view<
        const NetworkComponent,
        const TransformComponent,
        const MovementComponent>();

    view.each([this](
        const NetworkComponent&   net,
        const TransformComponent& xform,
        const MovementComponent&  move)
    {
        if (!net.isLocallyOwned) return;

        GamePacket packet {};
        packet.type        = PacketType::PlayerStateSnapshot;
        packet.sequenceNum = ++m_inputSequence;
        packet.senderId    = net.networkId;
        packet.timestamp   = Timer::nowNs() * 1e-9f;

        packet.playerState.px = xform.position.x;
        packet.playerState.py = xform.position.y;
        packet.playerState.pz = xform.position.z;
        packet.playerState.qx = xform.rotation.x;
        packet.playerState.qy = xform.rotation.y;
        packet.playerState.qz = xform.rotation.z;
        packet.playerState.qw = xform.rotation.w;
        packet.playerState.vx = move.velocity.x;
        packet.playerState.vy = move.velocity.y;
        packet.playerState.vz = move.velocity.z;

        // Unreliable channel: low latency, may drop. Server will extrapolate
        // between received snapshots. Critical events (weapon fire) use reliable.
        m_networkManager->sendPacket(packet, false);
    });
}

void NetworkSystem::enqueueInboundPacket(const GamePacket& packet) {
    if (!m_inboundQueue.push(packet)) {
        LOG_WARN("NetworkSystem: inbound queue overflow — packet dropped");
    }
}

} // namespace hs
