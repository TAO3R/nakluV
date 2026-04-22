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
#include <algorithm>

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

static void create_fallback_env_cubemap(Helpers &helpers, VkDevice device,
	Helpers::AllocatedImage &out_image, VkImageView &out_view, VkSampler &out_sampler)
{
	std::vector<float> black(1 * 1 * 6 * 4, 0.0f);
	out_image = helpers.create_cube_image(
		VkExtent2D{1, 1}, VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped);
	helpers.transfer_to_cube_image(black.data(), black.size() * sizeof(float), out_image);
	VkImageViewCreateInfo vci{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = out_image.handle, .viewType = VK_IMAGE_VIEW_TYPE_CUBE, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }};
	VK(vkCreateImageView(device, &vci, nullptr, &out_view));
	VkSamplerCreateInfo sci{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	VK(vkCreateSampler(device, &sci, nullptr, &out_sampler));
}

void Tutorial::load_environment_cubemap()
{
	if (scene_S72.environments.empty()) {
		std::cout << "[Materials.cpp]: No environment found in scene, creating fallback black cubemap." << std::endl;
		create_fallback_env_cubemap(rtg.helpers, rtg.device, environment_cubemap, environment_cubemap_view, environment_cubemap_sampler);
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

void Tutorial::load_lambertian_cubemap()
{
	if (scene_S72.environments.empty()) {
		create_fallback_env_cubemap(rtg.helpers, rtg.device, lambertian_cubemap, lambertian_cubemap_view, lambertian_cubemap_sampler);
		return;
	}

	for (auto &[name, env] : scene_S72.environments)
	{
		if (env.radiance == nullptr) continue;

		// Derive lambertian cubemap path: replace ".png" with ".lambertian.png"
		std::string rad_path = env.radiance->path;
		std::string lamb_path;
		{
			auto dot_pos = rad_path.rfind(".png");
			if (dot_pos != std::string::npos) {
				lamb_path = rad_path.substr(0, dot_pos) + ".lambertian.png";
			} else {
				lamb_path = rad_path + ".lambertian.png";
			}
		}

		int img_w = 0, img_h = 0;
		std::unique_ptr<unsigned char, void(*)(void*)> pixels(
			stbi_load(lamb_path.c_str(), &img_w, &img_h, nullptr, 4),
			[](void *p) { stbi_image_free(p); }
		);

		if (!pixels) {
			std::cerr << "[Materials.cpp]: Lambertian cubemap not found at '"
				<< lamb_path << "' (run cube utility to generate it)." << std::endl;
			return;
		}

		if (img_w <= 0 || img_h != img_w * 6) {
			std::cerr << "[Materials.cpp]: Lambertian cubemap dimensions (" << img_w << "x" << img_h
				<< ") don't match vertical strip (w x 6w)." << std::endl;
			return;
		}

		uint32_t face_size = static_cast<uint32_t>(img_w);
		size_t face_pixels = static_cast<size_t>(face_size) * face_size;
		size_t total_pixels = face_pixels * 6;

		// Decode RGBE -> RGBA32F (same as environment cubemap)
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

		lambertian_cubemap = rtg.helpers.create_cube_image(
			VkExtent2D{ .width = face_size, .height = face_size },
			VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		rtg.helpers.transfer_to_cube_image(
			hdr_data.data(),
			hdr_data.size() * sizeof(float),
			lambertian_cubemap
		);

		VkImageViewCreateInfo view_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = lambertian_cubemap.handle,
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
		VK(vkCreateImageView(rtg.device, &view_info, nullptr, &lambertian_cubemap_view));

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
		VK(vkCreateSampler(rtg.device, &sampler_info, nullptr, &lambertian_cubemap_sampler));

		std::cout << "[Materials.cpp]: Loaded lambertian cubemap '" << lamb_path
			<< "' (" << face_size << "x" << face_size << " per face)" << std::endl;

		break;
	}
}

void Tutorial::build_normal_map_textures()
{
	// Create default 1x1 normal map (flat normal pointing up in tangent space)
	{
		uint32_t default_pixel = 0xFFFF8080; // RGBA = (128, 128, 255, 255) -> tangent-space (0,0,1) after *2-1
		normal_map_textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{ .width = 1, .height = 1 },
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		));
		rtg.helpers.transfer_to_image(&default_pixel, sizeof(default_pixel), normal_map_textures.back());
	}
	const uint32_t default_normal_idx = 0;

	// load per-material normal maps
	mat_to_normal_tex.clear();
	for (auto const &it : scene_S72.materials)
	{
		if (it.second.normal_map != nullptr
			&& it.second.normal_map->type == S72::Texture::Type::flat)
		{
			S72::Texture *nm = it.second.normal_map;
			int w = 0, h = 0;
			std::unique_ptr<unsigned char, void(*)(void*)> pixels(
				stbi_load(nm->path.c_str(), &w, &h, nullptr, 4),
				[](void *p) { stbi_image_free(p); }
			);
			if (!pixels || w <= 0 || h <= 0) {
				std::cerr << "[Materials.cpp]: Failed to load normal map '" << nm->path
					<< "', using default." << std::endl;
				mat_to_normal_tex[&it.second] = default_normal_idx;
				continue;
			}

			size_t byte_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
			normal_map_textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h) },
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			));
			mat_to_normal_tex[&it.second] = uint32_t(normal_map_textures.size() - 1);
			rtg.helpers.transfer_to_image(pixels.get(), byte_size, normal_map_textures.back());

			std::cout << "[Materials.cpp]: Loaded normal map '" << nm->path
				<< "' (" << w << "x" << h << ")" << std::endl;
		}
		else
		{
			mat_to_normal_tex[&it.second] = default_normal_idx;
		}
	}

	std::cout << "[Materials.cpp]: Loaded " << normal_map_textures.size()
		<< " normal map texture(s) (" << (normal_map_textures.size() - 1) << " custom + 1 default)." << std::endl;
}

