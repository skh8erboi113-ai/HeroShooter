// app/src/main/cpp/network/GamePacket.h
#pragma once

#include <cstdint>
#include <cstring>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Packet type enumeration.
// Kept as uint8_t to minimise header overhead.
// ─────────────────────────────────────────────────────────────────────────────
enum class PacketType : uint8_t {
    // Reliable channel (ordered, guaranteed)
    PlayerJoined        = 0x01,
    PlayerLeft          = 0x02,
    WeaponFired         = 0x03,
    DamageDealt         = 0x04,
    PlayerKilled        = 0x05,
    AbilityActivated    = 0x06,
    MatchStarted        = 0x10,
    MatchEnded          = 0x11,

    // Unreliable channel (unordered, may drop — use for snapshots)
    PlayerStateSnapshot = 0x80,
    WorldStateSnapshot  = 0x81,
    InputAck            = 0x82,
};

// ─────────────────────────────────────────────────────────────────────────────
// GamePacket — a tagged union over all packet payloads.
// All fields are packed for network efficiency.
// ─────────────────────────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct GamePacket {
    PacketType  type        = PacketType::PlayerStateSnapshot;
    uint16_t    sequenceNum = 0;    // Monotonically increasing; used for ordering
    uint32_t    senderId    = 0;    // Network entity ID of the sender
    float       timestamp   = 0.0f; // Server time at send

    union {
        // PlayerStateSnapshot: position and rotation of a player
        struct {
            float px, py, pz;           // Position
            float qx, qy, qz, qw;       // Orientation quaternion
            float vx, vy, vz;           // Velocity (for client-side prediction)
            uint8_t  health;
            uint8_t  shield;
        } playerState;

        // WeaponFired: hitscan or projectile spawn
        struct {
            float originX, originY, originZ;
            float dirX, dirY, dirZ;
            uint16_t weaponId;
            uint32_t targetEntityId;    // 0 = missed
        } weaponFired;

        // DamageDealt
        struct {
            uint32_t targetId;
            uint16_t damage;
            uint8_t  damageType;        // 0=bullet, 1=explosion, 2=ability
        } damageDealt;

        // Raw payload for extensibility
        uint8_t rawData[64];
    };
};
#pragma pack(pop)

static_assert(sizeof(GamePacket) <= 128,
    "GamePacket must fit in two cache lines for efficient network copy");

} // namespace hs
