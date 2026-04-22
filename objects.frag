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
    float  shadow;    // resolution hint; unused in lighting for now
    float  _pe, _pf;
};

layout(std430, set = 8, binding = 0) buffer Lights {
    uint     light_count;
    uint     _h0, _h1, _h2;
    GPULight lights[];
} g_lights;

const float A3_PI = 3.14159265358979323846;

// Diffuse (Lambert) direct light from scene lights: contribution before albedo/BRDF (irradiance-like)
vec3 evalDiffuseDirectLights(vec3 n, vec3 worldPos) {
    vec3 sum = vec3(0.0);
    for (uint i = 0u; i < g_lights.light_count; i++) {
        GPULight Lg = g_lights.lights[i];
        if (Lg.type == 0u) {
            // Sun: direction is toward scene; N·L, strength in power
            vec3 L = normalize(Lg.direction);
            float NdotL = max(0.0, dot(n, L));
            sum += Lg.tint * Lg.power * NdotL;
        } else {
            // Sphere / spot: point at center, inverse-square, limit falloff, spot cone
            vec3 toLight = Lg.position - worldPos;
            float d2 = max(dot(toLight, toLight), 1e-8);
            float d = sqrt(d2);
            vec3 L = toLight / d;
            float NdotL = max(0.0, dot(n, L));
            float atten = Lg.power / (4.0 * A3_PI * d2);

            float limitFalloff = 1.0;
            if (!isinf(Lg.limit) && Lg.limit > 0.0) {
                limitFalloff = max(0.0, 1.0 - pow(d / Lg.limit, 4.0));
            }

            float spotFactor = 1.0;
            if (Lg.type == 2u) {
                vec3 wOut = -toLight;                 // from light toward surface
                vec3 axis = normalize(Lg.direction); // light forward
                if (Lg.fov > 1e-5) {
                    float cosDir = dot(normalize(wOut), axis);
                    float halfOuter = 0.5 * Lg.fov;
                    float halfInner = halfOuter * (1.0 - Lg.blend);
                    float cOuter = cos(halfOuter);
                    float cInner = cos(halfInner);
                    if (abs(cInner - cOuter) < 1e-4) {
                        spotFactor = float(cosDir >= cOuter);
                    } else {
                        spotFactor = smoothstep(cOuter, cInner, cosDir);
                    }
                } else {
                    spotFactor = 1.0;
                }
            }

            sum += Lg.tint * atten * NdotL * limitFalloff * spotFactor;
        }
    }
    return sum;
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
        // PBR (split-sum approximation)
        float roughness = texture(ROUGHNESS_MAP, texCoord).r;
        float metalness = texture(METALNESS_MAP, texCoord).r;
        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        vec3 F0 = mix(vec3(0.04), albedo, metalness);
        vec3 V = normalize(eye - position);
        float NdotV = max(dot(n, V), 0.0);
        vec3 R = reflect(-V, n);

        // Specular (split-sum)
        vec3 prefilteredColor = textureLod(GGX_CUBEMAP, R, roughness * ggx_max_lod).rgb;
        vec2 envBRDF = texture(BRDF_LUT, vec2(NdotV, roughness)).rg;
        vec3 specular = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

        // Diffuse
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
        vec3 kD = (1.0 - F) * (1.0 - metalness);

        vec3 irradiance;
        if (has_lambertian == 1u) {
            irradiance = texture(LAMBERTIAN_CUBEMAP, n).rgb;
        } else {
            irradiance = SKY_ENERGY * (0.5 * dot(n, SKY_DIRECTION) + 0.5)
                       + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));
        }
        irradiance += evalDiffuseDirectLights(n, position);
        vec3 diffuse = kD * albedo * irradiance;

        radiance = diffuse + specular;
    }
    else {
        // Lambertian (0)
        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        vec3 e;
        if (has_lambertian == 1u) {
            e = texture(LAMBERTIAN_CUBEMAP, n).rgb;
        } else {
            e = SKY_ENERGY * (0.5 * dot(n, SKY_DIRECTION) + 0.5)
                + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));
        }
        e += evalDiffuseDirectLights(n, position);

        radiance = e * albedo;
    }

    outColor = vec4(apply_tone_map(radiance, exposure_scale, tone_map_mode), 1.0);
}