void Tutorial::build_pbr_textures()
{
	// Create default 1x1 roughness texture (roughness = 1.0 -> 255)
	{
		uint32_t pixel = 0xFF0000FF; // R=255, G=0, B=0, A=255 (only R channel used)
		roughness_textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{ .width = 1, .height = 1 }, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped));
		rtg.helpers.transfer_to_image(&pixel, sizeof(pixel), roughness_textures.back());
	}

	// Create default 1x1 metalness texture (metalness = 0.0 -> 0)
	{
		uint32_t pixel = 0xFF000000; // R=0
		metalness_textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{ .width = 1, .height = 1 }, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped));
		rtg.helpers.transfer_to_image(&pixel, sizeof(pixel), metalness_textures.back());
	}

	mat_to_roughness_tex.clear();
	mat_to_metalness_tex.clear();

	for (auto const &it : scene_S72.materials) {
		if (!std::holds_alternative<S72::Material::PBR>(it.second.brdf)) {
			mat_to_roughness_tex[&it.second] = 0;
			mat_to_metalness_tex[&it.second] = 0;
			continue;
		}
		auto const &pbr = std::get<S72::Material::PBR>(it.second.brdf);

		// Roughness
		if (std::holds_alternative<float>(pbr.roughness)) {
			float val = std::get<float>(pbr.roughness);
			uint32_t pixel = uint32_t(std::clamp(val, 0.0f, 1.0f) * 255.0f + 0.5f);
			pixel = pixel | 0xFF000000u;
			roughness_textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = 1, .height = 1 }, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped));
			rtg.helpers.transfer_to_image(&pixel, sizeof(pixel), roughness_textures.back());
			mat_to_roughness_tex[&it.second] = uint32_t(roughness_textures.size() - 1);
		} else {
			S72::Texture *tex = std::get<S72::Texture *>(pbr.roughness);
			if (!tex || tex->type != S72::Texture::Type::flat) {
				mat_to_roughness_tex[&it.second] = 0;
			} else {
				int w = 0, h = 0;
				std::unique_ptr<unsigned char, void(*)(void*)> pixels(
					stbi_load(tex->path.c_str(), &w, &h, nullptr, 4),
					[](void *p) { stbi_image_free(p); });
				if (!pixels || w <= 0 || h <= 0) {
					mat_to_roughness_tex[&it.second] = 0;
				} else {
					roughness_textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = uint32_t(w), .height = uint32_t(h) }, VK_FORMAT_R8G8B8A8_UNORM,
						VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped));
					mat_to_roughness_tex[&it.second] = uint32_t(roughness_textures.size() - 1);
					rtg.helpers.transfer_to_image(pixels.get(), size_t(w) * h * 4, roughness_textures.back());
				}
			}
		}

		// Metalness
		if (std::holds_alternative<float>(pbr.metalness)) {
			float val = std::get<float>(pbr.metalness);
			uint32_t pixel = uint32_t(std::clamp(val, 0.0f, 1.0f) * 255.0f + 0.5f);
			pixel = pixel | 0xFF000000u;
			metalness_textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = 1, .height = 1 }, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped));
			rtg.helpers.transfer_to_image(&pixel, sizeof(pixel), metalness_textures.back());
			mat_to_metalness_tex[&it.second] = uint32_t(metalness_textures.size() - 1);
		} else {
			S72::Texture *tex = std::get<S72::Texture *>(pbr.metalness);
			if (!tex || tex->type != S72::Texture::Type::flat) {
				mat_to_metalness_tex[&it.second] = 0;
			} else {
				int w = 0, h = 0;
				std::unique_ptr<unsigned char, void(*)(void*)> pixels(
					stbi_load(tex->path.c_str(), &w, &h, nullptr, 4),
					[](void *p) { stbi_image_free(p); });
				if (!pixels || w <= 0 || h <= 0) {
					mat_to_metalness_tex[&it.second] = 0;
				} else {
					metalness_textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = uint32_t(w), .height = uint32_t(h) }, VK_FORMAT_R8G8B8A8_UNORM,
						VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped));
					mat_to_metalness_tex[&it.second] = uint32_t(metalness_textures.size() - 1);
					rtg.helpers.transfer_to_image(pixels.get(), size_t(w) * h * 4, metalness_textures.back());
				}
			}
		}
	}

	std::cout << "[Materials.cpp]: Built " << roughness_textures.size() << " roughness and "
		<< metalness_textures.size() << " metalness textures." << std::endl;
}

