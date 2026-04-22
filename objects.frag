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

layout(std430, set = 8, binding = 0) readonly buffer Lights {
    uint     light_count;
    uint     _h0, _h1, _h2;
    GPULight lights[];
} g_lights;

const float A3_PI = 3.14159265358979323846;

// Epic limit falloff: 1 for infinite/missing/invalid limit; else max(0, 1 - (d/limit)^4)
float lightLimitFalloff(float d, float limit) {
    if (isinf(limit) || isnan(limit) || limit <= 0.0) {
        return 1.0;
    }
    return max(0.0, 1.0 - pow(d / limit, 4.0));
}

// Spot cone attenuation (shared by diffuse and specular)
float evalSpotCone(GPULight Lg, vec3 toCenter) {
    if (Lg.type != 2u || Lg.fov < 1e-5) return 1.0;
    vec3 fromLight = -toCenter; // from light toward surface
    vec3 axis = normalize(Lg.direction); // light forward (-Z)
    float cosDir = dot(normalize(fromLight), axis);
    float halfOuter = 0.5 * Lg.fov;
    float halfInner = halfOuter * (1.0 - Lg.blend);
    float cOuter = cos(halfOuter);
    float cInner = cos(halfInner);
    if (abs(cInner - cOuter) < 1e-4) return float(cosDir >= cOuter);
    return smoothstep(cOuter, cInner, cosDir);
}

// Diffuse (Lambert) direct light from scene lights
vec3 evalDiffuseDirectLights(vec3 n, vec3 worldPos) {
    vec3 sum = vec3(0.0);
    for (uint i = 0u; i < g_lights.light_count; i++) {
        GPULight Lg = g_lights.lights[i];
        if (Lg.type == 0u) {
            // Sun: Lg.direction = photon travel dir; L = opposite (toward light)
            vec3 L = -normalize(Lg.direction);
            float NdotL = max(0.0, dot(n, L));
            sum += Lg.tint * Lg.power * NdotL;
        } else {
            vec3 toLight = Lg.position - worldPos;
            float d2 = max(dot(toLight, toLight), 1e-8);
            float d = sqrt(d2);
            vec3 L = toLight / d;
            float NdotL = max(0.0, dot(n, L));
            float atten = Lg.power / (4.0 * A3_PI * d2);
            float limitFalloff = lightLimitFalloff(d, Lg.limit);
            float spotFactor = evalSpotCone(Lg, toLight);

            sum += Lg.tint * atten * NdotL * limitFalloff * spotFactor;
        }
    }
    return sum;
}

// GGX: α = rough*rough (perceptual, Karis/UE4). D and G1 both use the same a² = (α)² in numerics.
// D = (α)²/π, f = (N·H)²(α²−1) + 1, with α = roughness². So pass a = α, use a2 = a*a inside.
float D_GTR2(float NdotH, float a) {
    float a2 = a * a; // a2 = (rough*rough)², matches Walter/UE NDF
    float nh2 = NdotH * NdotH;
    float f = nh2 * (a2 - 1.0) + 1.0;
    return a2 / (A3_PI * f * f);
}

// Smith G1 (GGX / height-correlated form): must use a², not a, with the same a as D (Frostbite/UE4)
float G1_SmithGGX(float NdotX, float a) {
    float a2 = a * a;
    return 2.0 * NdotX / (NdotX + sqrt(a2 + (1.0 - a2) * (NdotX * NdotX)));
}

vec3 F_Schlick_F0_VH(vec3 F0, float VdotH) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

// Representative point on sphere for specular: closest point on sphere surface to the reflection ray.
// When the ray passes through the sphere (vl < rL), fall back to center direction.
vec3 repPointSphere(vec3 p, vec3 R, vec3 c, float rL) {
    vec3 w = c - p;
    float t = max(0.0, dot(w, R));
    vec3 pLine = p + R * t;
    vec3 v = pLine - c;
    float vl = length(v);
    if (vl < rL) {
        return c; // ray intersects sphere — use center
    }
    return c + (rL / vl) * v;
}

// Sun disc: direction on disc closest to R; D = direction toward sun (from surface)
vec3 sunDirClosestToR(vec3 R, vec3 D, float angDiameter) {
    D = normalize(D);
    R = normalize(R);
    float halfA = 0.5 * angDiameter;
    if (angDiameter < 1e-5) { return D; }
    float c2 = dot(R, D);
    float o = acos(clamp(c2, -1.0, 1.0));
    if (o <= halfA) { return R; }
    if (o < 1e-3) { return D; }
    float k = min(1.0, halfA / o);
    float so = sin(o);
    return (sin((1.0 - k) * o) * D + sin(k * o) * R) / so;
}

// PBR: direct (GGX) specular from all lights, representative point + roughness-widening normalization
vec3 evalSpecularDirectLightsPBR(
    vec3 n, vec3 V, float rough,
    vec3 F0, float NdotV, vec3 worldPos
) {
    float NdotVCl = max(NdotV, 1e-4);
    float alpha = max(0.01, rough) * max(0.01, rough); // α = rough²
    vec3 Rrefl = reflect(-V, n);

    vec3 acc = vec3(0.0);
    for (uint i = 0u; i < g_lights.light_count; i++) {
        GPULight Lg = g_lights.lights[i];
        vec3 L;
        vec3 atten;
        float norm = 1.0;

        if (Lg.type == 0u) {
            // Sun: Lg.direction = photon travel dir; toward-sun = -direction
            vec3 sunDir = -normalize(Lg.direction);
            L = sunDirClosestToR(Rrefl, sunDir, Lg.angle);
            L = normalize(L);
            // Epic-style normalization for sun disc (treat angular radius as source size)
            float sinHalf = sin(0.5 * Lg.angle);
            float aPrime = clamp(alpha + sinHalf * sinHalf, 0.0, 1.0);
            norm = (alpha * alpha) / (aPrime * aPrime);
            atten = Lg.tint * Lg.power;
        } else {
            // Sphere / spot: representative point on emissive sphere
            vec3 toC = Lg.position - worldPos;
            float d2 = max(dot(toC, toC), 1e-8);
            float d = sqrt(d2);

            vec3 rep = repPointSphere(worldPos, Rrefl, Lg.position, Lg.radius);
            vec3 w = rep - worldPos;
            float dS = max(length(w), 1e-4);
            L = w / dS;

            atten = Lg.tint * (Lg.power / (4.0 * A3_PI * d2));
            atten *= lightLimitFalloff(d, Lg.limit);
            atten *= evalSpotCone(Lg, toC);

            // Epic roughness-widening normalization: α' = saturate(α + r/(2d))
            float aPrime = clamp(alpha + Lg.radius / (2.0 * d), 0.0, 1.0);
            norm = (alpha * alpha) / (aPrime * aPrime);
        }

        float NdotL = max(0.0, dot(n, L));
        if (NdotL < 1e-5) { continue; }
        vec3 H = normalize(L + V);
        float NdotH = max(0.0, dot(n, H));
        float VdotH = max(0.0, dot(V, H));

        float Dv = D_GTR2(NdotH, alpha);
        float G = G1_SmithGGX(NdotL, alpha) * G1_SmithGGX(NdotVCl, alpha);
        vec3 Fv = F_Schlick_F0_VH(F0, VdotH);
        // Cook-Torrance: f = DGF/(4·N·L·N·V), then L_o = f * Li * N·L → N·L cancels
        acc += (Dv * G * Fv) * norm * atten / (4.0 * NdotVCl);
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
