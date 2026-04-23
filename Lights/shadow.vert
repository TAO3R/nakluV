#version 450

struct Transform {
    mat4 CLIP_FROM_LOCAL;
    mat4 WORLD_FROM_LOCAL;
    mat4 WORLD_FROM_LOCAL_NORMAL;
};

layout(set = 0, binding = 0, std140) readonly buffer Transforms {
    Transform TRANSFORMS[];
};

layout(push_constant) uniform Push {
    mat4 LIGHT_CLIP_FROM_WORLD;
};

layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec4 Tangent;
layout(location = 3) in vec2 TexCoord;

void main() {
    vec4 world_pos = TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL * vec4(Position, 1.0);
    gl_Position = LIGHT_CLIP_FROM_WORLD * world_pos;
}
