#include "../Helpers.hpp"
#include "../RTG.hpp"
#include "../Tutorial.hpp"
#include "../VK.hpp"

#include "../SceneViewer/stb_image.h"

#include <vulkan/utility/vk_format_utils.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <array>
#include <memory>

Helpers::AllocatedImage Helpers::create_cube_image(VkExtent2D const &extent, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, MapFlag map)
{
	AllocatedImage image;
	image.extent = extent;
	image.format = format;
	image.arrayLayers = 6;

	VkImageCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent{
			.width = extent.width,
			.height = extent.height,
			.depth = 1
		},
		.mipLevels = 1,
		.arrayLayers = 6,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VK(vkCreateImage(rtg.device, &create_info, nullptr, &image.handle));

	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(rtg.device, image.handle, &req);

	image.allocation = allocate(req, properties, map);

	VK(vkBindImageMemory(rtg.device, image.handle, image.allocation.handle, image.allocation.offset));

	return image;
}

void Helpers::transfer_to_cube_image(void const *data, size_t size, Helpers::AllocatedImage &target)
{
	assert(target.handle != VK_NULL_HANDLE);
	assert(target.arrayLayers == 6);

	size_t bytes_per_block = vkuFormatTexelBlockSize(target.format);
	size_t texels_per_block = vkuFormatTexelsPerBlock(target.format);
	size_t face_size = target.extent.width * target.extent.height * bytes_per_block / texels_per_block;
	assert(size == face_size * 6);

	AllocatedBuffer transfer_src = create_buffer(
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		Mapped
	);

	std::memcpy(transfer_src.allocation.data(), data, size);

	VK( vkResetCommandBuffer(transfer_command_buffer, 0) );

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	VK( vkBeginCommandBuffer(transfer_command_buffer, &begin_info) );

	VkImageSubresourceRange whole_image{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 6,
	};

	{	// transition all 6 faces to transfer-dst layout
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target.handle,
			.subresourceRange = whole_image,
		};

		vkCmdPipelineBarrier(
			transfer_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}

	{	// copy buffer data to all 6 faces
		// data layout in buffer: [face0][face1][face2][face3][face4][face5]
		std::array<VkBufferImageCopy, 6> regions;
		for (uint32_t face = 0; face < 6; ++face) {
			regions[face] = VkBufferImageCopy{
				.bufferOffset = face * face_size,
				.bufferRowLength = target.extent.width,
				.bufferImageHeight = target.extent.height,
				.imageSubresource{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = face,
					.layerCount = 1,
				},
				.imageOffset{ .x = 0, .y = 0, .z = 0 },
				.imageExtent{
					.width = target.extent.width,
					.height = target.extent.height,
					.depth = 1
				},
			};
		}

		vkCmdCopyBufferToImage(
			transfer_command_buffer,
			transfer_src.handle,
			target.handle,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			uint32_t(regions.size()), regions.data()
		);
	}

	{	// transition all 6 faces to shader-read-only layout
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target.handle,
			.subresourceRange = whole_image,
		};

		vkCmdPipelineBarrier(
			transfer_command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}

	VK( vkEndCommandBuffer(transfer_command_buffer) );

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &transfer_command_buffer
	};

	VK( vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE) );
	VK( vkQueueWaitIdle(rtg.graphics_queue) );

	destroy_buffer(std::move(transfer_src));
}

void Tutorial::load_environment_cubemap()
{
	if (scene_S72.environments.empty()) {
		std::cout << "[Materials.cpp]: No environment found in scene." << std::endl;
		return;
	}

	for (auto &[name, env] : scene_S72.environments)
	{
		if (env.radiance == nullptr) {
			std::cerr << "[Materials.cpp]: Environment '" << name << "' has null radiance texture." << std::endl;
			continue;
		}

		assert(env.radiance->type == S72::Texture::Type::cube);
		assert(env.radiance->format == S72::Texture::Format::rgbe);

		// Load the image (6 faces stacked vertically in a single PNG)
		int img_w = 0, img_h = 0;
		std::unique_ptr<unsigned char, void(*)(void*)> pixels(
			stbi_load(env.radiance->path.c_str(), &img_w, &img_h, nullptr, 4),
			[](void *p) { stbi_image_free(p); }
		);

		if (!pixels) {
			std::cerr << "[Materials.cpp]: Failed to load cubemap '" << env.radiance->path
				<< "': " << stbi_failure_reason() << std::endl;
			continue;
		}

		// Expect 6 square faces stacked vertically: width = face_size, height = 6 * face_size
		if (img_w <= 0 || img_h != img_w * 6) {
			std::cerr << "[Materials.cpp]: Cubemap image dimensions (" << img_w << "x" << img_h
				<< ") don't match expected vertical strip (w x 6w)." << std::endl;
			continue;
		}

		uint32_t face_size = static_cast<uint32_t>(img_w);
		size_t face_pixels = static_cast<size_t>(face_size) * face_size;
		size_t total_pixels = face_pixels * 6;

		// Decode RGBE -> RGBA32F
		// Formula: rgb' = 2^(e-128) * (rgb + 0.5) / 256, with (0,0,0,0) -> (0,0,0)
		std::vector<float> hdr_data(total_pixels * 4);
		unsigned char *src = pixels.get();
		for (size_t i = 0; i < total_pixels; ++i) {
			uint8_t r = src[i * 4 + 0];
			uint8_t g = src[i * 4 + 1];
			uint8_t b = src[i * 4 + 2];
			uint8_t e = src[i * 4 + 3];

			if (r == 0 && g == 0 && b == 0 && e == 0) {
				hdr_data[i * 4 + 0] = 0.0f;
				hdr_data[i * 4 + 1] = 0.0f;
				hdr_data[i * 4 + 2] = 0.0f;
			} else {
				float scale = std::ldexp(1.0f, static_cast<int>(e) - 128) / 256.0f;
				hdr_data[i * 4 + 0] = (r + 0.5f) * scale;
				hdr_data[i * 4 + 1] = (g + 0.5f) * scale;
				hdr_data[i * 4 + 2] = (b + 0.5f) * scale;
			}
			hdr_data[i * 4 + 3] = 1.0f;
		}

		// Create cubemap image on GPU
		environment_cubemap = rtg.helpers.create_cube_image(
			VkExtent2D{ .width = face_size, .height = face_size },
			VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		// Upload all 6 faces
		rtg.helpers.transfer_to_cube_image(
			hdr_data.data(),
			hdr_data.size() * sizeof(float),
			environment_cubemap
		);

		// Create cubemap image view (VK_IMAGE_VIEW_TYPE_CUBE with 6 layers)
		VkImageViewCreateInfo view_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = environment_cubemap.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.components{
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 6,
			},
		};
		VK(vkCreateImageView(rtg.device, &view_info, nullptr, &environment_cubemap_view));

		// Create sampler for cubemap lookup
		VkSamplerCreateInfo sampler_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.minLod = 0.0f,
			.maxLod = 0.0f,
		};
		VK(vkCreateSampler(rtg.device, &sampler_info, nullptr, &environment_cubemap_sampler));

		std::cout << "[Materials.cpp]: Loaded environment cubemap '" << name
			<< "' (" << face_size << "x" << face_size << " per face)" << std::endl;

		// Only one environment per scene (per assumptions)
		break;
	}
}
