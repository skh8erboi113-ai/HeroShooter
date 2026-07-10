// app/src/main/cpp/audio/AudioEngine.h
#pragma once

// Oboe — Google's low-latency audio library
// Linked as a prefab package from the AAR dependency
#include <oboe/Oboe.h>

#include <android/asset_manager.h>
#include <atomic>
#include <memory>
#include <cstdint>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// AudioEngine
//
// Wraps Oboe to provide:
//   • Low-latency output stream (targets AAudio, falls back to OpenSL ES)
//   • PCM mixing for concurrent sound effects
//   • Compressed audio (OGG/OPUS) for background music via a decoder thread
//
// Oboe automatically chooses the optimal API and buffer size for the device.
// We set the stream's performance mode to LowLatency to get the smallest
// possible audio buffer, which is critical for weapon sounds and hit feedback.
// ─────────────────────────────────────────────────────────────────────────────
class AudioEngine final : public oboe::AudioStreamDataCallback,
                          public oboe::AudioStreamErrorCallback {
public:
    AudioEngine() noexcept;
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] bool init(AAssetManager* assetManager);
    void shutdown();
    void pause();
    void resume();

    // ── Sound API ──────────────────────────────────────────────────────────
    // Play a one-shot sound effect (fire-and-forget)
    void playSfx(uint32_t soundId, float volume = 1.0f, float pitch = 1.0f);

    // Start/stop looping background music
    void playMusic(uint32_t musicId, float volume = 0.8f);
    void stopMusic();

    // ── Oboe callbacks (called on the high-priority audio thread) ──────────
    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream* stream,
        void*              audioData,
        int32_t            numFrames) override;

    void onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) override;

private:
    [[nodiscard]] bool createOutputStream();
    void mixAudio(float* outputBuffer, int32_t numFrames);

    AAssetManager*                          m_assetManager  = nullptr;
    std::shared_ptr<oboe::AudioStream>      m_outputStream;

    std::atomic<bool>                       m_playing       { false };

    // Mixing state
    static constexpr int32_t kChannelCount  = 2;    // Stereo
    static constexpr int32_t kSampleRate    = 48000; // Match device native rate
};

} // namespace hs
