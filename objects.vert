#version 450

struct Transform {
    mat4 CLIP_FROM_LOCAL;   // transformation that gets to clip space directly from the object's local space
    mat4 WORLD_FROM_LOCAL;  // transformations that get to world space (the space lighting computation is done)
    mat4 WORLD_FROM_LOCAL_NORMAL;   // normals
};

// how a storage buffer in vertex shader is declared
layout(set = 1, binding = 0, std140) readonly buffer Transforms {
    Transform TRANSFORMS[];
};

layout(location = 0) in vec3 Position;   // position supplied as a vertex attribute, will copy into the 'gl_Position' output
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;

void main() {
    gl_Position = TRANSFORMS[gl_InstanceIndex].CLIP_FROM_LOCAL * vec4(Position, 1.0);
    position = mat4x3(TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL) * vec4(Position, 1.0);
    normal = mat3(TRANSFORMS[gl_InstanceIndex].WORLD_FROM_LOCAL_NORMAL) * Normal;
    texCoord = TexCoord;
}