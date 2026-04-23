#include "../Tutorial.hpp"
#include "../RTG.hpp"
#include "../Helpers.hpp"
#include "../VK.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <variant>
#include <cassert>

void Tutorial::log_lights() {
	static bool logged = false;
	if (logged || light_instances.empty()) return;
	logged = true;

	std::cout << "[A3-load]: Collected " << light_instances.size() << " light instance(s):" << std::endl;
	for (size_t i = 0; i < light_instances.size(); ++i) {
		auto &li = light_instances[i];
		const char *type = "unknown";
		if (std::holds_alternative<S72::Light::Sun>(li.light->source)) type = "sun";
		else if (std::holds_alternative<S72::Light::Sphere>(li.light->source)) type = "sphere";
		else if (std::holds_alternative<S72::Light::Spot>(li.light->source)) type = "spot";
		std::cout << "  [" << i << "] \"" << li.light->name << "\" type=" << type
			<< " pos=(" << li.world_position.x << "," << li.world_position.y << "," << li.world_position.z << ")"
			<< " dir=(" << li.world_direction.x << "," << li.world_direction.y << "," << li.world_direction.z << ")"
			<< " shadow=" << li.shadow
			<< " tint=(" << li.light->tint.r << "," << li.light->tint.g << "," << li.light->tint.b << ")"
			<< std::endl;
	}
}

