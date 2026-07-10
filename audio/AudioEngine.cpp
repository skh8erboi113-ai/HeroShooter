// app/src/main/cpp/audio/AudioEngine.cpp
#include "AudioEngine.h"
#include "../utils/Logger.h"

#include <cstring>  // memset

namespace hs {

AudioEngine::AudioEngine() noexcept {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init(AAssetManager* assetManager) {
    LOG_INFO("AudioEngine::init()");
    m_assetManager = assetManager;
    return createOutputStream();
}

bool AudioEngine::createOutputStream() {
    oboe::AudioStreamBuilder builder;

    builder.setDirection(oboe::Direction::Output)
           .setPerformanceMode(oboe::PerformanceMode::LowLatency)
           // LowLatency maps to AAudio EXCLUSIVE mode on Android 8+
           // which gives the smallest possible audio latency (~5ms on Pixel)
           .setSharingMode(oboe::SharingMode::Exclusive)
           .setFormat(oboe::AudioFormat::Float)       // Float is most efficient
           .setChannelCount(kChannelCount)
           .setSampleRate(kSampleRate)
           .setDataCallback(this)
           .setErrorCallback(this);

    oboe::Result result = builder.openStream(m_outputStream);
    if (result != oboe::Result::OK) {
        LOG_ERROR("AudioEngine: failed to open stream: %s",
                  oboe::convertToText(result));
        return false;
    }

    LOG_INFO("AudioEngine: stream opened — API=%s, latency=%s, bufferSize=%d frames",
             oboe::convertToText(m_outputStream->getAudioApi()),
             oboe::convertToText(m_outputStream->getPerformanceMode()),
             m_outputStream->getBufferSizeInFrames());

    result = m_outputStream->requestStart();
    if (result != oboe::Result::OK) {
        LOG_ERROR("AudioEngine: failed to start stream: %s",
                  oboe::convertToText(result));
        return false;
    }

    m_playing.store(true, std::memory_order_release);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// onAudioReady — called by Oboe on the HIGH PRIORITY audio thread.
// Keep this lock-free. No allocations, no mutexes.
// ─────────────────────────────────────────────────────────────────────────────
oboe::DataCallbackResult AudioEngine::onAudioReady(
    oboe::AudioStream* /*stream*/,
    void* audioData,
    int32_t numFrames)
{
    auto* output = static_cast<float*>(audioData);
    const int32_t totalSamples = numFrames * kChannelCount;

    // Zero the output buffer then mix into it
    memset(output, 0, totalSamples * sizeof(float));

    if (m_playing.load(std::memory_order_acquire)) {
        mixAudio(output, numFrames);
    }

    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::mixAudio(float* outputBuffer, int32_t numFrames) {
    // TODO: Mix active sound sources (PCM buffers) into outputBuffer.
    // This would iterate an array of ActiveSoundSource structs (lock-free,
    // updated via atomic operations from the game thread).
    (void)outputBuffer;
    (void)numFrames;
}

void AudioEngine::onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) {
    LOG_WARN("AudioEngine: stream error '%s' — attempting restart",
             oboe::convertToText(error));
    // Oboe recommends reopening the stream on a separate thread
    // to avoid deadlocking the audio thread
    createOutputStream();
}

void AudioEngine::pause() {
    m_playing.store(false, std::memory_order_release);
    if (m_outputStream) {
        m_outputStream->requestPause();
    }
}

void AudioEngine::resume() {
    if (m_outputStream) {
        m_outputStream->requestStart();
    }
    m_playing.store(true, std::memory_order_release);
}

void AudioEngine::shutdown() {
    m_playing.store(false, std::memory_order_release);
    if (m_outputStream) {
        m_outputStream->stop();
        m_outputStream->close();
        m_outputStream.reset();
    }
    LOG_INFO("AudioEngine shutdown complete");
}

void AudioEngine::playSfx(uint32_t soundId, float /*volume*/, float /*pitch*/) {
    (void)soundId;  // TODO: queue sound for mixing in onAudioReady
}

void AudioEngine::playMusic(uint32_t musicId, float /*volume*/) {
    (void)musicId;  // TODO: start decoder thread for compressed music
}

void AudioEngine::stopMusic() {
    // TODO: signal decoder thread to stop
}

} // namespace hs