static void create_fallback_ggx_cubemap(Helpers &helpers, VkDevice device,
	Helpers::AllocatedImage &out_image, VkImageView &out_view, VkSampler &out_sampler, uint32_t &out_mip_levels)
{
	std::vector<float> black(1 * 1 * 6 * 4, 0.0f);
	out_image = helpers.create_cube_image(
		VkExtent2D{1, 1}, VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped);
	helpers.transfer_to_cube_image(black.data(), black.size() * sizeof(float), out_image);
	VkImageViewCreateInfo vci{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = out_image.handle, .viewType = VK_IMAGE_VIEW_TYPE_CUBE, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }};
	VK(vkCreateImageView(device, &vci, nullptr, &out_view));
	VkSamplerCreateInfo sci{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	VK(vkCreateSampler(device, &sci, nullptr, &out_sampler));
	out_mip_levels = 1;
}

static void create_fallback_brdf_lut(Helpers &helpers, VkDevice device,
	Helpers::AllocatedImage &out_image, VkImageView &out_view, VkSampler &out_sampler)
{
	uint16_t zero[2] = {0, 0};
	out_image = helpers.create_image(
		VkExtent2D{1, 1}, VK_FORMAT_R16G16_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped);
	helpers.transfer_to_image(zero, sizeof(zero), out_image);
	VkImageViewCreateInfo vci{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = out_image.handle, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R16G16_SFLOAT,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }};
	VK(vkCreateImageView(device, &vci, nullptr, &out_view));
	VkSamplerCreateInfo sci{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	VK(vkCreateSampler(device, &sci, nullptr, &out_sampler));
}

