// app/src/main/cpp/ecs/components/TransformComponent.cpp
#include "TransformComponent.h"

#include <cmath>

namespace hs {

// ─────────────────────────────────────────────────────────────────────────────
// Compute the TRS (Translation * Rotation * Scale) world matrix.
// Uses the quaternion → rotation matrix formula.
// Column-major storage to match Vulkan/GLSL conventions.
//
// M = T * R * S
// Where:
//   T = translation matrix
//   R = rotation matrix from quaternion
//   S = scale matrix
//
// Optimised version avoids the explicit matrix multiplications by
// computing TRS directly from components.
// ─────────────────────────────────────────────────────────────────────────────
void TransformComponent::recomputeWorldMatrix() const noexcept {
    const float qx = rotation.x, qy = rotation.y;
    const float qz = rotation.z, qw = rotation.w;
    const float sx = scale.x,    sy = scale.y, sz = scale.z;

    // Precomputed quaternion products
    const float x2 = qx + qx,  y2 = qy + qy,  z2 = qz + qz;
    const float xx = qx * x2,  xy = qx * y2,  xz = qx * z2;
    const float yy = qy * y2,  yz = qy * z2,  zz = qz * z2;
    const float wx = qw * x2,  wy = qw * y2,  wz = qw * z2;

    // Row-major TRS matrix (GLSL layout(row_major) matches this)
    // Column 0
    worldMatrix.m[0][0] = (1.0f - (yy + zz)) * sx;
    worldMatrix.m[1][0] = (xy + wz)           * sx;
    worldMatrix.m[2][0] = (xz - wy)           * sx;
    worldMatrix.m[3][0] = 0.0f;

    // Column 1
    worldMatrix.m[0][1] = (xy - wz)           * sy;
    worldMatrix.m[1][1] = (1.0f - (xx + zz))  * sy;
    worldMatrix.m[2][1] = (yz + wx)           * sy;
    worldMatrix.m[3][1] = 0.0f;

    // Column 2
    worldMatrix.m[0][2] = (xz + wy)           * sz;
    worldMatrix.m[1][2] = (yz - wx)           * sz;
    worldMatrix.m[2][2] = (1.0f - (xx + yy))  * sz;
    worldMatrix.m[3][2] = 0.0f;

    // Column 3 — translation
    worldMatrix.m[0][3] = position.x;
    worldMatrix.m[1][3] = position.y;
    worldMatrix.m[2][3] = position.z;
    worldMatrix.m[3][3] = 1.0f;
}

} // namespace hs
