#version 450
#extension GL_GOOGLE_include_directive : require

layout(set = 0, binding = 0, std140) uniform World {
    vec3 SKY_DIRECTION; float _pad0;
    vec3 SKY_ENERGY;    float _pad1;

    vec3 SUN_DIRECTION; float _pad2;
    vec3 SUN_ENERGY;    float _pad3;

    // A2-tone
    float exposure_scale;
    uint tone_map_mode;

    // A2-diffuse
    uint has_lambertian;

    // A2-pbr
    float ggx_max_lod;
};

#include "Materials/tonemap.glsl"

layout(set = 2, binding = 0) uniform sampler2D TEXTURE;

// A2-env
layout(set = 3, binding = 0) uniform samplerCube CUBEMAP;

// A2-diffuse
layout(set = 4, binding = 0) uniform samplerCube LAMBERTIAN_CUBEMAP;

// A2-normal
layout(set = 5, binding = 0) uniform sampler2D NORMAL_MAP;

// A2-pbr
layout(set = 6, binding = 0) uniform sampler2D ROUGHNESS_MAP;
layout(set = 6, binding = 1) uniform sampler2D METALNESS_MAP;
layout(set = 7, binding = 0) uniform samplerCube GGX_CUBEMAP;
layout(set = 7, binding = 1) uniform sampler2D BRDF_LUT;

// A3-shadow (spot PCF) — must match ObjectsPipeline::A3_MAX_SHADOW_MAPS
const int A3_MAX_SHADOW_MAPS = 8;
layout(set = 9, binding = 0) uniform sampler2DShadow SHADOW_MAPS[A3_MAX_SHADOW_MAPS];
layout(set = 9, binding = 1) uniform ShadowUBO {
    mat4 light_clip_from_world[A3_MAX_SHADOW_MAPS];
} g_shadow;

// A3-lights (must match Tutorial::GPULight / GPULightHeader in C++)
struct GPULight {
    uint   type;      // 0 = sun, 1 = sphere, 2 = spot
    float  _pad0, _pad1, _pad2;
    vec3   position;
    float  _ppos;
    vec3   direction;
    float  _pdir;
    vec3   tint;
    float  power;     // sun: strength; sphere/spot: power
    float  radius;
    float  limit;
    float  fov;
    float  blend;
    float  angle;     // sun only
    float  shadow;    // shadow map res (0 = no shadow)
    int    shadow_map_index;
    uint   _pad_e;
};

layout(std430, set = 8, binding = 0) readonly buffer Lights {
    uint     light_count;
    uint     _h0, _h1, _h2;
    GPULight lights[];
} g_lights;

const float A3_PI = 3.14159265358979323846;

// 3x3 PCF (9 samples) for spot light shadow map
float shadow_pcf(int map_index, vec3 world_pos, mat4 light_clip_from_world) {
    if (map_index < 0) return 1.0;
    vec4 light_clip = light_clip_from_world * vec4(world_pos, 1.0);
    if (light_clip.w <= 0.0) return 1.0;
    vec3 ndc = light_clip.xyz / light_clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0
        || depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }
    int mi = clamp(map_index, 0, A3_MAX_SHADOW_MAPS - 1);
    float texel = 1.0 / float(textureSize(SHADOW_MAPS[mi], 0).x);
    float s = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(float(x), float(y)) * texel;
            s += texture(SHADOW_MAPS[mi], vec3(uv + offset, depth));
        }
    }
    return s / 9.0;
}

// ──────────────────────────────────────────────
// A3 shared light helpers
// ──────────────────────────────────────────────

float lightLimitFalloff(float d, float limit) {
    if (isinf(limit) || isnan(limit) || limit <= 0.0) return 1.0;
    return max(0.0, 1.0 - pow(d / limit, 4.0));
}

// s72 spot cone: linear in angle between inner and outer half-angles
float evalSpotCone(GPULight Lg, vec3 toCenter) {
    if (Lg.type != 2u || Lg.fov < 1e-5) return 1.0;
    vec3 fromLight = normalize(-toCenter);
    vec3 axis = normalize(Lg.direction); // light forward (-Z)
    float cosDir = dot(fromLight, axis);
    float theta = acos(clamp(cosDir, -1.0, 1.0));
    float thetaOuter = 0.5 * Lg.fov;
    float thetaInner = 0.5 * Lg.fov * (1.0 - Lg.blend);
    if (theta > thetaOuter) return 0.0;
    if (theta <= thetaInner) return 1.0;
    if (abs(thetaOuter - thetaInner) < 1e-5) return 1.0;
    return (thetaOuter - theta) / (thetaOuter - thetaInner);
}

