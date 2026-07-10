// app/src/main/cpp/ecs/components/NetworkComponent.h
#pragma once

#include <cstdint>
#include <array>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// NetworkComponent
//
// Tags an entity as network-replicated.
// The NetworkSystem interpolates/extrapolates remote entity state between
// received snapshots to smooth out network jitter.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) NetworkComponent {
    uint32_t    networkId       = 0;        // Server-assigned unique ID
    uint32_t    ownerId         = 0;        // Client that owns (authoritative) this entity
    uint16_t    lastSnapshotSeq = 0;        // Sequence number of last received snapshot
    bool        isLocallyOwned  = false;    // True only for entities controlled locally

    // Snapshot interpolation buffer
    // Stores the last 8 received state snapshots for interpolation
    static constexpr uint32_t kSnapshotBufferSize = 8;
    struct Snapshot {
        float    timestamp  = 0.0f;
        float    px = 0.0f, py = 0.0f, pz = 0.0f;   // Position
        float    qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f; // Rotation
    };
    std::array<Snapshot, kSnapshotBufferSize> snapshotBuffer {};
    uint8_t     snapshotWriteIdx = 0;

    // Prediction (client-side prediction for locally-owned entities)
    uint16_t    lastAckedInputSeq = 0;
};

} // namespace hs
