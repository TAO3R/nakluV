#include "../Tutorial.hpp"
#include "../Helpers.hpp"
#include "../VK.hpp"

static uint32_t vert_code[] =
#include "../spv/SSAO/fullscreen.vert.inl"
;

static uint32_t frag_code[] =
#include "../spv/SSAO/ssdo_blur.frag.inl"
;

void Tutorial::SSDOBlurPipeline::create(
	RTG &rtg, VkRenderPass render_pass, uint32_t subpass,
	VkDescriptorSetLayout set0_SSDOInput)
{
	VkShaderModule vert_module = rtg.helpers.create_shader_module(vert_code);
	VkShaderModule frag_module = rtg.helpers.create_shader_module(frag_code);

	{
		VkPipelineLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &set0_SSDOInput,
		};
		VK(vkCreatePipelineLayout(rtg.device, &ci, nullptr, &layout));
	}

	{
		std::array<VkPipelineShaderStageCreateInfo, 2> stages{
			VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main"},
			VkPipelineShaderStageCreateInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main"},
		};

		std::vector<VkDynamicState> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamic_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = uint32_t(dynamic_states.size()), .pDynamicStates = dynamic_states.data()};

		VkPipelineVertexInputStateCreateInfo vertex_input_state{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 0, .vertexAttributeDescriptionCount = 0,
		};

		VkPipelineInputAssemblyStateCreateInfo input_assembly_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE};
		VkPipelineViewportStateCreateInfo viewport_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};

		VkPipelineRasterizationStateCreateInfo rasterization_state{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE, .rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .depthBiasEnable = VK_FALSE, .lineWidth = 1.0f,
		};

		VkPipelineMultisampleStateCreateInfo multisample_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, .sampleShadingEnable = VK_FALSE};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_state{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE,
		};

		std::array<VkPipelineColorBlendAttachmentState, 1> attachment_states{
			VkPipelineColorBlendAttachmentState{.blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT},
		};

		VkPipelineColorBlendStateCreateInfo color_blend_state{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = uint32_t(attachment_states.size()),
			.pAttachments = attachment_states.data(),
		};

		VkGraphicsPipelineCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = uint32_t(stages.size()),
			.pStages = stages.data(),
			.pVertexInputState = &vertex_input_state,
			.pInputAssemblyState = &input_assembly_state,
			.pViewportState = &viewport_state,
			.pRasterizationState = &rasterization_state,
			.pMultisampleState = &multisample_state,
			.pDepthStencilState = &depth_stencil_state,
			.pColorBlendState = &color_blend_state,
			.pDynamicState = &dynamic_state,
			.layout = layout,
			.renderPass = render_pass,
			.subpass = subpass,
		};

		VK(vkCreateGraphicsPipelines(rtg.device, VK_NULL_HANDLE, 1, &ci, nullptr, &handle));

		vkDestroyShaderModule(rtg.device, frag_module, nullptr);
		vkDestroyShaderModule(rtg.device, vert_module, nullptr);
	}
}

void Tutorial::SSDOBlurPipeline::destroy(RTG &rtg) {
	if (handle != VK_NULL_HANDLE) { vkDestroyPipeline(rtg.device, handle, nullptr); handle = VK_NULL_HANDLE; }
	if (layout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(rtg.device, layout, nullptr); layout = VK_NULL_HANDLE; }
}
