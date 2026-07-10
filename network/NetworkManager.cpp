// app/src/main/cpp/network/NetworkManager.cpp
#include "NetworkManager.h"
#include "../utils/Logger.h"
#include "../utils/Timer.h"

#ifdef ENGINE_NETWORKING_ENABLED
#include <yojimbo/yojimbo.h>
#endif

#include <cstring>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Yojimbo message type adapter
// Yojimbo uses a message factory pattern — each message type has its own
// class. We wrap our GamePacket in a single yojimbo::Message subclass.
// ─────────────────────────────────────────────────────────────────────────────
#ifdef ENGINE_NETWORKING_ENABLED

// Number of channel types: reliable and unreliable
static constexpr int kNumChannels = 2;

// Yojimbo channel config
static yojimbo::ClientServerConfig buildYojimboConfig() {
    yojimbo::ClientServerConfig config;
    config.numChannels      = kNumChannels;

    // Channel 0: Reliable ordered (game events — weapon fire, deaths)
    config.channel[0].type  = yojimbo::CHANNEL_TYPE_RELIABLE_ORDERED;
    config.channel[0].maxBlockSize = 256 * 1024;    // 256 KB max reliable block

    // Channel 1: Unreliable unordered (state snapshots — positions every 50ms)
    config.channel[1].type  = yojimbo::CHANNEL_TYPE_UNRELIABLE_UNORDERED;

    config.timeout          = 5;    // 5 second connection timeout
    config.maxPacketSize    = 1200; // Below IPv6 MTU (1280 - header overhead)
    return config;
}

// Packet adapter wrapping our GamePacket
class GameMessage final : public yojimbo::Message {
public:
    GamePacket packet {};

    template<typename Stream>
    bool Serialize(Stream& stream) {
        // Yojimbo serialisation macro generates bi-directional read/write code
        serialize_bytes(stream,
            reinterpret_cast<uint8_t*>(&packet), sizeof(GamePacket));
        return true;
    }

    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS();
};

// Message factory — maps message type IDs to message classes
enum MessageType { kGameMessageType = 0, kNumMessageTypes };

YOJIMBO_MESSAGE_FACTORY_START(HsMessageFactory, kNumMessageTypes);
    YOJIMBO_DECLARE_MESSAGE_TYPE(kGameMessageType, GameMessage);
YOJIMBO_MESSAGE_FACTORY_FINISH();

#endif // ENGINE_NETWORKING_ENABLED

// ─────────────────────────────────────────────────────────────────────────────
NetworkManager::NetworkManager() noexcept {}

