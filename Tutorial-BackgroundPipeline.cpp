#include "Tutorial.hpp"

#include "Helpers.hpp"
#include "refsol.hpp"


// static (local to this object file) buffers of SPIR-V code from .inl files
static uint32_t vert_code[] =
#include "spv/background.vert.inl"
;

static uint32_t frag_code[] =
#include "spv/background.frag.inl"
;

void Tutorial::BackgroundPipeline::create(RTG& rtg, VkRenderPass render_pass, uint32_t subpass) {
	/*VkShaderModule vert_module = VK_NULL_HANDLE;
	VkShaderModule frag_module = VK_NULL_HANDLE;*/

	// Vulkan's wrapper that turns a SPIR-V code buffer into shader modules
	VkShaderModule vert_module = rtg.helpers.create_shader_module(vert_code);
	VkShaderModule frag_module = rtg.helpers.create_shader_module(frag_code);

	// shader modules passed to the pipeline creation function, used as the shaders in the created pipeline
	// the refsol pipeline creation function uses compiled-in SPIR-V buffers when VK_NULL_HANDLE is passed for the module parameters
	refsol::BackgroundPipeline_create(rtg, render_pass, subpass, vert_module, frag_module, &layout, &handle);
}

void Tutorial::BackgroundPipeline::destroy(RTG& rtg) {
	refsol::BackgroundPipeline_destroy(rtg, &layout, &handle);
}