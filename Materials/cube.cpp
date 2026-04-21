#define _CRT_SECURE_NO_WARNINGS

#define STB_IMAGE_IMPLEMENTATION
#include "../SceneViewer/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <vulkan/vulkan.h>
#include "VK.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---- SPIR-V code for compute shaders ----

static uint32_t ggx_convolve_code[] =
#include "spv/Materials/ggx_convolve.comp.inl"
;

static uint32_t brdf_lut_code[] =
#include "spv/Materials/brdf_lut.comp.inl"
;

// ---- Vec3 math ----

struct Vec3 {
	float x, y, z;
	Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
	Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
	Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float length(Vec3 v) { return std::sqrt(dot(v, v)); }

static Vec3 normalize(Vec3 v) {
	float l = length(v);
	return (l > 1e-12f) ? v * (1.0f / l) : Vec3{0, 0, 0};
}

// ---- Cubemap face conventions (s72 / OpenGL standard) ----

static Vec3 direction_from_face_uv(int face, float u, float v) {
	float s = 2.0f * u - 1.0f;
	float t = 2.0f * v - 1.0f;
	switch (face) {
		case 0: return normalize({ 1.0f,    t,   -s}); // +X
		case 1: return normalize({-1.0f,    t,    s}); // -X
		case 2: return normalize({    s, 1.0f,   -t}); // +Y
		case 3: return normalize({    s,-1.0f,    t}); // -Y
		case 4: return normalize({    s,    t, 1.0f}); // +Z
		case 5: return normalize({   -s,    t,-1.0f}); // -Z
		default: return {0, 0, 0};
	}
}

// ---- RGBE decode / encode ----

static void decode_rgbe(const uint8_t *src, float *dst, size_t pixel_count) {
	for (size_t i = 0; i < pixel_count; ++i) {
		uint8_t r = src[i * 4 + 0];
		uint8_t g = src[i * 4 + 1];
		uint8_t b = src[i * 4 + 2];
		uint8_t e = src[i * 4 + 3];
		if (r == 0 && g == 0 && b == 0 && e == 0) {
			dst[i * 3 + 0] = 0.0f;
			dst[i * 3 + 1] = 0.0f;
			dst[i * 3 + 2] = 0.0f;
		} else {
			float scale = std::ldexp(1.0f, static_cast<int>(e) - 128) / 256.0f;
			dst[i * 3 + 0] = (r + 0.5f) * scale;
			dst[i * 3 + 1] = (g + 0.5f) * scale;
			dst[i * 3 + 2] = (b + 0.5f) * scale;
		}
	}
}

static void encode_rgbe(const float *src, uint8_t *dst, size_t pixel_count) {
	for (size_t i = 0; i < pixel_count; ++i) {
		float r = src[i * 3 + 0];
		float g = src[i * 3 + 1];
		float b = src[i * 3 + 2];

		float max_val = std::max(r, std::max(g, b));
		if (max_val < 1e-32f) {
			dst[i * 4 + 0] = 0;
			dst[i * 4 + 1] = 0;
			dst[i * 4 + 2] = 0;
			dst[i * 4 + 3] = 0;
		} else {
			int exponent;
			float mantissa = std::frexp(max_val, &exponent);
			float scale = mantissa * 256.0f / max_val;
			dst[i * 4 + 0] = static_cast<uint8_t>(std::max(0.0f, r * scale - 0.5f));
			dst[i * 4 + 1] = static_cast<uint8_t>(std::max(0.0f, g * scale - 0.5f));
			dst[i * 4 + 2] = static_cast<uint8_t>(std::max(0.0f, b * scale - 0.5f));
			dst[i * 4 + 3] = static_cast<uint8_t>(exponent + 128);
		}
	}
}

// ---- Lambertian convolution (CPU) ----

static void convolve_lambertian(
	const float *input_hdr, int input_face_size,
	float *output_hdr, int output_face_size)
{
	size_t out_face_pixels = static_cast<size_t>(output_face_size) * output_face_size;
	size_t in_face_pixels = static_cast<size_t>(input_face_size) * input_face_size;

	for (int out_face = 0; out_face < 6; ++out_face) {
		for (int out_y = 0; out_y < output_face_size; ++out_y) {
			for (int out_x = 0; out_x < output_face_size; ++out_x) {
				float u = (out_x + 0.5f) / output_face_size;
				float v = (out_y + 0.5f) / output_face_size;
				Vec3 n = direction_from_face_uv(out_face, u, v);

				float accum_r = 0, accum_g = 0, accum_b = 0;

				for (int in_face = 0; in_face < 6; ++in_face) {
					for (int in_y = 0; in_y < input_face_size; ++in_y) {
						for (int in_x = 0; in_x < input_face_size; ++in_x) {
							float iu = (in_x + 0.5f) / input_face_size;
							float iv = (in_y + 0.5f) / input_face_size;
							Vec3 w = direction_from_face_uv(in_face, iu, iv);

							float cos_theta = dot(n, w);
							if (cos_theta <= 0.0f) continue;

							float s = 2.0f * iu - 1.0f;
							float t = 2.0f * iv - 1.0f;
							float d2 = s * s + t * t + 1.0f;
							float texel_solid_angle = 4.0f / (in_face_pixels * d2 * std::sqrt(d2));

							size_t idx = in_face * in_face_pixels + static_cast<size_t>(in_y) * input_face_size + in_x;

							float w_sample = cos_theta * texel_solid_angle;
							accum_r += input_hdr[idx * 3 + 0] * w_sample;
							accum_g += input_hdr[idx * 3 + 1] * w_sample;
							accum_b += input_hdr[idx * 3 + 2] * w_sample;
						}
					}
				}

				size_t out_idx = out_face * out_face_pixels
					+ static_cast<size_t>(out_y) * output_face_size + out_x;
				float inv_pi = 1.0f / static_cast<float>(M_PI);
				output_hdr[out_idx * 3 + 0] = accum_r * inv_pi;
				output_hdr[out_idx * 3 + 1] = accum_g * inv_pi;
				output_hdr[out_idx * 3 + 2] = accum_b * inv_pi;
			}
		}
		std::cout << "  face " << out_face << "/5 done" << std::endl;
	}
}

// ===========================================================================
// Minimal headless Vulkan compute context
// ===========================================================================

struct VulkanCompute {
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	VkCommandPool cmd_pool = VK_NULL_HANDLE;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties mem_props{};

