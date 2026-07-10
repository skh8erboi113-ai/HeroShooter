#version 450
// mesh.vert — PBR-ready vertex shader for 3D mesh rendering

// ── Vertex attributes ────────────────────────────────────────────────────────
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;    // xyz=tangent, w=handedness

// ── Per-frame uniform buffer (set 0, binding 0) ───────────────────────────────
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraWorldPos;
    float time;
    vec4 ambientLight;
} frame;

// ── Per-draw push constant (model matrix for 60fps entity rendering) ──────────
// Push constants have near-zero overhead vs. descriptor set updates
layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

// ── Outputs to fragment shader ────────────────────────────────────────────────
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

void main() {
    vec4 worldPos   = push.model * vec4(inPosition, 1.0);
    fragWorldPos    = worldPos.xyz;

    // Transform normal using the inverse transpose of the model matrix
    // (handles non-uniform scaling correctly)
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragNormal      = normalize(normalMatrix * inNormal);
    fragTangent     = normalize(normalMatrix * inTangent.xyz);
    fragBitangent   = cross(fragNormal, fragTangent) * inTangent.w;

    fragTexCoord    = inTexCoord;

    gl_Position     = frame.projection * frame.view * worldPos;
}
