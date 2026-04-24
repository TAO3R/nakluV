#include "../Tutorial.hpp"
#include "../Helpers.hpp"
#include "../VK.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>

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
		std::array<VkDescriptorSetLayoutBinding, 5> bindings{
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
			VkDescriptorSetLayoutBinding{
				.binding = 4,
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
				.descriptorCount = 5 * per_workspace,
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

	VkDescriptorImageInfo ao_info{
		.sampler = gbuf_sampler,
		.imageView = (ssao_blur_image_view != VK_NULL_HANDLE) ? ssao_blur_image_view : gbuf_albedo_view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	std::array<VkDescriptorImageInfo, 5> img_infos{
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
		ao_info,
	};

	std::array<VkWriteDescriptorSet, 5> writes{};
	for (uint32_t i = 0; i < 5; ++i) {
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

// ──────────────────────────────────────────────────────────────────────────────
// SSAO
// ──────────────────────────────────────────────────────────────────────────────

void Tutorial::generate_ssao_kernel() {
	std::default_random_engine rng(42);
	std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
	std::uniform_real_distribution<float> distNeg11(-1.0f, 1.0f);

	for (uint32_t i = 0; i < SSAO_MAX_SAMPLES; ++i) {
		float x, y, z;
		do {
			x = distNeg11(rng);
			y = distNeg11(rng);
			z = dist01(rng);
		} while (x*x + y*y + z*z > 1.0f || z < 1e-6f);

		float len = std::sqrt(x*x + y*y + z*z);
		x /= len; y /= len; z /= len;

		float scale = float(i) / float(SSAO_MAX_SAMPLES);
		scale = 0.1f + 0.9f * scale * scale;
		ssao_kernel[i][0] = x * scale;
		ssao_kernel[i][1] = y * scale;
		ssao_kernel[i][2] = z * scale;
		ssao_kernel[i][3] = 0.0f;
	}
}

void Tutorial::create_ssao_noise_texture() {
	std::default_random_engine rng(7);
	std::uniform_real_distribution<float> distNeg11(-1.0f, 1.0f);

	struct Half4 { uint16_t r, g, b, a; };
	auto floatToHalf = [](float f) -> uint16_t {
		uint32_t bits;
		std::memcpy(&bits, &f, 4);
		uint32_t sign = (bits >> 16) & 0x8000;
		int32_t exp = ((bits >> 23) & 0xFF) - 127 + 15;
		uint32_t mantissa = bits & 0x007FFFFF;
		if (exp <= 0) return uint16_t(sign);
		if (exp >= 31) return uint16_t(sign | 0x7C00);
		return uint16_t(sign | (uint32_t(exp) << 10) | (mantissa >> 13));
	};

	Half4 noise[16];
	for (int i = 0; i < 16; ++i) {
		float x = distNeg11(rng);
		float y = distNeg11(rng);
		float len = std::sqrt(x*x + y*y);
		if (len > 1e-6f) { x /= len; y /= len; }
		else { x = 1.0f; y = 0.0f; }
		noise[i] = { floatToHalf(x), floatToHalf(y), floatToHalf(0.0f), floatToHalf(0.0f) };
	}

	VkExtent2D noise_extent{4, 4};
	ssao_noise_image = rtg.helpers.create_image(
		noise_extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);

	rtg.helpers.transfer_to_image(noise, sizeof(noise), ssao_noise_image);

	{
		VkImageViewCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = ssao_noise_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0, .levelCount = 1,
				.baseArrayLayer = 0, .layerCount = 1,
			},
		};
		VK(vkCreateImageView(rtg.device, &ci, nullptr, &ssao_noise_view));
	}

	{
		VkSamplerCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		};
		VK(vkCreateSampler(rtg.device, &ci, nullptr, &ssao_noise_sampler));
	}
}

void Tutorial::create_ssao_render_pass() {
	VkAttachmentDescription attachment{
		.format = VK_FORMAT_R8_UNORM,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkAttachmentReference color_ref{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass{
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_ref,
	};

	VkSubpassDependency dep{
		.srcSubpass = 0,
		.dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	};

	VkRenderPassCreateInfo ci{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &dep,
	};

	VK(vkCreateRenderPass(rtg.device, &ci, nullptr, &ssao_render_pass));
}

void Tutorial::create_ssao_images(VkExtent2D extent) {
	destroy_ssao_images();

	auto make_r8 = [&]() {
		return rtg.helpers.create_image(
			extent, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);
	};

	ssao_image = make_r8();
	ssao_blur_image = make_r8();

	auto make_view = [&](VkImage image) {
		VkImageView view = VK_NULL_HANDLE;
		VkImageViewCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R8_UNORM,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0, .levelCount = 1,
				.baseArrayLayer = 0, .layerCount = 1,
			},
		};
		VK(vkCreateImageView(rtg.device, &ci, nullptr, &view));
		return view;
	};

	ssao_image_view = make_view(ssao_image.handle);
	ssao_blur_image_view = make_view(ssao_blur_image.handle);

	{
		VkFramebufferCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = ssao_render_pass,
			.attachmentCount = 1,
			.pAttachments = &ssao_image_view,
			.width = extent.width,
			.height = extent.height,
			.layers = 1,
		};
		VK(vkCreateFramebuffer(rtg.device, &ci, nullptr, &ssao_framebuffer));
	}

	{
		VkFramebufferCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = ssao_render_pass,
			.attachmentCount = 1,
			.pAttachments = &ssao_blur_image_view,
			.width = extent.width,
			.height = extent.height,
			.layers = 1,
		};
		VK(vkCreateFramebuffer(rtg.device, &ci, nullptr, &ssao_blur_framebuffer));
	}
}

