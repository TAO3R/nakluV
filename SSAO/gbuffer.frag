#version 450

layout(set = 2, binding = 0) uniform sampler2D TEXTURE;
layout(set = 5, binding = 0) uniform sampler2D NORMAL_MAP;
layout(set = 6, binding = 0) uniform sampler2D ROUGHNESS_MAP;
layout(set = 6, binding = 1) uniform sampler2D METALNESS_MAP;

layout(push_constant) uniform PushConstants {
    uint material_type;
    float eye_x, eye_y, eye_z;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 tangent;
layout(location = 4) in float bitangent_sign;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;

void main() {
    vec3 N = normalize(normal);
    vec3 T = normalize(tangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * bitangent_sign;
    mat3 TBN = mat3(T, B, N);

    vec3 map_n = texture(NORMAL_MAP, texCoord).rgb * 2.0 - 1.0;
    vec3 n = normalize(TBN * map_n);

    vec3 albedo = texture(TEXTURE, texCoord).rgb;
    float roughness = 1.0;
    float metalness = 0.0;

    if (material_type == 3u) {
        roughness = texture(ROUGHNESS_MAP, texCoord).r;
        metalness = texture(METALNESS_MAP, texCoord).r;
    }

    outPosition = vec4(position, float(material_type));
    outNormal = vec4(n, roughness);
    outAlbedo = vec4(albedo, metalness);
}