void Tutorial::load_ggx_cubemap()
{
	if (scene_S72.environments.empty()) {
		create_fallback_ggx_cubemap(rtg.helpers, rtg.device, ggx_cubemap, ggx_cubemap_view, ggx_cubemap_sampler, ggx_mip_levels);
		return;
	}

	for (auto &[name, env] : scene_S72.environments) {
		if (env.radiance == nullptr) continue;

		std::string rad_path = env.radiance->path;
		std::string base;
		{ auto dot = rad_path.rfind(".png"); base = (dot != std::string::npos) ? rad_path.substr(0, dot) : rad_path; }

		// Count available mip files
		uint32_t num_mips = 0;
		for (int m = 1; ; ++m) {
			std::string path = base + "." + std::to_string(m) + ".png";
			std::ifstream f(path);
			if (!f.good()) break;
			num_mips = uint32_t(m);
		}
		if (num_mips == 0) {
			std::cout << "[Materials.cpp]: No GGX mip files found for '" << name << "', using fallback." << std::endl;
			create_fallback_ggx_cubemap(rtg.helpers, rtg.device, ggx_cubemap, ggx_cubemap_view, ggx_cubemap_sampler, ggx_mip_levels);
			return;
		}
		ggx_mip_levels = num_mips + 1; // base level + mip files

		// Load base level (same data as environment cubemap)
		int base_w = 0, base_h = 0;
		std::unique_ptr<unsigned char, void(*)(void*)> base_pixels(
			stbi_load(rad_path.c_str(), &base_w, &base_h, nullptr, 4),
			[](void *p) { stbi_image_free(p); });
		if (!base_pixels || base_w <= 0 || base_h != base_w * 6) {
			std::cerr << "[Materials.cpp]: Cannot load GGX base cubemap '" << rad_path << "'." << std::endl;
			return;
		}
		uint32_t face_size = uint32_t(base_w);
		size_t face_pixels = size_t(face_size) * face_size;

		// Decode base level RGBE -> RGBA32F
		size_t base_total_pixels = face_pixels * 6;
		std::vector<float> base_hdr(base_total_pixels * 4);
		for (size_t i = 0; i < base_total_pixels; ++i) {
			uint8_t r = base_pixels.get()[i*4+0], g = base_pixels.get()[i*4+1], b = base_pixels.get()[i*4+2], e = base_pixels.get()[i*4+3];
			if (r == 0 && g == 0 && b == 0 && e == 0) {
				base_hdr[i*4+0] = base_hdr[i*4+1] = base_hdr[i*4+2] = 0.0f;
			} else {
				float scale = std::ldexp(1.0f, int(e) - 128) / 256.0f;
				base_hdr[i*4+0] = (r + 0.5f) * scale;
				base_hdr[i*4+1] = (g + 0.5f) * scale;
				base_hdr[i*4+2] = (b + 0.5f) * scale;
			}
			base_hdr[i*4+3] = 1.0f;
		}

		// Create mipmapped cube image manually
		{
			VkImageCreateInfo ci{
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.extent = { face_size, face_size, 1 },
				.mipLevels = ggx_mip_levels,
				.arrayLayers = 6,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			};
			VK(vkCreateImage(rtg.device, &ci, nullptr, &ggx_cubemap.handle));
			ggx_cubemap.extent = { face_size, face_size };
			ggx_cubemap.format = VK_FORMAT_R32G32B32A32_SFLOAT;

			VkMemoryRequirements req;
			vkGetImageMemoryRequirements(rtg.device, ggx_cubemap.handle, &req);
			ggx_cubemap.allocation = rtg.helpers.allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped);
			VK(vkBindImageMemory(rtg.device, ggx_cubemap.handle, ggx_cubemap.allocation.handle, ggx_cubemap.allocation.offset));
		}

		// Upload all mip levels using a single staging buffer + command buffer
		size_t total_staging_size = base_total_pixels * 4 * sizeof(float);
		for (uint32_t m = 1; m <= num_mips; ++m) {
			uint32_t ms = face_size >> m; if (ms == 0) ms = 1;
			total_staging_size += size_t(ms) * ms * 6 * 4 * sizeof(float);
		}

		Helpers::AllocatedBuffer staging = rtg.helpers.create_buffer(
			total_staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Helpers::Mapped);

		uint8_t *staging_ptr = static_cast<uint8_t*>(staging.allocation.data());
		size_t offset = 0;

		// Copy base level
		std::memcpy(staging_ptr + offset, base_hdr.data(), base_hdr.size() * sizeof(float));
		size_t base_offset = offset;
		offset += base_hdr.size() * sizeof(float);

		// Load and copy mip levels
		struct MipInfo { size_t buf_offset; uint32_t size; };
		std::vector<MipInfo> mip_infos(num_mips);
		for (uint32_t m = 1; m <= num_mips; ++m) {
			uint32_t ms = face_size >> m; if (ms == 0) ms = 1;
			std::string path = base + "." + std::to_string(m) + ".png";
			int mw = 0, mh = 0;
			std::unique_ptr<unsigned char, void(*)(void*)> mpx(
				stbi_load(path.c_str(), &mw, &mh, nullptr, 4),
				[](void *p) { stbi_image_free(p); });
			size_t mip_pixels = size_t(ms) * ms * 6;
			std::vector<float> mip_hdr(mip_pixels * 4, 0.0f);
			if (mpx && mw == int(ms) && mh == int(ms) * 6) {
				for (size_t i = 0; i < mip_pixels; ++i) {
					uint8_t r = mpx.get()[i*4+0], g = mpx.get()[i*4+1], b_ = mpx.get()[i*4+2], e = mpx.get()[i*4+3];
					if (r == 0 && g == 0 && b_ == 0 && e == 0) {
						mip_hdr[i*4+0] = mip_hdr[i*4+1] = mip_hdr[i*4+2] = 0.0f;
					} else {
						float s = std::ldexp(1.0f, int(e) - 128) / 256.0f;
						mip_hdr[i*4+0] = (r + 0.5f) * s;
						mip_hdr[i*4+1] = (g + 0.5f) * s;
						mip_hdr[i*4+2] = (b_ + 0.5f) * s;
					}
					mip_hdr[i*4+3] = 1.0f;
				}
			}
			mip_infos[m-1] = { offset, ms };
			std::memcpy(staging_ptr + offset, mip_hdr.data(), mip_hdr.size() * sizeof(float));
			offset += mip_hdr.size() * sizeof(float);
		}

		// Record copy commands
		VK(vkResetCommandBuffer(rtg.helpers.transfer_command_buffer, 0));
		VkCommandBufferBeginInfo bi{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
		VK(vkBeginCommandBuffer(rtg.helpers.transfer_command_buffer, &bi));

		{ // transition all mip levels to TRANSFER_DST
			VkImageMemoryBarrier b{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = ggx_cubemap.handle,
				.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, ggx_mip_levels, 0, 6 }};
			vkCmdPipelineBarrier(rtg.helpers.transfer_command_buffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,0, 0,0, 1, &b);
		}

		// Base level (mip 0)
		{
			std::array<VkBufferImageCopy, 6> regions;
			size_t face_bytes = face_pixels * 4 * sizeof(float);
			for (uint32_t f = 0; f < 6; ++f) {
				regions[f] = VkBufferImageCopy{
					.bufferOffset = base_offset + f * face_bytes,
					.bufferRowLength = face_size, .bufferImageHeight = face_size,
					.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, f, 1 },
					.imageExtent = { face_size, face_size, 1 }};
			}
			vkCmdCopyBufferToImage(rtg.helpers.transfer_command_buffer, staging.handle, ggx_cubemap.handle,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions.data());
		}

		// Mip levels 1..N
		for (uint32_t m = 1; m <= num_mips; ++m) {
			auto &mi = mip_infos[m-1];
			size_t mip_face_bytes = size_t(mi.size) * mi.size * 4 * sizeof(float);
			std::array<VkBufferImageCopy, 6> regions;
			for (uint32_t f = 0; f < 6; ++f) {
				regions[f] = VkBufferImageCopy{
					.bufferOffset = mi.buf_offset + f * mip_face_bytes,
					.bufferRowLength = mi.size, .bufferImageHeight = mi.size,
					.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, f, 1 },
					.imageExtent = { mi.size, mi.size, 1 }};
			}
			vkCmdCopyBufferToImage(rtg.helpers.transfer_command_buffer, staging.handle, ggx_cubemap.handle,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions.data());
		}

		{ // transition to SHADER_READ_ONLY
			VkImageMemoryBarrier b{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = ggx_cubemap.handle,
				.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, ggx_mip_levels, 0, 6 }};
			vkCmdPipelineBarrier(rtg.helpers.transfer_command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,0, 0,0, 1, &b);
		}

		VK(vkEndCommandBuffer(rtg.helpers.transfer_command_buffer));
		VkSubmitInfo si{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &rtg.helpers.transfer_command_buffer };
		VK(vkQueueSubmit(rtg.graphics_queue, 1, &si, VK_NULL_HANDLE));
		VK(vkQueueWaitIdle(rtg.graphics_queue));
		rtg.helpers.destroy_buffer(std::move(staging));

		// Create view spanning all mip levels
		{
			VkImageViewCreateInfo ci{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = ggx_cubemap.handle, .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, ggx_mip_levels, 0, 6 }};
			VK(vkCreateImageView(rtg.device, &ci, nullptr, &ggx_cubemap_view));
		}

		// Create sampler with trilinear filtering across mip levels
		{
			VkSamplerCreateInfo ci{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
				.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
				.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.minLod = 0.0f, .maxLod = float(ggx_mip_levels - 1)};
			VK(vkCreateSampler(rtg.device, &ci, nullptr, &ggx_cubemap_sampler));
		}

		std::cout << "[Materials.cpp]: Loaded GGX cubemap '" << name << "' ("
			<< face_size << "x" << face_size << ", " << ggx_mip_levels << " mip levels)" << std::endl;
		break;
	}
}

