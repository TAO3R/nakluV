#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

struct PosNorTexVertex {
    struct {float x,y,z;} Position;
    struct {float x, y, z;} Normal;
    struct {float s, t;} TexCoord;
    // a pipeline vertex input state that works with a buffer holding a PosColVertex[] array:
    static const VkPipelineVertexInputStateCreateInfo array_input_state;
};

// to make sure the structure's layout in memory is as we expected (no padding)
static_assert(sizeof(PosNorTexVertex) == 3*4 + 3*4 + 2*4, "PosNorTexVertex is packed.");