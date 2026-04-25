#version 450

layout(set = 0, binding = 0) uniform sampler2D ssdoInput;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outBlurred;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(ssdoInput, 0));
    vec3 result = vec3(0.0);
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x) + 0.5, float(y) + 0.5) * texelSize;
            result += texture(ssdoInput, texCoord + offset).rgb;
        }
    }
    outBlurred = vec4(result / 16.0, 1.0);
}
