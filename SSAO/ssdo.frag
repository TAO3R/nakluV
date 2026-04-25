#version 450

layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gDepth;

const int SSAO_MAX_SAMPLES = 64;

layout(std140, set = 1, binding = 0) uniform SSAOParams {
    mat4 VIEW_FROM_WORLD;
    mat4 CLIP_FROM_VIEW;
    vec4 samples[SSAO_MAX_SAMPLES];
    float noise_scale_x, noise_scale_y;
    float radius;
    float bias;
    uint sample_count;
};

layout(set = 2, binding = 0) uniform sampler2D noiseTex;

layout(set = 3, binding = 0, std140) uniform World {
    vec3 SKY_DIRECTION; float _pad0;
    vec3 SKY_ENERGY;    float _pad1;
    vec3 SUN_DIRECTION; float _pad2;
    vec3 SUN_ENERGY;    float _pad3;
    float exposure_scale;
    uint tone_map_mode;
    uint has_lambertian;
    float ggx_max_lod;
};

layout(set = 4, binding = 0) uniform samplerCube LAMBERTIAN_CUBEMAP;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outIndirect;

void main() {
    float d = texture(gDepth, texCoord).r;
    if (d >= 1.0) {
        outIndirect = vec4(0.0);
        return;
    }

    vec3 worldPos = texture(gPosition, texCoord).xyz;
    vec3 worldNormal = normalize(texture(gNormal, texCoord).xyz);

    vec3 fragPosView = (VIEW_FROM_WORLD * vec4(worldPos, 1.0)).xyz;
    mat3 viewRotation = mat3(VIEW_FROM_WORLD);
    vec3 normalView = normalize(viewRotation * worldNormal);

    vec3 randomVec = texture(noiseTex, texCoord * vec2(noise_scale_x, noise_scale_y)).xyz;

    vec3 tangent = normalize(randomVec - normalView * dot(randomVec, normalView));
    vec3 bitangent = cross(normalView, tangent);
    mat3 TBN = mat3(tangent, bitangent, normalView);

    vec3 indirect = vec3(0.0);
    uint count = clamp(sample_count, 1u, uint(SSAO_MAX_SAMPLES));

    for (uint i = 0u; i < count; ++i) {
        vec3 sampleOffset = TBN * samples[i].xyz;
        vec3 samplePos = fragPosView + sampleOffset * radius;

        vec4 offset = CLIP_FROM_VIEW * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        vec2 sampleUV = offset.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        float sampledDepth = texture(gDepth, sampleUV).r;
        if (sampledDepth >= 1.0) continue;

        vec3 sampledWorldPos = texture(gPosition, sampleUV).xyz;
        vec3 blockerPosView = (VIEW_FROM_WORLD * vec4(sampledWorldPos, 1.0)).xyz;

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPosView.z - blockerPosView.z));
        bool isOccluded = (blockerPosView.z >= samplePos.z + bias);

        if (isOccluded && rangeCheck > 0.0) {
            vec3 blockerAlbedo = texture(gAlbedo, sampleUV).rgb;
            vec3 blockerWorldNormal = normalize(texture(gNormal, sampleUV).xyz);

            vec3 blockerIrradiance;
            if (has_lambertian == 1u) {
                blockerIrradiance = texture(LAMBERTIAN_CUBEMAP, blockerWorldNormal).rgb;
            } else {
                blockerIrradiance = SKY_ENERGY * (0.5 * dot(blockerWorldNormal, SKY_DIRECTION) + 0.5)
                    + SUN_ENERGY * max(0.0, dot(blockerWorldNormal, SUN_DIRECTION));
            }

            vec3 dir = blockerPosView - fragPosView;
            float dist2 = dot(dir, dir) + 1e-6;
            vec3 dirN = dir * inversesqrt(dist2);

            float cos_receiver = max(dot(normalView, dirN), 0.0);
            vec3 blockerNormalView = normalize(viewRotation * blockerWorldNormal);
            float cos_sender = max(dot(-dirN, blockerNormalView), 0.0);

            indirect += blockerAlbedo * blockerIrradiance * cos_receiver * cos_sender * rangeCheck;
        }
    }

    outIndirect = vec4(indirect / float(count), 1.0);
}
