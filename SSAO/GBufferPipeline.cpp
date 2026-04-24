#include "../Tutorial.hpp"
#include "../Helpers.hpp"
#include "../VK.hpp"

static uint32_t vert_code[] =
#include "../spv/SSAO/gbuffer.vert.inl"
;

static uint32_t frag_code[] =
#include "../spv/SSAO/gbuffer.frag.inl"
;

void Tutorial::GBufferPipeline::create(RTG &rtg, VkRenderPass render_pass, uint32_t subpass) {
	VkShaderModule vert_module = rtg.helpers.create_shader_module(vert_code);
	VkShaderModule frag_module = rtg.helpers.create_shader_module(frag_code);

	{
		std::array<VkDescriptorSetLayoutBinding, 1> bindings{
			VkDescriptorSetLayoutBinding{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		};
		VkDescriptorSetLayoutCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = uint32_t(bindings.size()), .pBindings = bindings.data()};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &set0_World));
	}
	{
		std::array<VkDescriptorSetLayoutBinding, 1> bindings{
			VkDescriptorSetLayoutBinding{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
		};
		VkDescriptorSetLayoutCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = uint32_t(bindings.size()), .pBindings = bindings.data()};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &set1_Transforms));
	}

	auto make_sampler_layout = [&](VkDescriptorSetLayout *out, uint32_t count) {
		std::vector<VkDescriptorSetLayoutBinding> bindings(count);
		for (uint32_t i = 0; i < count; ++i) {
			bindings[i] = VkDescriptorSetLayoutBinding{.binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
		}
		VkDescriptorSetLayoutCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = count, .pBindings = bindings.data()};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, out));
	};

	make_sampler_layout(&set2_Texture, 1);
	make_sampler_layout(&set3_Cubemap, 1);
	make_sampler_layout(&set4_LambertianCubemap, 1);
	make_sampler_layout(&set5_NormalMap, 1);

	{
		std::array<VkDescriptorSetLayoutBinding, 2> bindings{
			VkDescriptorSetLayoutBinding{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
			VkDescriptorSetLayoutBinding{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		};
		VkDescriptorSetLayoutCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = uint32_t(bindings.size()), .pBindings = bindings.data()};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &set6_PBRMaps));
	}
	{
		std::array<VkDescriptorSetLayoutBinding, 2> bindings{
			VkDescriptorSetLayoutBinding{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
			VkDescriptorSetLayoutBinding{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		};
		VkDescriptorSetLayoutCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = uint32_t(bindings.size()), .pBindings = bindings.data()};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &set7_PBREnv));
	}

	{
		std::array<VkDescriptorSetLayout, 8> layouts{
			set0_World, set1_Transforms, set2_Texture, set3_Cubemap,
			set4_LambertianCubemap, set5_NormalMap, set6_PBRMaps, set7_PBREnv,
		};

		VkPushConstantRange push_range{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(Push),
		};

		VkPipelineLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = uint32_t(layouts.size()),
			.pSetLayouts = layouts.data(),
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &push_range,
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
		VkPipelineInputAssemblyStateCreateInfo input_assembly_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE};
		VkPipelineViewportStateCreateInfo viewport_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};

		VkPipelineRasterizationStateCreateInfo rasterization_state{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE, .rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .depthBiasEnable = VK_FALSE, .lineWidth = 1.0f,
		};

		VkPipelineMultisampleStateCreateInfo multisample_state{.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, .sampleShadingEnable = VK_FALSE};

		VkPipelineDepthStencilStateCreateInfo depth_stencil_state{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS, .depthBoundsTestEnable = VK_FALSE, .stencilTestEnable = VK_FALSE,
		};

		std::array<VkPipelineColorBlendAttachmentState, 3> attachment_states{
			VkPipelineColorBlendAttachmentState{.blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT},
			VkPipelineColorBlendAttachmentState{.blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT},
			VkPipelineColorBlendAttachmentState{.blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT},
		};

		VkPipelineColorBlendStateCreateInfo color_blend_state{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = uint32_t(attachment_states.size()),
			.pAttachments = attachment_states.data(),
			.blendConstants{0.0f, 0.0f, 0.0f, 0.0f},
		};

		VkGraphicsPipelineCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = uint32_t(stages.size()),
			.pStages = stages.data(),
			.pVertexInputState = &Vertex::array_input_state,
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

void Tutorial::GBufferPipeline::destroy(RTG &rtg) {
	if (handle != VK_NULL_HANDLE) { vkDestroyPipeline(rtg.device, handle, nullptr); handle = VK_NULL_HANDLE; }
	if (layout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(rtg.device, layout, nullptr); layout = VK_NULL_HANDLE; }
	if (set7_PBREnv != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set7_PBREnv, nullptr); set7_PBREnv = VK_NULL_HANDLE; }
	if (set6_PBRMaps != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set6_PBRMaps, nullptr); set6_PBRMaps = VK_NULL_HANDLE; }
	if (set5_NormalMap != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set5_NormalMap, nullptr); set5_NormalMap = VK_NULL_HANDLE; }
	if (set4_LambertianCubemap != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set4_LambertianCubemap, nullptr); set4_LambertianCubemap = VK_NULL_HANDLE; }
	if (set3_Cubemap != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set3_Cubemap, nullptr); set3_Cubemap = VK_NULL_HANDLE; }
	if (set2_Texture != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set2_Texture, nullptr); set2_Texture = VK_NULL_HANDLE; }
	if (set1_Transforms != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set1_Transforms, nullptr); set1_Transforms = VK_NULL_HANDLE; }
	if (set0_World != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(rtg.device, set0_World, nullptr); set0_World = VK_NULL_HANDLE; }
}
