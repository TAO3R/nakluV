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

layout(location = 0) in vec2 texCoord;
layout(location = 0) out float outOcclusion;

void main() {
    float d = texture(gDepth, texCoord).r;
    if (d >= 1.0) {
        outOcclusion = 1.0;
        return;
    }

    vec3 worldPos = texture(gPosition, texCoord).xyz;
    vec3 worldNormal = normalize(texture(gNormal, texCoord).xyz);

    vec3 fragPosView = (VIEW_FROM_WORLD * vec4(worldPos, 1.0)).xyz;
    vec3 normalView = normalize(mat3(VIEW_FROM_WORLD) * worldNormal);

    vec3 randomVec = texture(noiseTex, texCoord * vec2(noise_scale_x, noise_scale_y)).xyz;

    vec3 tangent = normalize(randomVec - normalView * dot(randomVec, normalView));
    vec3 bitangent = cross(normalView, tangent);
    mat3 TBN = mat3(tangent, bitangent, normalView);

    float occlusion = 0.0;
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
        float sampledDepthView = (VIEW_FROM_WORLD * vec4(sampledWorldPos, 1.0)).z;

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPosView.z - sampledDepthView));
        occlusion += (sampledDepthView >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    outOcclusion = 1.0 - (occlusion / float(count));
}
