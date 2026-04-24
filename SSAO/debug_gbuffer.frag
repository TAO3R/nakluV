#version 450

layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gDepth;

layout(push_constant) uniform PushConstants {
    uint debug_mode;
    float near_plane;
    float far_plane;
};

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

float linearize_depth(float d, float near, float far) {
    return near * far / (far - d * (far - near));
}

void main() {
    if (debug_mode == 1u) {
        vec3 pos = texture(gPosition, texCoord).xyz;
        outColor = vec4(pos * 0.1, 1.0);
    }
    else if (debug_mode == 2u) {
        vec3 n = texture(gNormal, texCoord).xyz;
        outColor = vec4(n * 0.5 + 0.5, 1.0);
    }
    else if (debug_mode == 3u) {
        vec3 albedo = texture(gAlbedo, texCoord).rgb;
        outColor = vec4(albedo, 1.0);
    }
    else if (debug_mode == 4u) {
        float d = texture(gDepth, texCoord).r;
        if (d >= 1.0) {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
        } else {
            float lin = linearize_depth(d, near_plane, far_plane);
            float vis = (log(lin) - log(near_plane)) / (log(far_plane) - log(near_plane));
            outColor = vec4(vec3(1.0 - clamp(vis, 0.0, 1.0)), 1.0);
        }
    }
    else {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