NetworkManager::~NetworkManager() {
    if (m_connected) disconnect();
    shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::init() {
    LOG_INFO("NetworkManager::init()");

#ifdef ENGINE_NETWORKING_ENABLED
    if (!yojimbo::InitializeYojimbo()) {
        LOG_ERROR("NetworkManager: yojimbo::InitializeYojimbo() failed");
        return false;
    }
    LOG_INFO("NetworkManager: Yojimbo initialised (libsodium version: %s)",
             yojimbo::GetYojimboVersion());
#elif defined(ENGINE_NETWORKING_STUB)
    LOG_WARN("NetworkManager: using STUB implementation (no real networking)");
#endif

    m_active = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool NetworkManager::connect(
    const std::string& serverAddress,
    uint16_t           serverPort,
    uint64_t           clientId)
{
    m_clientId = clientId;
    LOG_INFO("NetworkManager: connecting to %s:%u (clientId=%llu)",
             serverAddress.c_str(), serverPort,
             static_cast<unsigned long long>(clientId));

#ifdef ENGINE_NETWORKING_ENABLED
    static yojimbo::ClientServerConfig config = buildYojimboConfig();

    // In production, obtain a connection token from your auth server via HTTPS.
    // The token is signed by the server using the private key and proves
    // the client is authorised to connect.
    uint8_t privateKey[yojimbo::KeyBytes] {};   // PLACEHOLDER — use real key
    uint8_t connectToken[yojimbo::ConnectTokenBytes] {};

    // Generate a local connect token for LAN / testing
    // Real servers generate this token and send it to clients via REST API
    double expiry  = 45.0;  // Token valid for 45 seconds
    double timeout = 5.0;
    yojimbo::Address addr(serverAddress.c_str(), serverPort);

    if (!yojimbo::GenerateInsecureConnectToken(
            config, connectToken, clientId, addr, expiry, timeout, privateKey)) {
        LOG_ERROR("NetworkManager: failed to generate connect token");
        return false;
    }

    m_client = new yojimbo::Client(
        yojimbo::GetDefaultAllocator(),
        yojimbo::Address("0.0.0.0"),
        config,
        HsMessageFactory(),
        Timer::nowNs() * 1e-9
    );

    m_client->Connect(clientId, connectToken);
    LOG_INFO("NetworkManager: connection initiated");
#endif

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkManager::tick(float deltaTime) {
    if (!m_active) return;

#ifdef ENGINE_NETWORKING_ENABLED
    if (!m_client) return;

    const double timeNow = Timer::nowNs() * 1e-9;
    m_client->AdvanceTime(timeNow);
    m_client->ReceivePackets();

    // ── Receive and dispatch messages ──────────────────────────────────────
    if (m_client->IsConnected()) {
        if (!m_connected) {
            m_connected = true;
            LOG_INFO("NetworkManager: connected to server!");
        }

        // Drain all pending messages from both channels
        for (int channel = 0; channel < kNumChannels; ++channel) {
            yojimbo::Message* msg;
            while ((msg = m_client->ReceiveMessage(channel)) != nullptr) {
                if (msg->GetType() == kGameMessageType) {
                    const auto* gameMsg = static_cast<GameMessage*>(msg);
                    if (m_packetCallback) {
                        m_packetCallback(gameMsg->packet);
                    }
                }
                m_client->ReleaseMessage(msg);
            }
        }

        // Update ping estimate
        const yojimbo::NetworkInfo info = m_client->GetNetworkInfo();
        m_pingMs = info.RTT;
    }
    else if (m_client->IsDisconnected()) {
        if (m_connected) {
            m_connected = false;
            LOG_WARN("NetworkManager: disconnected from server");
        }
    }

    m_client->SendPackets();

#elif defined(ENGINE_NETWORKING_STUB)
    // Stub: simulate 60ms RTT for testing
    m_pingMs    = 60.0f;
    m_connected = true;
    (void)deltaTime;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkManager::sendPacket(const GamePacket& packet, bool reliable) {
#ifdef ENGINE_NETWORKING_ENABLED
    if (!m_client || !m_client->IsConnected()) return;

    const int channel = reliable
        ? kReliableChannelIndex
        : kUnreliableChannelIndex;

    auto* msg = static_cast<GameMessage*>(
        m_client->CreateMessage(kGameMessageType));

    if (!msg) {
        LOG_WARN("NetworkManager: CreateMessage failed (channel %d full?)", channel);
        return;
    }

    msg->packet = packet;
    m_client->SendMessage(channel, msg);
#elif defined(ENGINE_NETWORKING_STUB)
    LOG_DEBUG("NetworkManager [STUB]: sendPacket type=%d reliable=%s",
              static_cast<int>(packet.type), reliable ? "yes" : "no");
    (void)packet;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
void NetworkManager::disconnect() {
#ifdef ENGINE_NETWORKING_ENABLED
    if (m_client) {
        m_client->Disconnect();
    }
#endif
    m_connected = false;
    LOG_INFO("NetworkManager: disconnected");
}

void NetworkManager::pause() {
    m_active = false;
    LOG_INFO("NetworkManager: paused");
}

void NetworkManager::resume() {
    m_active = true;
    LOG_INFO("NetworkManager: resumed");
}

void NetworkManager::shutdown() {
    LOG_INFO("NetworkManager::shutdown()");
    m_active = false;

#ifdef ENGINE_NETWORKING_ENABLED
    if (m_client) {
        if (m_client->IsConnected()) {
            m_client->Disconnect();
        }
        delete m_client;
        m_client = nullptr;
    }
    yojimbo::ShutdownYojimbo();
#endif
}

} // namespace hs
