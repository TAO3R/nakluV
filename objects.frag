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
};

#include "Materials/tonemap.glsl"

layout(set = 2, binding = 0) uniform sampler2D TEXTURE;

// A2-env
layout(set = 3, binding = 0) uniform samplerCube CUBEMAP;

// A2-diffuse
layout(set = 4, binding = 0) uniform samplerCube LAMBERTIAN_CUBEMAP;

layout(push_constant) uniform PushConstants {
    uint material_type;
    float eye_x, eye_y, eye_z;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(normal);
    vec3 eye = vec3(eye_x, eye_y, eye_z);   // A2-env

    vec3 radiance;

    if (material_type == 1u) // Environment, sample cubemap along the surface normal
    {
        radiance = texture(CUBEMAP, n).rgb;
    }
    else if (material_type == 2u)    // Mirror, reflect the view direction around the surface normal
    {
        vec3 view_dir = normalize(position - eye);
        vec3 refl = reflect(view_dir, n);
        radiance = texture(CUBEMAP, refl).rgb;
    }
    else    // Lambertian (0) or PBR (3)
    {
        vec3 albedo = texture(TEXTURE, texCoord).rgb;

        vec3 e;
        if (has_lambertian == 1u) {
            e = texture(LAMBERTIAN_CUBEMAP, n).rgb;
        } else {
            e = SKY_ENERGY * (0.5 * dot(n, SKY_DIRECTION) + 0.5)
                + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));
        }

        radiance = e * albedo;
    }

    outColor = vec4(apply_tone_map(radiance, exposure_scale, tone_map_mode), 1.0);
}