void Tutorial::load_brdf_lut()
{
	std::string path = "brdf_lut.bin";
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		std::cerr << "[Materials.cpp]: BRDF LUT not found at '" << path << "', using fallback." << std::endl;
		create_fallback_brdf_lut(rtg.helpers, rtg.device, brdf_lut, brdf_lut_view, brdf_lut_sampler);
		return;
	}
	uint32_t header[2];
	f.read(reinterpret_cast<char*>(header), sizeof(header));
	uint32_t w = header[0], h = header[1];
	if (w == 0 || h == 0 || w > 4096 || h > 4096) {
		std::cerr << "[Materials.cpp]: Invalid BRDF LUT dimensions " << w << "x" << h << std::endl;
		return;
	}
	size_t data_bytes = size_t(w) * h * 4; // R16G16 = 4 bytes per pixel
	std::vector<char> data(data_bytes);
	f.read(data.data(), data_bytes);
	if (!f) {
		std::cerr << "[Materials.cpp]: BRDF LUT read failed." << std::endl;
		return;
	}

	brdf_lut = rtg.helpers.create_image(
		VkExtent2D{ .width = w, .height = h }, VK_FORMAT_R16G16_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Helpers::Unmapped);
	rtg.helpers.transfer_to_image(data.data(), data_bytes, brdf_lut);

	{
		VkImageViewCreateInfo ci{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = brdf_lut.handle, .viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R16G16_SFLOAT,
			.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }};
		VK(vkCreateImageView(rtg.device, &ci, nullptr, &brdf_lut_view));
	}
	{
		VkSamplerCreateInfo ci{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
		VK(vkCreateSampler(rtg.device, &ci, nullptr, &brdf_lut_sampler));
	}

	std::cout << "[Materials.cpp]: Loaded BRDF LUT (" << w << "x" << h << ")" << std::endl;
}