	void init() {
		VkApplicationInfo app_info{
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "cube",
			.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
			.pEngineName = "none",
			.engineVersion = VK_MAKE_VERSION(1, 0, 0),
			.apiVersion = VK_API_VERSION_1_2,
		};
		VkInstanceCreateInfo inst_ci{
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &app_info,
		};
		VK(vkCreateInstance(&inst_ci, nullptr, &instance));

		uint32_t gpu_count = 0;
		VK(vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr));
		if (gpu_count == 0) throw std::runtime_error("No Vulkan-capable GPU found.");
		std::vector<VkPhysicalDevice> gpus(gpu_count);
		VK(vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data()));
		physical_device = gpus[0];

		vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

		uint32_t qf_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count, nullptr);
		std::vector<VkQueueFamilyProperties> qf_props(qf_count);
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count, qf_props.data());

		bool found = false;
		for (uint32_t i = 0; i < qf_count; ++i) {
			if (qf_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
				queue_family = i;
				found = true;
				break;
			}
		}
		if (!found) throw std::runtime_error("No compute queue family found.");

		float priority = 1.0f;
		VkDeviceQueueCreateInfo queue_ci{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queue_family,
			.queueCount = 1,
			.pQueuePriorities = &priority,
		};
		VkDeviceCreateInfo dev_ci{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queue_ci,
		};
		VK(vkCreateDevice(physical_device, &dev_ci, nullptr, &device));
		vkGetDeviceQueue(device, queue_family, 0, &queue);

		VkCommandPoolCreateInfo pool_ci{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = queue_family,
		};
		VK(vkCreateCommandPool(device, &pool_ci, nullptr, &cmd_pool));

		VkCommandBufferAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = cmd_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		VK(vkAllocateCommandBuffers(device, &alloc_info, &cmd));
	}

	void destroy() {
		if (cmd_pool) vkDestroyCommandPool(device, cmd_pool, nullptr);
		if (device) vkDestroyDevice(device, nullptr);
		if (instance) vkDestroyInstance(instance, nullptr);
	}

	uint32_t find_memory(uint32_t type_bits, VkMemoryPropertyFlags flags) {
		for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
			if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & flags) == flags)
				return i;
		}
		throw std::runtime_error("Failed to find suitable memory type.");
	}

	struct Buffer {
		VkBuffer handle = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDeviceSize size = 0;
	};

	Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
		Buffer b;
		b.size = size;
		VkBufferCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		VK(vkCreateBuffer(device, &ci, nullptr, &b.handle));
		VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(device, b.handle, &req);
		VkMemoryAllocateInfo ai{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = req.size,
			.memoryTypeIndex = find_memory(req.memoryTypeBits, props),
		};
		VK(vkAllocateMemory(device, &ai, nullptr, &b.memory));
		VK(vkBindBufferMemory(device, b.handle, b.memory, 0));
		return b;
	}

	void destroy_buffer(Buffer &b) {
		if (b.handle) vkDestroyBuffer(device, b.handle, nullptr);
		if (b.memory) vkFreeMemory(device, b.memory, nullptr);
		b = {};
	}

	struct Image {
		VkImage handle = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
	};

	Image create_image(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
	                   uint32_t layers = 1, VkImageCreateFlags flags = 0) {
		Image img;
		VkImageCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.flags = flags,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = format,
			.extent = {w, h, 1},
			.mipLevels = 1,
			.arrayLayers = layers,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		VK(vkCreateImage(device, &ci, nullptr, &img.handle));
		VkMemoryRequirements req;
		vkGetImageMemoryRequirements(device, img.handle, &req);
		VkMemoryAllocateInfo ai{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = req.size,
			.memoryTypeIndex = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
		};
		VK(vkAllocateMemory(device, &ai, nullptr, &img.memory));
		VK(vkBindImageMemory(device, img.handle, img.memory, 0));
		return img;
	}

	void destroy_image(Image &img) {
		if (img.handle) vkDestroyImage(device, img.handle, nullptr);
		if (img.memory) vkFreeMemory(device, img.memory, nullptr);
		img = {};
	}

	VkShaderModule create_shader_module(const uint32_t *code, size_t byte_size) {
		VkShaderModuleCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = byte_size,
			.pCode = code,
		};
		VkShaderModule m = VK_NULL_HANDLE;
		VK(vkCreateShaderModule(device, &ci, nullptr, &m));
		return m;
	}

	void begin() {
		VK(vkResetCommandBuffer(cmd, 0));
		VkCommandBufferBeginInfo bi{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		VK(vkBeginCommandBuffer(cmd, &bi));
	}

	void submit_and_wait() {
		VK(vkEndCommandBuffer(cmd));
		VkSubmitInfo si{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmd,
		};
		VK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
		VK(vkQueueWaitIdle(queue));
	}

	void image_barrier(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
	                   VkAccessFlags src_access, VkAccessFlags dst_access,
	                   VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
	                   uint32_t layers = 1) {
		VkImageMemoryBarrier b{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = src_access,
			.dstAccessMask = dst_access,
			.oldLayout = old_layout,
			.newLayout = new_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers},
		};
		vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
	}
};

