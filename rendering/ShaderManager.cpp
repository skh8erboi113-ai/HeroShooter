// app/src/main/cpp/rendering/ShaderManager.cpp
#include "ShaderManager.h"
#include "../utils/Logger.h"

#include <android/asset_manager.h>
#include <cstring>

namespace hs {

bool ShaderManager::init(AAssetManager* assetManager) {
    if (!assetManager) {
        LOG_ERROR("ShaderManager::init: null AAssetManager");
        return false;
    }
    m_assetManager = assetManager;
    LOG_INFO("ShaderManager initialised");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
const ShaderModule* ShaderManager::loadShader(const std::string& name) {
    // Check cache first
    auto it = m_cache.find(name);
    if (it != m_cache.end()) {
        return &it->second;
    }

    // Build asset path: assets/shaders/<name>.spv
    const std::string assetPath = "shaders/" + name + ".spv";

    ShaderModule module;
    module.name = name;

    if (!loadFromAsset(assetPath, module)) {
        LOG_ERROR("ShaderManager: failed to load '%s'", assetPath.c_str());
        return nullptr;
    }

    // Validate SPIR-V magic number (0x07230203)
    if (module.spirv.empty() || module.spirv[0] != 0x07230203u) {
        LOG_ERROR("ShaderManager: '%s' is not valid SPIR-V (bad magic)", name.c_str());
        return nullptr;
    }

    LOG_INFO("ShaderManager: loaded '%s' (%zu words, %zu bytes)",
             name.c_str(), module.spirv.size(), module.spirv.size() * 4);

    auto [insertIt, _] = m_cache.emplace(name, std::move(module));
    return &insertIt->second;
}

// ─────────────────────────────────────────────────────────────────────────────
bool ShaderManager::loadFromAsset(const std::string& assetPath, ShaderModule& out) {
    AAsset* asset = AAssetManager_open(
        m_assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);

    if (!asset) {
        LOG_ERROR("ShaderManager: AAssetManager_open failed for '%s'",
                  assetPath.c_str());
        return false;
    }

    const off_t assetSize = AAsset_getLength(asset);
    if (assetSize <= 0 || (assetSize % 4) != 0) {
        LOG_ERROR("ShaderManager: invalid SPIR-V file size %ld for '%s'",
                  assetSize, assetPath.c_str());
        AAsset_close(asset);
        return false;
    }

    // Read SPIR-V words directly (file is 4-byte aligned by construction)
    const size_t wordCount = static_cast<size_t>(assetSize) / sizeof(uint32_t);
    out.spirv.resize(wordCount);

    const int bytesRead = AAsset_read(
        asset, out.spirv.data(), static_cast<size_t>(assetSize));

    AAsset_close(asset);

    if (bytesRead != assetSize) {
        LOG_ERROR("ShaderManager: short read on '%s' (%d/%ld bytes)",
                  assetPath.c_str(), bytesRead, assetSize);
        out.spirv.clear();
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
void ShaderManager::preloadAll() {
    // Enumerate shaders directory and load all .spv files
    // This runs during the loading screen to avoid hitching in-game
    static constexpr const char* kShaderNames[] = {
        "mesh",
        "shadow_depth",
        "skybox",
        "particle",
        "ui",
        "post_process",
    };
    for (const char* name : kShaderNames) {
        loadShader(name);
    }
}

void ShaderManager::releaseAll() {
    m_cache.clear();
    LOG_INFO("ShaderManager: all shaders released");
}

} // namespace hs
