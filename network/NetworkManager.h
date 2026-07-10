// app/src/main/cpp/network/NetworkManager.h
#pragma once

#include "GamePacket.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifdef ENGINE_NETWORKING_ENABLED
#include <yojimbo/yojimbo.h>
#endif

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// NetworkManager
//
// Provides reliable UDP networking via Yojimbo (libyojimbo).
// When ENGINE_NETWORKING_STUB is defined, all methods are no-ops that log
// their intention — useful for single-player testing without a server.
//
// Architecture:
//   • Client-server model (not P2P) — required for anti-cheat
//   • Server authority: server owns all entity states
//   • Client prediction: locally-owned entities are predicted and reconciled
//   • Snapshot interpolation: remote entities are interpolated between snapshots
//
// Yojimbo provides:
//   • Reliable ordered channels (for game events: weapon fires, deaths)
//   • Unreliable unordered channels (for state snapshots: positions every 50ms)
//   • Encryption via libsodium
//   • Connection tokens (secure server matchmaking)
// ─────────────────────────────────────────────────────────────────────────────
class NetworkManager final {
public:
    // Packet receive callback — called on game thread after tick()
    using PacketCallback = std::function<void(const GamePacket&)>;

    NetworkManager() noexcept;
    ~NetworkManager();

    [[nodiscard]] bool init();
    void shutdown();
    void tick(float deltaTime);
    void pause();
    void resume();

    // ── Connection ─────────────────────────────────────────────────────────
    [[nodiscard]] bool connect(
        const std::string& serverAddress,
        uint16_t           serverPort,
        uint64_t           clientId
    );
    void disconnect();

    // ── Packet sending ─────────────────────────────────────────────────────
    // reliable = true: guaranteed delivery via Yojimbo reliable channel
    // reliable = false: unreliable channel (lower latency, may drop)
    void sendPacket(const GamePacket& packet, bool reliable = false);

    // ── Callbacks ──────────────────────────────────────────────────────────
    void setPacketCallback(PacketCallback callback) {
        m_packetCallback = std::move(callback);
    }

    // ── State ──────────────────────────────────────────────────────────────
    [[nodiscard]] bool      isConnected()  const noexcept { return m_connected; }
    [[nodiscard]] float     getPing()      const noexcept { return m_pingMs; }
    [[nodiscard]] uint64_t  getClientId()  const noexcept { return m_clientId; }

private:
#ifdef ENGINE_NETWORKING_ENABLED
    yojimbo::Client*    m_client    = nullptr;
#endif

    PacketCallback  m_packetCallback;
    uint64_t        m_clientId  = 0;
    float           m_pingMs    = 0.0f;
    bool            m_connected = false;
    bool            m_active    = false;

    // Yojimbo configuration
    static constexpr int kReliableChannelIndex   = 0;
    static constexpr int kUnreliableChannelIndex = 1;
};

} // namespace hs