// ===========================================================================
// GGX prefiltered cubemap computation (Vulkan compute)
// ===========================================================================

static void compute_ggx(VulkanCompute &vk,
                        const float *input_rgba, uint32_t face_size,
                        const std::string &output_base, int num_samples)
{
	constexpr int NUM_MIPS = 5;
	constexpr float MIP_ROUGHNESS[NUM_MIPS] = { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };

	if (face_size < (1u << NUM_MIPS)) {
		std::cerr << "Input cubemap too small for " << NUM_MIPS << " GGX mip levels." << std::endl;
		return;
	}
	int num_mips = NUM_MIPS;

	// Upload input cubemap as VkImage (RGBA32F, 6 layers)
	size_t face_bytes = size_t(face_size) * face_size * 4 * sizeof(float);
	size_t total_bytes = face_bytes * 6;

	auto staging_in = vk.create_buffer(total_bytes,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	{
		void *mapped = nullptr;
		VK(vkMapMemory(vk.device, staging_in.memory, 0, total_bytes, 0, &mapped));
		std::memcpy(mapped, input_rgba, total_bytes);
		vkUnmapMemory(vk.device, staging_in.memory);
	}

	auto cube_img = vk.create_image(face_size, face_size, VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 6,
		VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

	// Copy staging buffer -> cube image
	vk.begin();
	vk.image_barrier(cube_img.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 6);
	std::vector<VkBufferImageCopy> regions(6);
	for (uint32_t f = 0; f < 6; ++f) {
		regions[f] = VkBufferImageCopy{
			.bufferOffset = f * face_bytes,
			.bufferRowLength = face_size,
			.bufferImageHeight = face_size,
			.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, f, 1},
			.imageOffset = {0, 0, 0},
			.imageExtent = {face_size, face_size, 1},
		};
	}
	vkCmdCopyBufferToImage(vk.cmd, staging_in.handle, cube_img.handle,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions.data());
	vk.image_barrier(cube_img.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 6);
	vk.submit_and_wait();

	// Create cube image view + sampler
	VkImageView cube_view = VK_NULL_HANDLE;
	{
		VkImageViewCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = cube_img.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = VK_FORMAT_R32G32B32A32_SFLOAT,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6},
		};
		VK(vkCreateImageView(vk.device, &ci, nullptr, &cube_view));
	}
	VkSampler cube_sampler = VK_NULL_HANDLE;
	{
		VkSamplerCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		};
		VK(vkCreateSampler(vk.device, &ci, nullptr, &cube_sampler));
	}

	// Create descriptor set layout, pool, set
	VkDescriptorSetLayout ds_layout = VK_NULL_HANDLE;
	{
		VkDescriptorSetLayoutBinding bindings[2] = {
			{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
			{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
		};
		VkDescriptorSetLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 2,
			.pBindings = bindings,
		};
		VK(vkCreateDescriptorSetLayout(vk.device, &ci, nullptr, &ds_layout));
	}
	VkDescriptorPool ds_pool = VK_NULL_HANDLE;
	{
		VkDescriptorPoolSize sizes[2] = {
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
			{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
		};
		VkDescriptorPoolCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = 1,
			.poolSizeCount = 2,
			.pPoolSizes = sizes,
		};
		VK(vkCreateDescriptorPool(vk.device, &ci, nullptr, &ds_pool));
	}
	VkDescriptorSet ds = VK_NULL_HANDLE;
	{
		VkDescriptorSetAllocateInfo ai{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = ds_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &ds_layout,
		};
		VK(vkAllocateDescriptorSets(vk.device, &ai, &ds));
	}

	// Write the input cubemap descriptor (binding 0) -- stays fixed
	{
		VkDescriptorImageInfo img_info{
			.sampler = cube_sampler,
			.imageView = cube_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		VkWriteDescriptorSet w{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ds,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &img_info,
		};
		vkUpdateDescriptorSets(vk.device, 1, &w, 0, nullptr);
	}

	// Create pipeline layout + compute pipeline
	VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
	{
		VkPushConstantRange pcr{
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.offset = 0,
			.size = 16, // float roughness, int face, int output_size, int num_samples
		};
		VkPipelineLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &ds_layout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &pcr,
		};
		VK(vkCreatePipelineLayout(vk.device, &ci, nullptr, &pipe_layout));
	}
	VkPipeline pipeline = VK_NULL_HANDLE;
	{
		VkShaderModule m = vk.create_shader_module(ggx_convolve_code, sizeof(ggx_convolve_code));
		VkComputePipelineCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = m,
				.pName = "main",
			},
			.layout = pipe_layout,
		};
		VK(vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline));
		vkDestroyShaderModule(vk.device, m, nullptr);
	}

	// Per mip-level: create output image, dispatch 6 faces, read back, write PNG
	for (int mip = 1; mip <= num_mips; ++mip) {
		uint32_t mip_size = face_size >> mip;
		if (mip_size == 0) mip_size = 1;
		float roughness = MIP_ROUGHNESS[mip - 1];

		size_t mip_face_bytes = size_t(mip_size) * mip_size * 4 * sizeof(float);
		size_t mip_total_bytes = mip_face_bytes * 6;

		auto out_img = vk.create_image(mip_size, mip_size, VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		VkImageView out_view = VK_NULL_HANDLE;
		{
			VkImageViewCreateInfo ci{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = out_img.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
			};
			VK(vkCreateImageView(vk.device, &ci, nullptr, &out_view));
		}

		// Update output image descriptor (binding 1)
		{
			VkDescriptorImageInfo img_info{
				.imageView = out_view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			};
			VkWriteDescriptorSet w{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = ds,
				.dstBinding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = &img_info,
			};
			vkUpdateDescriptorSets(vk.device, 1, &w, 0, nullptr);
		}

		auto staging_out = vk.create_buffer(mip_total_bytes,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		// Record command buffer: for each face, dispatch + copy
		vk.begin();
		vk.image_barrier(out_img.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		vkCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_layout, 0, 1, &ds, 0, nullptr);

		uint32_t groups = (mip_size + 7) / 8;

		for (int f = 0; f < 6; ++f) {
			struct { float roughness; int32_t face; int32_t output_size; int32_t num_samples; } push{
				roughness, f, (int32_t)mip_size, num_samples
			};
			vkCmdPushConstants(vk.cmd, pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &push);
			vkCmdDispatch(vk.cmd, groups, groups, 1);

			// barrier: shader write -> transfer read
			vk.image_barrier(out_img.handle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

			VkBufferImageCopy region{
				.bufferOffset = VkDeviceSize(f) * mip_face_bytes,
				.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
				.imageExtent = {mip_size, mip_size, 1},
			};
			vkCmdCopyImageToBuffer(vk.cmd, out_img.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				staging_out.handle, 1, &region);

			if (f < 5) {
				// barrier: transfer -> compute, back to GENERAL for next face
				vk.image_barrier(out_img.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
					VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
			}
		}
		vk.submit_and_wait();

		// Read back and encode RGBE
		void *mapped = nullptr;
		VK(vkMapMemory(vk.device, staging_out.memory, 0, mip_total_bytes, 0, &mapped));

		size_t mip_total_pixels = size_t(mip_size) * mip_size * 6;
		std::vector<float> hdr_rgb(mip_total_pixels * 3);
		const float *src = static_cast<const float*>(mapped);
		for (size_t i = 0; i < mip_total_pixels; ++i) {
			hdr_rgb[i * 3 + 0] = src[i * 4 + 0];
			hdr_rgb[i * 3 + 1] = src[i * 4 + 1];
			hdr_rgb[i * 3 + 2] = src[i * 4 + 2];
		}
		vkUnmapMemory(vk.device, staging_out.memory);

		std::vector<uint8_t> rgbe(mip_total_pixels * 4);
		encode_rgbe(hdr_rgb.data(), rgbe.data(), mip_total_pixels);

		std::string out_path = output_base + "." + std::to_string(mip) + ".png";
		int out_w = int(mip_size);
		int out_h = int(mip_size) * 6;
		if (!stbi_write_png(out_path.c_str(), out_w, out_h, 4, rgbe.data(), out_w * 4)) {
			std::cerr << "Error: failed to write '" << out_path << "'." << std::endl;
		} else {
			std::cout << "  Wrote mip " << mip << "/" << num_mips
				<< " (roughness=" << roughness << ", " << mip_size << "x" << mip_size
				<< " per face): " << out_path << std::endl;
		}

		vkDestroyImageView(vk.device, out_view, nullptr);
		vk.destroy_image(out_img);
		vk.destroy_buffer(staging_out);
	}

	// Cleanup
	vkDestroyPipeline(vk.device, pipeline, nullptr);
	vkDestroyPipelineLayout(vk.device, pipe_layout, nullptr);
	vkDestroyDescriptorPool(vk.device, ds_pool, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, ds_layout, nullptr);
	vkDestroySampler(vk.device, cube_sampler, nullptr);
	vkDestroyImageView(vk.device, cube_view, nullptr);
	vk.destroy_image(cube_img);
	vk.destroy_buffer(staging_in);
}

// ===========================================================================
// BRDF LUT computation (Vulkan compute)
// ===========================================================================

static void compute_brdf_lut(VulkanCompute &vk, const std::string &output_path,
                             int lut_size, int num_samples)
{
	auto out_img = vk.create_image(uint32_t(lut_size), uint32_t(lut_size),
		VK_FORMAT_R16G16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	VkImageView out_view = VK_NULL_HANDLE;
	{
		VkImageViewCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = out_img.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R16G16_SFLOAT,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		VK(vkCreateImageView(vk.device, &ci, nullptr, &out_view));
	}

	VkDescriptorSetLayout ds_layout = VK_NULL_HANDLE;
	{
		VkDescriptorSetLayoutBinding binding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		};
		VkDescriptorSetLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = 1,
			.pBindings = &binding,
		};
		VK(vkCreateDescriptorSetLayout(vk.device, &ci, nullptr, &ds_layout));
	}
	VkDescriptorPool ds_pool = VK_NULL_HANDLE;
	{
		VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
		VkDescriptorPoolCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = 1,
			.poolSizeCount = 1,
			.pPoolSizes = &ps,
		};
		VK(vkCreateDescriptorPool(vk.device, &ci, nullptr, &ds_pool));
	}
	VkDescriptorSet ds = VK_NULL_HANDLE;
	{
		VkDescriptorSetAllocateInfo ai{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = ds_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &ds_layout,
		};
		VK(vkAllocateDescriptorSets(vk.device, &ai, &ds));
	}
	{
		VkDescriptorImageInfo img_info{
			.imageView = out_view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};
		VkWriteDescriptorSet w{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ds,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &img_info,
		};
		vkUpdateDescriptorSets(vk.device, 1, &w, 0, nullptr);
	}

	VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
	{
		VkPushConstantRange pcr{
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			.offset = 0,
			.size = 12, // int width, int height, int num_samples
		};
		VkPipelineLayoutCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &ds_layout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &pcr,
		};
		VK(vkCreatePipelineLayout(vk.device, &ci, nullptr, &pipe_layout));
	}
	VkPipeline pipeline = VK_NULL_HANDLE;
	{
		VkShaderModule m = vk.create_shader_module(brdf_lut_code, sizeof(brdf_lut_code));
		VkComputePipelineCreateInfo ci{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = m,
				.pName = "main",
			},
			.layout = pipe_layout,
		};
		VK(vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline));
		vkDestroyShaderModule(vk.device, m, nullptr);
	}

	size_t pixel_bytes = 4; // R16G16 = 2+2 bytes
	size_t total_bytes = size_t(lut_size) * lut_size * pixel_bytes;
	auto staging = vk.create_buffer(total_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	vk.begin();
	vk.image_barrier(out_img.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
		0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	vkCmdBindPipeline(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_layout, 0, 1, &ds, 0, nullptr);

	struct { int32_t w, h, samples; } push{lut_size, lut_size, num_samples};
	vkCmdPushConstants(vk.cmd, pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, &push);

	uint32_t groups = (uint32_t(lut_size) + 7) / 8;
	vkCmdDispatch(vk.cmd, groups, groups, 1);

	vk.image_barrier(out_img.handle, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferImageCopy region{
		.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.imageExtent = {uint32_t(lut_size), uint32_t(lut_size), 1},
	};
	vkCmdCopyImageToBuffer(vk.cmd, out_img.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		staging.handle, 1, &region);
	vk.submit_and_wait();

	// Read back and write binary file
	void *mapped = nullptr;
	VK(vkMapMemory(vk.device, staging.memory, 0, total_bytes, 0, &mapped));

	std::ofstream ofs(output_path, std::ios::binary);
	if (!ofs) {
		std::cerr << "Error: cannot open '" << output_path << "' for writing." << std::endl;
	} else {
		uint32_t header[2] = { uint32_t(lut_size), uint32_t(lut_size) };
		ofs.write(reinterpret_cast<const char*>(header), sizeof(header));
		ofs.write(static_cast<const char*>(mapped), total_bytes);
		std::cout << "Wrote BRDF LUT: " << output_path << " (" << lut_size << "x" << lut_size
			<< ", " << total_bytes << " bytes data)" << std::endl;
	}
	vkUnmapMemory(vk.device, staging.memory);

	vk.destroy_buffer(staging);
	vkDestroyPipeline(vk.device, pipeline, nullptr);
	vkDestroyPipelineLayout(vk.device, pipe_layout, nullptr);
	vkDestroyDescriptorPool(vk.device, ds_pool, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, ds_layout, nullptr);
	vkDestroyImageView(vk.device, out_view, nullptr);
	vk.destroy_image(out_img);
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char **argv) {
	std::string input_path;

	bool do_lambertian = false;
	std::string lambertian_output;
	int lambertian_size = 16;

	bool do_ggx = false;
	std::string ggx_output;
	int ggx_samples = 1024;

	bool do_brdf_lut = false;
	std::string brdf_lut_output;
	int brdf_lut_size = 512;
	int brdf_lut_samples = 1024;

	bool do_merge = false;
	std::string merge_output;
	std::vector<std::string> merge_faces;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--lambertian") {
			do_lambertian = true;
			if (i + 1 < argc) { lambertian_output = argv[++i]; }
			else { std::cerr << "Error: --lambertian requires an output path." << std::endl; return 1; }
		} else if (arg == "--ggx") {
			do_ggx = true;
			if (i + 1 < argc) { ggx_output = argv[++i]; }
			else { std::cerr << "Error: --ggx requires an output path." << std::endl; return 1; }
		} else if (arg == "--brdf-lut") {
			do_brdf_lut = true;
			if (i + 1 < argc) { brdf_lut_output = argv[++i]; }
			else { std::cerr << "Error: --brdf-lut requires an output path." << std::endl; return 1; }
		} else if (arg == "--size") {
			if (i + 1 < argc) { lambertian_size = std::stoi(argv[++i]); }
			else { std::cerr << "Error: --size requires a number." << std::endl; return 1; }
		} else if (arg == "--ggx-samples") {
			if (i + 1 < argc) { ggx_samples = std::stoi(argv[++i]); }
			else { std::cerr << "Error: --ggx-samples requires a number." << std::endl; return 1; }
		} else if (arg == "--brdf-lut-size") {
			if (i + 1 < argc) { brdf_lut_size = std::stoi(argv[++i]); }
			else { std::cerr << "Error: --brdf-lut-size requires a number." << std::endl; return 1; }
		} else if (arg == "--brdf-lut-samples") {
			if (i + 1 < argc) { brdf_lut_samples = std::stoi(argv[++i]); }
			else { std::cerr << "Error: --brdf-lut-samples requires a number." << std::endl; return 1; }
		} else if (arg == "--merge-faces") {
			do_merge = true;
			if (i + 6 >= argc) {
				std::cerr << "Error: --merge-faces requires 6 face image paths." << std::endl;
				return 1;
			}
			for (int f = 0; f < 6; ++f) merge_faces.push_back(argv[++i]);
		} else if (arg == "-o" && do_merge) {
			if (i + 1 >= argc) { std::cerr << "Error: -o requires an output path." << std::endl; return 1; }
			merge_output = argv[++i];
		} else if (input_path.empty()) {
			input_path = arg;
		} else {
			std::cerr << "Error: unexpected argument '" << arg << "'." << std::endl;
			return 1;
		}
	}

	if (do_merge) {
		if (merge_output.empty()) {
			std::cerr << "Error: --merge-faces requires -o output.png" << std::endl;
			return 1;
		}

		// Load all 6 faces and verify they're the same square size
		int face_size = 0;
		std::vector<std::vector<uint8_t>> face_data(6);

		for (int f = 0; f < 6; ++f) {
			int w = 0, h = 0;
			std::unique_ptr<unsigned char, void(*)(void*)> pixels(
				stbi_load(merge_faces[f].c_str(), &w, &h, nullptr, 4),
				[](void *p) { stbi_image_free(p); }
			);
			if (!pixels) {
				std::cerr << "Error: failed to load face " << f << " '" << merge_faces[f]
					<< "': " << stbi_failure_reason() << std::endl;
				return 1;
			}
			if (w != h) {
				std::cerr << "Error: face '" << merge_faces[f] << "' is not square (" << w << "x" << h << ")." << std::endl;
				return 1;
			}
			if (face_size == 0) face_size = w;
			else if (w != face_size) {
				std::cerr << "Error: face '" << merge_faces[f] << "' size " << w
					<< " doesn't match first face size " << face_size << "." << std::endl;
				return 1;
			}

			size_t bytes = size_t(w) * h * 4;
			face_data[f].assign(pixels.get(), pixels.get() + bytes);
		}

		// Stack vertically: output is face_size x (face_size * 6)
		size_t row_bytes = size_t(face_size) * 4;
		std::vector<uint8_t> output(size_t(face_size) * face_size * 6 * 4);
		for (int f = 0; f < 6; ++f) {
			for (int y = 0; y < face_size; ++y) {
				memcpy(
					&output[(size_t(f) * face_size + y) * row_bytes],
					&face_data[f][y * row_bytes],
					row_bytes
				);
			}
		}

		if (!stbi_write_png(merge_output.c_str(), face_size, face_size * 6, 4, output.data(), int(row_bytes))) {
			std::cerr << "Error: failed to write '" << merge_output << "'." << std::endl;
			return 1;
		}
		std::cout << "Merged 6 faces (" << face_size << "x" << face_size << ") -> "
			<< merge_output << " (" << face_size << "x" << face_size * 6 << ")" << std::endl;
		return 0;
	}

	if (!do_lambertian && !do_ggx && !do_brdf_lut) {
		std::cerr << "Usage:\n"
			<< "  cube in.png --lambertian out.png [--size N]\n"
			<< "  cube in.png --ggx out.png [--ggx-samples N]\n"
			<< "  cube --brdf-lut out.bin [--brdf-lut-size N] [--brdf-lut-samples N]\n"
			<< "  cube --merge-faces px.png nx.png py.png ny.png pz.png nz.png -o out.png\n";
		return 1;
	}

	// Load input cubemap if needed
	std::vector<float> input_hdr;
	int input_face_size = 0;
	std::vector<float> input_rgba; // RGBA32F for GPU upload

	if (do_lambertian || do_ggx) {
		if (input_path.empty()) {
			std::cerr << "Error: input cubemap path required for --lambertian or --ggx." << std::endl;
			return 1;
		}

		int img_w = 0, img_h = 0;
		std::unique_ptr<unsigned char, void(*)(void*)> pixels(
			stbi_load(input_path.c_str(), &img_w, &img_h, nullptr, 4),
			[](void *p) { stbi_image_free(p); }
		);
		if (!pixels) {
			std::cerr << "Error: failed to load '" << input_path << "': " << stbi_failure_reason() << std::endl;
			return 1;
		}
		if (img_w <= 0 || img_h != img_w * 6) {
			std::cerr << "Error: image dimensions (" << img_w << "x" << img_h
				<< ") don't match vertical strip cubemap (w x 6w)." << std::endl;
			return 1;
		}

		input_face_size = img_w;
		size_t total_pixels = size_t(input_face_size) * input_face_size * 6;

		std::cout << "Input: " << input_path << " (" << input_face_size << "x"
			<< input_face_size << " per face)" << std::endl;

		// Decode RGBE -> RGB float (for lambertian CPU path)
		input_hdr.resize(total_pixels * 3);
		decode_rgbe(pixels.get(), input_hdr.data(), total_pixels);

		// Also prepare RGBA32F for GPU upload
		if (do_ggx) {
			input_rgba.resize(total_pixels * 4);
			for (size_t i = 0; i < total_pixels; ++i) {
				input_rgba[i * 4 + 0] = input_hdr[i * 3 + 0];
				input_rgba[i * 4 + 1] = input_hdr[i * 3 + 1];
				input_rgba[i * 4 + 2] = input_hdr[i * 3 + 2];
				input_rgba[i * 4 + 3] = 1.0f;
			}
		}
	}

	// Lambertian (CPU)
	if (do_lambertian) {
		size_t output_total_pixels = size_t(lambertian_size) * lambertian_size * 6;
		std::vector<float> output_hdr(output_total_pixels * 3, 0.0f);

		std::cout << "Convolving lambertian (input " << input_face_size << "x" << input_face_size
			<< ", output " << lambertian_size << "x" << lambertian_size << " per face)..." << std::endl;

		auto t0 = std::chrono::high_resolution_clock::now();
		convolve_lambertian(input_hdr.data(), input_face_size, output_hdr.data(), lambertian_size);
		auto t1 = std::chrono::high_resolution_clock::now();
		double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
		std::cout << "Convolution took " << ms << " ms" << std::endl;

		std::vector<uint8_t> output_rgbe(output_total_pixels * 4);
		encode_rgbe(output_hdr.data(), output_rgbe.data(), output_total_pixels);

		int out_w = lambertian_size;
		int out_h = lambertian_size * 6;
		if (!stbi_write_png(lambertian_output.c_str(), out_w, out_h, 4, output_rgbe.data(), out_w * 4)) {
			std::cerr << "Error: failed to write '" << lambertian_output << "'." << std::endl;
			return 1;
		}
		std::cout << "Wrote: " << lambertian_output << " (" << out_w << "x" << out_h << ")" << std::endl;
	}

	// GGX and/or BRDF LUT (Vulkan compute)
	if (do_ggx || do_brdf_lut) {
		VulkanCompute vk;
		vk.init();
		std::cout << "Vulkan compute context initialized." << std::endl;

		if (do_ggx) {
			// Derive output base name (strip .png)
			std::string base = ggx_output;
			auto dot = base.rfind(".png");
			if (dot != std::string::npos) base = base.substr(0, dot);

			std::cout << "Computing GGX prefiltered cubemap mip chain..." << std::endl;
			auto t0 = std::chrono::high_resolution_clock::now();
			compute_ggx(vk, input_rgba.data(), uint32_t(input_face_size), base, ggx_samples);
			auto t1 = std::chrono::high_resolution_clock::now();
			double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
			std::cout << "GGX prefiltering took " << ms << " ms" << std::endl;
		}

		if (do_brdf_lut) {
			std::cout << "Computing BRDF LUT (" << brdf_lut_size << "x" << brdf_lut_size
				<< ", " << brdf_lut_samples << " samples)..." << std::endl;
			auto t0 = std::chrono::high_resolution_clock::now();
			compute_brdf_lut(vk, brdf_lut_output, brdf_lut_size, brdf_lut_samples);
			auto t1 = std::chrono::high_resolution_clock::now();
			double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
			std::cout << "BRDF LUT computation took " << ms << " ms" << std::endl;
		}

		vk.destroy();
	}

	return 0;
}
