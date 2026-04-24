#version 450

layout(set = 0, binding = 0) uniform sampler2D ssaoInput;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out float outBlurred;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoInput, 0));
    float result = 0.0;
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x) + 0.5, float(y) + 0.5) * texelSize;
            result += texture(ssaoInput, texCoord + offset).r;
        }
    }
    outBlurred = result / 16.0;
}