// Horizon N·L approximation for area lights (smooth transition using light angular half-size)
float horizon_ndotl(float mu, float s) {
    if (s < 1e-8) return max(mu, 0.0);
    if (mu > s) return mu;
    if (mu < -s) return 0.0;
    return 0.5 * (mu + s);
}

// sin(half-angle) of the light's angular extent from the shading point
float light_sin_half_angle(GPULight Lg, float dist) {
    if (Lg.type == 0u) return sin(0.5 * Lg.angle);
    return clamp(Lg.radius / max(dist, 1e-6), 0.0, 1.0);
}

// Full angular diameter (radians) of the light from the shading point — for specular rep L
float spec_light_angle_rad(GPULight Lg, float dist) {
    if (Lg.type == 0u) return max(Lg.angle, 0.0);
    return 2.0 * asin(clamp(Lg.radius / max(dist, 1e-6), 0.0, 1.0));
}

// Representative specular L: closest direction on the light disc to the reflection vector.
// Dir = direction toward light center; Angle = angular diameter in radians.
vec3 representative_spec_L(vec3 V, vec3 N, vec3 Dir, float Angle) {
    vec3 r = reflect(-V, N);
    if (Angle < 1e-6) return Dir;
    float half_a = 0.5 * Angle;
    float cos_h = cos(half_a);
    float sin_h = sin(half_a);
    if (dot(r, Dir) > cos_h) return r; // reflection already falls inside the disc
    vec3 to = r - dot(Dir, r) * Dir;
    float to_len = length(to);
    if (to_len < 1e-6) {
        vec3 ortho = cross(Dir, vec3(0.0, 1.0, 0.0));
        if (dot(ortho, ortho) < 1e-8)
            ortho = cross(Dir, vec3(1.0, 0.0, 0.0));
        to = normalize(ortho);
    } else {
        to /= to_len;
    }
    return cos_h * Dir + sin_h * to;
}

// (α / α')² normalization for area-light specular energy conservation
float sphere_light_spec_normalization(float roughness, float lightAngle) {
    float alpha = roughness * roughness;
    float alpha_prime = alpha + 0.5 * sin(lightAngle * 0.5);
    alpha_prime = max(alpha_prime, 1e-5);
    float n = alpha / alpha_prime;
    return n * n;
}

bool incident_light(vec3 worldPos, GPULight Lg, out vec3 L, out vec3 rad) {
    if (Lg.type == 0u) {
        vec3 d = Lg.direction;
        if (dot(d, d) < 1e-10) return false;
        L = -normalize(d); // photon travel dir → toward-light
        rad = Lg.tint * Lg.power;
        return true;
    }
    vec3 toL = Lg.position - worldPos;
    float dist_sq = dot(toL, toL);
    if (dist_sq < 1e-8) return false;
    float dist = sqrt(dist_sq);
    L = toL / dist;
    float d_eff = max(dist, Lg.radius);
    float atten = Lg.power / (4.0 * A3_PI * d_eff * d_eff);
    float lim_atten = lightLimitFalloff(dist, Lg.limit);
    if (lim_atten <= 0.0) return false;
    float spot = evalSpotCone(Lg, toL);
    if (spot <= 0.0) return false;
    rad = Lg.tint * atten * lim_atten * spot;
    if (Lg.type == 2u) {
        if (Lg.shadow_map_index >= 0) {
            int smi = clamp(Lg.shadow_map_index, 0, A3_MAX_SHADOW_MAPS - 1);
            mat4 clip_m = g_shadow.light_clip_from_world[smi];
            float sh = shadow_pcf(smi, worldPos, clip_m);
            rad *= sh;
        }
    }
    return true;
}

// ──────────────────────────────────────────────
// BRDF building blocks
// ──────────────────────────────────────────────

float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float nh2 = NdotH * NdotH;
    float denom = nh2 * (a2 - 1.0) + 1.0;
    return a2 / (A3_PI * denom * denom);
}

float G_SchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

vec3 F_Schlick(vec3 F0, float cosTheta) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Per-light loop: diffuse + specular (PBR path)

vec3 evalDiffuseDirectLights(vec3 n, vec3 worldPos) {
    vec3 sum = vec3(0.0);
    for (uint i = 0u; i < g_lights.light_count; i++) {
        GPULight Lg = g_lights.lights[i];
        vec3 L; vec3 rad;
        if (!incident_light(worldPos, Lg, L, rad)) continue;
        float dist = (Lg.type == 0u) ? 0.0 : length(Lg.position - worldPos);
        float s = light_sin_half_angle(Lg, dist);
        float mu = dot(n, L);
        sum += rad * horizon_ndotl(mu, s);
    }
    return sum;
}

