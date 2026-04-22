#include "../Tutorial.hpp"
#include "../RTG.hpp"
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

void Tutorial::build_gpu_lights() {
	gpu_lights.clear();
	gpu_lights.reserve(light_instances.size());

	constexpr float INF = std::numeric_limits<float>::infinity();

	for (auto &li : light_instances) {
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
}

void Tutorial::upload_lights(Workspace &workspace) {
	size_t needed_bytes = sizeof(GPULightHeader) + gpu_lights.size() * sizeof(GPULight);

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

	VkBufferCopy copy_region{
		.srcOffset = 0,
		.dstOffset = 0,
		.size = needed_bytes,
	};
	vkCmdCopyBuffer(workspace.command_buffer, workspace.Lights_src.handle, workspace.Lights.handle, 1, &copy_region);
}
