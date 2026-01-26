#version 450

layout(set = 0, binding = 0, std140) uniform World {
    vec3 SKY_DIRECTION;
    vec3 SKY_ENERGY;    // energy supplied by sky to a surface path with normal = SKY_DIRECTION

    vec3 SUN_DIRECTION;
    vec3 SUN_ENERGY;    // energy supplied by sun to a surface patch with normal = SUN_DIRECTION
};

layout(set = 2, binding = 0) uniform sampler2D TEXTURE;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(normal);
    vec3 albedo = texture(TEXTURE, texCoord).rgb;

   // hemisphere sky + directional sun:
    vec3 e = SKY_ENERGY * (0.5 * dot(n, SKY_DIRECTION) + 0.5)   // only reaches zero energy when the normal is exactly opposite the light direction
           + SUN_ENERGY * max(0.0, dot(n, SUN_DIRECTION));      // only reaches zero energy when the normal is perpendicular to the light direction
    
    outColor = vec4(e * albedo, 1.0);
}