vec3 evalSpecularDirectLightsPBR(
    vec3 n, vec3 V, float roughness,
    vec3 F0, float NdotV, vec3 worldPos
) {
    float NdotVCl = max(NdotV, 1e-4);
    vec3 acc = vec3(0.0);

    for (uint i = 0u; i < g_lights.light_count; i++) {
        GPULight Lg = g_lights.lights[i];
        vec3 L; vec3 rad;
        if (!incident_light(worldPos, Lg, L, rad)) continue;

        float dist = (Lg.type == 0u) ? 0.0 : length(Lg.position - worldPos);
        float lightAngle = spec_light_angle_rad(Lg, dist);
        vec3 L_spec = representative_spec_L(V, n, L, lightAngle);
        float NdotL_spec = max(dot(n, L_spec), 0.0);
        if (NdotL_spec < 1e-5) continue;

        vec3 H_sum = V + L_spec;
        float h_len = length(H_sum);
        vec3 H = (h_len > 1e-6) ? (H_sum / h_len) : n;
        float NdotH = max(dot(n, H), 0.0);
        float VdotH = max(dot(V, H), 0.0);

        vec3 Fv = F_Schlick(F0, VdotH);
        float Dv = D_GGX(NdotH, roughness);
        float Gv = G_Smith(NdotVCl, NdotL_spec, roughness);

        float denom = 4.0 * NdotVCl * NdotL_spec + 0.001;
        vec3 spec = (Dv * Gv * Fv) / denom * rad * NdotL_spec;
        spec *= sphere_light_spec_normalization(roughness, lightAngle);

        acc += spec;
    }
    return acc;
}

layout(push_constant) uniform PushConstants {
    uint material_type;
    float eye_x, eye_y, eye_z;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 tangent;
layout(location = 4) in float bitangent_sign;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(normal);
    vec3 T = normalize(tangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * bitangent_sign;
    mat3 TBN = mat3(T, B, N);

    vec3 map_n = texture(NORMAL_MAP, texCoord).rgb * 2.0 - 1.0;
    vec3 n = normalize(TBN * map_n);

    vec3 eye = vec3(eye_x, eye_y, eye_z);

    vec3 radiance;

    if (material_type == 1u) {
        radiance = texture(CUBEMAP, n).rgb;
    }
    else if (material_type == 2u) {
        vec3 view_dir = normalize(position - eye);
        vec3 refl = reflect(view_dir, n);
        radiance = texture(CUBEMAP, refl).rgb;
    }
    else if (material_type == 3u) {
        // PBR: environment IBL (split-sum approximation) + per-light direct (diffuse + GGX spec)
        float roughness = texture(ROUGHNESS_MAP, texCoord).r;
        float metalness = texture(METALNESS_MAP, texCoord).r;
        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        vec3 F0 = mix(vec3(0.04), albedo, metalness);
        vec3 V = normalize(eye - position);
        float NdotV = max(dot(n, V), 0.0);
        vec3 R = reflect(-V, n);

        // Specular: split-sum IBL + direct GGX (separate, then sum)
        vec3 prefilteredColor = textureLod(GGX_CUBEMAP, R, roughness * ggx_max_lod).rgb;
        vec2 envBRDF = texture(BRDF_LUT, vec2(NdotV, roughness)).rg;
        vec3 specularIbl = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);
        vec3 specularDirect = evalSpecularDirectLightsPBR(
            n, V, roughness, F0, NdotV, position);
        vec3 specular = specularIbl + specularDirect;

        // Diffuse: Lambert term uses (irradiance from env) + (irradiance from scene lights)
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
        vec3 kD = (1.0 - F) * (1.0 - metalness);

        vec3 irradianceIbl;
        if (has_lambertian == 1u) {
            irradianceIbl = texture(LAMBERTIAN_CUBEMAP, n).rgb;
        } else {
            irradianceIbl = SKY_ENERGY * (0.5 * dot(n, SKY_DIRECTION) + 0.5)
                + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));
        }
        vec3 irradianceDirect = evalDiffuseDirectLights(n, position);
        vec3 irradiance = irradianceIbl + irradianceDirect;
        vec3 diffuse = kD * albedo * irradiance;

        radiance = diffuse + specular;
    }
    else {
        // Lambertian: diffuse = albedo * (env irradiance + direct irradiance)
        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        vec3 irradianceIbl;
        if (has_lambertian == 1u) {
            irradianceIbl = texture(LAMBERTIAN_CUBEMAP, n).rgb;
        } else {
            irradianceIbl = SKY_ENERGY * (0.5 * dot(n, SKY_DIRECTION) + 0.5)
                + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));
        }
        vec3 irradianceDirect = evalDiffuseDirectLights(n, position);
        vec3 irradiance = irradianceIbl + irradianceDirect;

        radiance = irradiance * albedo;
    }

    outColor = vec4(apply_tone_map(radiance, exposure_scale, tone_map_mode), 1.0);
}
