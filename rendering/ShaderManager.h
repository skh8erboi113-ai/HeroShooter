// app/src/main/cpp/rendering/ShaderManager.h
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <string>

// Android asset manager for loading SPIR-V from APK assets
struct AAssetManager;

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// ShaderModule — holds SPIR-V bytecode loaded from disk/assets
// ─────────────────────────────────────────────────────────────────────────────
struct ShaderModule {
    std::vector<uint32_t>   spirv;      // SPIR-V words (not bytes)
    std::string             name;

    [[nodiscard]] const uint32_t* data()      const noexcept { return spirv.data(); }
    [[nodiscard]] uint32_t        byteSize()  const noexcept {
        return static_cast<uint32_t>(spirv.size() * sizeof(uint32_t));
    }
    [[nodiscard]] bool            isEmpty()   const noexcept { return spirv.empty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// ShaderManager
//
// Loads and caches SPIR-V shader modules from Android APK assets.
// Shaders are compiled offline (glslangValidator at build time) and
// stored as .spv files in assets/shaders/.
//
// At runtime, ShaderManager reads them via AAssetManager (direct APK access,
// no extraction to disk needed) and caches them by name.
// ─────────────────────────────────────────────────────────────────────────────
class ShaderManager final {
public:
    ShaderManager()  noexcept = default;
    ~ShaderManager() = default;

    ShaderManager(const ShaderManager&)            = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    [[nodiscard]] bool init(AAssetManager* assetManager);

    // Load a SPIR-V shader from assets/shaders/<name>.spv
    // Subsequent calls with the same name return the cached version.
    [[nodiscard]] const ShaderModule* loadShader(const std::string& name);

    // Preload all shaders matching a pattern (call during loading screen)
    void preloadAll();

    void releaseAll();

private:
    [[nodiscard]] bool loadFromAsset(const std::string& assetPath, ShaderModule& out);

    AAssetManager*                              m_assetManager  = nullptr;
    std::unordered_map<std::string, ShaderModule> m_cache;
};

} // namespace hs
