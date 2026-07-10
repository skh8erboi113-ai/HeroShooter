#version 450
// mesh.frag — PBR metallic-roughness fragment shader

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

layout(location = 0) out vec4 outColor;

// ── Per-frame uniforms ────────────────────────────────────────────────────────
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraWorldPos;
    float time;
    vec4 ambientLight;
} frame;

// ── Material textures (set 1) ─────────────────────────────────────────────────
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

// ── Material parameters ───────────────────────────────────────────────────────
layout(set = 1, binding = 5) uniform MaterialUBO {
    vec4  baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float emissiveFactor;
    float alphaCutoff;
} mat;

// ── Directional light (simplified for mobile) ────────────────────────────────
const vec3  kLightDir      = normalize(vec3(0.5, 1.0, 0.3));
const vec3  kLightColor    = vec3(1.0, 0.95, 0.85);
const float kLightIntensity = 3.0;

// ── PBR functions ─────────────────────────────────────────────────────────────
const float PI = 3.14159265358979;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdH = max(dot(N, H), 0.0);
    float denom = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // ── Sample textures ────────────────────────────────────────────────────
    vec4 albedo4        = texture(albedoMap, fragTexCoord) * mat.baseColorFactor;
    vec3 albedo         = pow(albedo4.rgb, vec3(2.2));  // sRGB to linear

    vec2 metallicRough  = texture(metallicRoughnessMap, fragTexCoord).bg;
    float metallic      = metallicRough.x * mat.metallicFactor;
    float roughness     = metallicRough.y * mat.roughnessFactor;
    float ao            = texture(aoMap, fragTexCoord).r;
    vec3 emissive       = texture(emissiveMap, fragTexCoord).rgb * mat.emissiveFactor;

    // ── Normal mapping ─────────────────────────────────────────────────────
    vec3 normalSample   = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
    mat3 TBN            = mat3(fragTangent, fragBitangent, fragNormal);
    vec3 N              = normalize(TBN * normalSample);

    // ── Lighting ───────────────────────────────────────────────────────────
    vec3 V              = normalize(frame.cameraWorldPos - fragWorldPos);
    vec3 L              = kLightDir;
    vec3 H              = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Fresnel reflectance at normal incidence
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Cook-Torrance specular BRDF
    float NDF   = DistributionGGX(N, H, roughness);
    float G     = GeometrySchlickGGX(NdotL, roughness)
                * GeometrySchlickGGX(NdotV, roughness);
    vec3  F     = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (NDF * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 Lo = (diffuse + specular) * kLightColor * kLightIntensity * NdotL;

    // Ambient (simplified IBL approximation for mobile)
    vec3 ambient = frame.ambientLight.rgb * albedo * ao;

    vec3 color  = ambient + Lo + emissive;

    // ── Tone mapping (Reinhard) + gamma correction ──────────────────────────
    color       = color / (color + vec3(1.0));
    color       = pow(color, vec3(1.0 / 2.2));

    outColor    = vec4(color, albedo4.a);
}
