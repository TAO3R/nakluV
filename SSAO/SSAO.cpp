#include "../Tutorial.hpp"
#include "../Helpers.hpp"
#include "../VK.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

void Tutorial::create_gbuffer_render_pass() {
	std::array<VkAttachmentDescription, 4> attachments{
		VkAttachmentDescription{ // 0 - position (R16G16B16A16_SFLOAT)
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkAttachmentDescription{ // 1 - normal (R16G16B16A16_SFLOAT)
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkAttachmentDescription{ // 2 - albedo (R8G8B8A8_UNORM)
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkAttachmentDescription{ // 3 - depth
			.format = depth_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		},
	};

	std::array<VkAttachmentReference, 3> color_refs{
		VkAttachmentReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
		VkAttachmentReference{.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
		VkAttachmentReference{.attachment = 2, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	};

	VkAttachmentReference depth_ref{
		.attachment = 3,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subpass{
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = uint32_t(color_refs.size()),
		.pColorAttachments = color_refs.data(),
		.pDepthStencilAttachment = &depth_ref,
	};

	std::array<VkSubpassDependency, 1> deps{
		VkSubpassDependency{
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		},
	};

	VkRenderPassCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = uint32_t(attachments.size()),
		.pAttachments = attachments.data(),
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = uint32_t(deps.size()),
		.pDependencies = deps.data(),
	};

	VK(vkCreateRenderPass(rtg.device, &create_info, nullptr, &gbuf_render_pass));
}

void Tutorial::create_gbuffer_images(VkExtent2D extent) {
	destroy_gbuffer_images();

	auto make_color = [&](VkFormat fmt) {
		return rtg.helpers.create_image(
			extent, fmt, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);
	};

	gbuf_position = make_color(VK_FORMAT_R16G16B16A16_SFLOAT);
	gbuf_normal   = make_color(VK_FORMAT_R16G16B16A16_SFLOAT);
	gbuf_albedo   = make_color(VK_FORMAT_R8G8B8A8_UNORM);

	gbuf_depth_image = rtg.helpers.create_image(
		extent, depth_format, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);

	auto make_view = [&](VkImage image, VkFormat fmt) {
		VkImageView view = VK_NULL_HANDLE;
		VkImageViewCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = fmt,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0, .levelCount = 1,
				.baseArrayLayer = 0, .layerCount = 1,
			},
		};
		VK(vkCreateImageView(rtg.device, &ci, nullptr, &view));
		return view;
	};

	gbuf_position_view = make_view(gbuf_position.handle, VK_FORMAT_R16G16B16A16_SFLOAT);
	gbuf_normal_view   = make_view(gbuf_normal.handle,   VK_FORMAT_R16G16B16A16_SFLOAT);
	gbuf_albedo_view   = make_view(gbuf_albedo.handle,   VK_FORMAT_R8G8B8A8_UNORM);

	{
		VkImageViewCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = gbuf_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0, .levelCount = 1,
				.baseArrayLayer = 0, .layerCount = 1,
			},
		};
		VK(vkCreateImageView(rtg.device, &ci, nullptr, &gbuf_depth_view));
	}

	std::array<VkImageView, 4> fb_views{
		gbuf_position_view,
		gbuf_normal_view,
		gbuf_albedo_view,
		gbuf_depth_view,
	};

	VkFramebufferCreateInfo fb_ci{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = gbuf_render_pass,
		.attachmentCount = uint32_t(fb_views.size()),
		.pAttachments = fb_views.data(),
		.width = extent.width,
		.height = extent.height,
		.layers = 1,
	};
	VK(vkCreateFramebuffer(rtg.device, &fb_ci, nullptr, &gbuf_framebuffer));

	if (gbuf_sampler == VK_NULL_HANDLE) {
		VkSamplerCreateInfo sampler_ci{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		};
		VK(vkCreateSampler(rtg.device, &sampler_ci, nullptr, &gbuf_sampler));
	}
}

void Tutorial::destroy_gbuffer_images() {
	if (gbuf_framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(rtg.device, gbuf_framebuffer, nullptr);
		gbuf_framebuffer = VK_NULL_HANDLE;
	}
	if (gbuf_depth_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, gbuf_depth_view, nullptr);
		gbuf_depth_view = VK_NULL_HANDLE;
	}
	if (gbuf_position_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, gbuf_position_view, nullptr);
		gbuf_position_view = VK_NULL_HANDLE;
	}
	if (gbuf_normal_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, gbuf_normal_view, nullptr);
		gbuf_normal_view = VK_NULL_HANDLE;
	}
	if (gbuf_albedo_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, gbuf_albedo_view, nullptr);
		gbuf_albedo_view = VK_NULL_HANDLE;
	}
	if (gbuf_position.handle != VK_NULL_HANDLE) {
		rtg.helpers.destroy_image(std::move(gbuf_position));
	}
	if (gbuf_normal.handle != VK_NULL_HANDLE) {
		rtg.helpers.destroy_image(std::move(gbuf_normal));
	}
	if (gbuf_albedo.handle != VK_NULL_HANDLE) {
		rtg.helpers.destroy_image(std::move(gbuf_albedo));
	}
	if (gbuf_depth_image.handle != VK_NULL_HANDLE) {
		rtg.helpers.destroy_image(std::move(gbuf_depth_image));
	}
}

void Tutorial::create_deferred_descriptors() {
	{
		std::array<VkDescriptorSetLayoutBinding, 4> bindings{
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			VkDescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			VkDescriptorSetLayoutBinding{
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		};

		VkDescriptorSetLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = uint32_t(bindings.size()),
			.pBindings = bindings.data(),
		};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &deferred_set0_GBuffer));
	}

	uint32_t per_workspace = uint32_t(workspaces.size());

	{
		std::array<VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 4 * per_workspace,
			},
		};

		VkDescriptorPoolCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = per_workspace,
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};
		VK(vkCreateDescriptorPool(rtg.device, &ci, nullptr, &deferred_descriptor_pool));
	}

	deferred_workspaces.resize(per_workspace);
	for (auto &dw : deferred_workspaces) {
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = deferred_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &deferred_set0_GBuffer,
		};
		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &dw.gbuffer_descriptors));
	}
}

void Tutorial::update_deferred_descriptors(uint32_t workspace_index) {
	auto &dw = deferred_workspaces[workspace_index];

	std::array<VkDescriptorImageInfo, 4> img_infos{
		VkDescriptorImageInfo{
			.sampler = gbuf_sampler,
			.imageView = gbuf_position_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkDescriptorImageInfo{
			.sampler = gbuf_sampler,
			.imageView = gbuf_normal_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkDescriptorImageInfo{
			.sampler = gbuf_sampler,
			.imageView = gbuf_albedo_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkDescriptorImageInfo{
			.sampler = gbuf_sampler,
			.imageView = gbuf_depth_view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		},
	};

	std::array<VkWriteDescriptorSet, 4> writes{};
	for (uint32_t i = 0; i < 4; ++i) {
		writes[i] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = dw.gbuffer_descriptors,
			.dstBinding = i,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &img_infos[i],
		};
	}

	vkUpdateDescriptorSets(rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}