void Tutorial::upload_lights(Workspace &workspace) {
	// Pack light_instances (filled by traverse_node during update) into gpu_lights for the SSBO
	gpu_lights.clear();
	gpu_lights.reserve(light_instances.size());

	constexpr float INF = std::numeric_limits<float>::infinity();

	for (auto &li : light_instances) {
		assert(li.light != nullptr);
		GPULight gl;
		std::memset(&gl, 0, sizeof(gl));

		gl.position[0] = li.world_position.x;
		gl.position[1] = li.world_position.y;
		gl.position[2] = li.world_position.z;

		gl.direction[0] = li.world_direction.x;
		gl.direction[1] = li.world_direction.y;
		gl.direction[2] = li.world_direction.z;

		gl.tint[0] = li.light->tint.r;
		gl.tint[1] = li.light->tint.g;
		gl.tint[2] = li.light->tint.b;

		gl.shadow = static_cast<float>(li.shadow);

		if (auto *sun = std::get_if<S72::Light::Sun>(&li.light->source)) {
			gl.type   = 0;
			gl.power  = sun->strength;
			gl.radius = 0.f;
			gl.limit  = INF;
			gl.fov    = 0.f;
			gl.blend  = 0.f;
			gl.angle  = sun->angle;
		} else if (auto *sphere = std::get_if<S72::Light::Sphere>(&li.light->source)) {
			gl.type   = 1;
			gl.power  = sphere->power;
			gl.radius = sphere->radius;
			gl.limit  = sphere->limit;
			gl.fov    = 0.f;
			gl.blend  = 0.f;
			gl.angle  = 0.f;
		} else if (auto *spot = std::get_if<S72::Light::Spot>(&li.light->source)) {
			gl.type   = 2;
			gl.power  = spot->power;
			gl.radius = spot->radius;
			gl.limit  = spot->limit;
			gl.fov    = spot->fov;
			gl.blend  = spot->blend;
			gl.angle  = 0.f;
		}

		gpu_lights.push_back(gl);
	}

	// At least the header (count=0 when no lights); never zero — SSBO + descriptor stay valid.
	size_t needed_bytes = sizeof(GPULightHeader) + gpu_lights.size() * sizeof(GPULight);
	assert(needed_bytes >= sizeof(GPULightHeader));

	if (workspace.Lights_src.handle == VK_NULL_HANDLE || workspace.Lights_src.size < needed_bytes) {
		size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;

		if (workspace.Lights_src.handle) {
			rtg.helpers.destroy_buffer(std::move(workspace.Lights_src));
		}
		if (workspace.Lights.handle) {
			rtg.helpers.destroy_buffer(std::move(workspace.Lights));
		}

		workspace.Lights_src = rtg.helpers.create_buffer(
			new_bytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);
		workspace.Lights = rtg.helpers.create_buffer(
			new_bytes,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		VkDescriptorBufferInfo lights_info{
			.buffer = workspace.Lights.handle,
			.offset = 0,
			.range = workspace.Lights.size,
		};

		std::array<VkWriteDescriptorSet, 1> writes{
			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Lights_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.pBufferInfo = &lights_info,
			},
		};

		vkUpdateDescriptorSets(rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	}

	assert(workspace.Lights_src.allocation.mapped);
	uint8_t *dst = reinterpret_cast<uint8_t *>(workspace.Lights_src.allocation.data());

	GPULightHeader header;
	std::memset(&header, 0, sizeof(header));
	header.count = uint32_t(gpu_lights.size());
	std::memcpy(dst, &header, sizeof(header));

	if (!gpu_lights.empty()) {
		std::memcpy(dst + sizeof(header), gpu_lights.data(), gpu_lights.size() * sizeof(GPULight));
	}
	// shader: for (i < light_count) — when count==0, loop body never runs

	VkBufferCopy copy_region{
		.srcOffset = 0,
		.dstOffset = 0,
		.size = needed_bytes,
	};
	assert(copy_region.size > 0);
	vkCmdCopyBuffer(workspace.command_buffer, workspace.Lights_src.handle, workspace.Lights.handle, 1, &copy_region);
}

// =========================================================================
// Shadow map resource management
// =========================================================================

void Tutorial::destroy_shadow_maps() {
	for (auto &sm : shadow_maps) {
		if (sm.framebuffer != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(rtg.device, sm.framebuffer, nullptr);
			sm.framebuffer = VK_NULL_HANDLE;
		}
		if (sm.depth_view != VK_NULL_HANDLE) {
			vkDestroyImageView(rtg.device, sm.depth_view, nullptr);
			sm.depth_view = VK_NULL_HANDLE;
		}
		if (sm.depth_image.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_image(std::move(sm.depth_image));
		}
	}
	shadow_maps.clear();
}

void Tutorial::create_shadow_resources() {
	// --- 1. Create the depth-only render pass (once) ---
	if (shadow_render_pass == VK_NULL_HANDLE) {
		VkAttachmentDescription depth_attachment{
			.format = depth_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		};

		VkAttachmentReference depth_ref{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = 0,
			.pColorAttachments = nullptr,
			.pDepthStencilAttachment = &depth_ref,
		};

		std::array<VkSubpassDependency, 2> deps{
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			},
			VkSubpassDependency{
				.srcSubpass = 0,
				.dstSubpass = VK_SUBPASS_EXTERNAL,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			},
		};

		VkRenderPassCreateInfo rp_ci{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &depth_attachment,
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = uint32_t(deps.size()),
			.pDependencies = deps.data(),
		};

		VK(vkCreateRenderPass(rtg.device, &rp_ci, nullptr, &shadow_render_pass));
	}

	// --- 2. Create the comparison sampler (once) ---
	if (shadow_sampler == VK_NULL_HANDLE) {
		VkSamplerCreateInfo sampler_ci{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			.compareEnable = VK_TRUE,
			.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
		};
		VK(vkCreateSampler(rtg.device, &sampler_ci, nullptr, &shadow_sampler));
	}

	// --- 3. Allocate per-spot-light shadow maps ---
	destroy_shadow_maps();

	for (auto &[name, light] : scene_S72.lights) {
		if (light.shadow == 0) continue;
		if (!std::holds_alternative<S72::Light::Spot>(light.source)) continue;

		uint32_t res = light.shadow;

		ShadowMap sm;
		sm.resolution = res;
		std::memset(&sm.LIGHT_CLIP_FROM_WORLD, 0, sizeof(sm.LIGHT_CLIP_FROM_WORLD));

		VkExtent2D extent{ .width = res, .height = res };

		sm.depth_image = rtg.helpers.create_image(
			extent,
			depth_format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		{
			VkImageViewCreateInfo view_ci{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = sm.depth_image.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = depth_format,
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};
			VK(vkCreateImageView(rtg.device, &view_ci, nullptr, &sm.depth_view));
		}

		{
			VkFramebufferCreateInfo fb_ci{
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = shadow_render_pass,
				.attachmentCount = 1,
				.pAttachments = &sm.depth_view,
				.width = res,
				.height = res,
				.layers = 1,
			};
			VK(vkCreateFramebuffer(rtg.device, &fb_ci, nullptr, &sm.framebuffer));
		}

		shadow_maps.push_back(std::move(sm));

		std::cout << "[A3-shadow]: Created shadow map for spot light \""
			<< name << "\" (" << res << "x" << res << ")" << std::endl;
	}

	std::cout << "[A3-shadow]: " << shadow_maps.size() << " shadow map(s) created." << std::endl;
}
