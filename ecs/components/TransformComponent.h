// app/src/main/cpp/ecs/components/TransformComponent.h
#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Math types — lightweight, SIMD-friendly.
// In production you'd use glm or a custom SIMD-aligned math library.
// These are intentionally minimal to avoid dependencies at the component level.
// ─────────────────────────────────────────────────────────────────────────────

namespace hs {

struct alignas(16) Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float _pad = 0.0f;  // Pad to 16 bytes for SIMD alignment
};

struct alignas(16) Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

struct alignas(64) Mat4 {
    float m[4][4] = {
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {0,0,0,1}
    };
};

// ─────────────────────────────────────────────────────────────────────────────
// TransformComponent
//
// Represents the world-space position, orientation, and scale of an entity.
// Stored as SoA-friendly data for cache-efficient iteration via EnTT views.
//
// alignas(16): Ensures 16-byte alignment for NEON SIMD operations.
// The model matrix is computed lazily (dirty flag) to avoid per-frame recompute
// when the entity hasn't moved.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) TransformComponent {
    Vec3  position { 0.0f, 0.0f, 0.0f };
    Quat  rotation { 0.0f, 0.0f, 0.0f, 1.0f };
    Vec3  scale    { 1.0f, 1.0f, 1.0f };

    // Cached world matrix — rebuilt when dirty
    mutable Mat4 worldMatrix;
    mutable bool isDirty = true;

    // ── Mutators (mark dirty) ──────────────────────────────────────────────
    void setPosition(const Vec3& pos) noexcept {
        position = pos;
        isDirty  = true;
    }

    void setRotation(const Quat& rot) noexcept {
        rotation = rot;
        isDirty  = true;
    }

    void setScale(const Vec3& s) noexcept {
        scale   = s;
        isDirty = true;
    }

    // ── Lazy world matrix computation ──────────────────────────────────────
    [[nodiscard]] const Mat4& getWorldMatrix() const noexcept {
        if (isDirty) {
            recomputeWorldMatrix();
            isDirty = false;
        }
        return worldMatrix;
    }

private:
    void recomputeWorldMatrix() const noexcept;
};

} // namespace hs