void Tutorial::destroy_ssao_images() {
	if (ssao_blur_framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(rtg.device, ssao_blur_framebuffer, nullptr);
		ssao_blur_framebuffer = VK_NULL_HANDLE;
	}
	if (ssao_framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(rtg.device, ssao_framebuffer, nullptr);
		ssao_framebuffer = VK_NULL_HANDLE;
	}
	if (ssao_blur_image_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, ssao_blur_image_view, nullptr);
		ssao_blur_image_view = VK_NULL_HANDLE;
	}
	if (ssao_image_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, ssao_image_view, nullptr);
		ssao_image_view = VK_NULL_HANDLE;
	}
	if (ssao_blur_image.handle != VK_NULL_HANDLE) {
		rtg.helpers.destroy_image(std::move(ssao_blur_image));
	}
	if (ssao_image.handle != VK_NULL_HANDLE) {
		rtg.helpers.destroy_image(std::move(ssao_image));
	}
}

void Tutorial::create_ssao_descriptors() {
	{
		VkDescriptorSetLayoutBinding binding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		};
		VkDescriptorSetLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 1, .pBindings = &binding,
		};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &ssao_params_set_layout));
	}

	{
		VkDescriptorSetLayoutBinding binding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		};
		VkDescriptorSetLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 1, .pBindings = &binding,
		};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &ssao_noise_set_layout));
	}

	{
		VkDescriptorSetLayoutBinding binding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		};
		VkDescriptorSetLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 1, .pBindings = &binding,
		};
		VK(vkCreateDescriptorSetLayout(rtg.device, &ci, nullptr, &ssao_blur_input_set_layout));
	}

	uint32_t per_workspace = uint32_t(workspaces.size());

	{
		std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = per_workspace,
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 + per_workspace,
			},
		};

		VkDescriptorPoolCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = 1 + 2 * per_workspace,
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};
		VK(vkCreateDescriptorPool(rtg.device, &ci, nullptr, &ssao_descriptor_pool));
	}

	{
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = ssao_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &ssao_noise_set_layout,
		};
		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &ssao_noise_descriptors));

		VkDescriptorImageInfo noise_info{
			.sampler = ssao_noise_sampler,
			.imageView = ssao_noise_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		VkWriteDescriptorSet write{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ssao_noise_descriptors,
			.dstBinding = 0, .dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &noise_info,
		};
		vkUpdateDescriptorSets(rtg.device, 1, &write, 0, nullptr);
	}

	ssao_workspaces.resize(per_workspace);
	for (auto &sw : ssao_workspaces) {
		sw.ssao_params_src = rtg.helpers.create_buffer(
			sizeof(SSAOParams),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);
		sw.ssao_params = rtg.helpers.create_buffer(
			sizeof(SSAOParams),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		{
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = ssao_descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &ssao_params_set_layout,
			};
			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &sw.ssao_params_descriptors));
		}

		VkDescriptorBufferInfo buf_info{
			.buffer = sw.ssao_params.handle,
			.offset = 0,
			.range = sw.ssao_params.size,
		};
		VkWriteDescriptorSet write{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = sw.ssao_params_descriptors,
			.dstBinding = 0, .dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buf_info,
		};
		vkUpdateDescriptorSets(rtg.device, 1, &write, 0, nullptr);

		{
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = ssao_descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &ssao_blur_input_set_layout,
			};
			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &sw.ssao_raw_descriptors));
		}
	}
}

void Tutorial::update_ssao_descriptors(uint32_t workspace_index) {
	auto &sw = ssao_workspaces[workspace_index];

	VkDescriptorImageInfo raw_info{
		.sampler = gbuf_sampler,
		.imageView = ssao_image_view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};
	VkWriteDescriptorSet write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = sw.ssao_raw_descriptors,
		.dstBinding = 0, .dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &raw_info,
	};
	vkUpdateDescriptorSets(rtg.device, 1, &write, 0, nullptr);
}
