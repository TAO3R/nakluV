#include "Tutorial.hpp"

#include "VK.hpp"
#include "refsol.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

// Constructor
Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);

	// pipeline creation, whcih is after the render pass creation
	// because pipeline creation requires a render pass to describe the output attachments the pipeline will be used with
	// before workspoace creation because will eventually create some per-pipeline, per workspace data
	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);

	{	// create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size());	// for easier-to-read counting

		std::array<VkDescriptorPoolSize, 1> pool_sizes{
			// we only need uniform buffer descriptors for the moment:
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1 * per_workspace,	// one descriptor per set, one set per workspace
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,	// because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors alocated from this pool
			.maxSets = 1 * per_workspace,	// one set per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);

		workspace.Camera_src = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,	// going to have GPU copy from this memory
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,	// host-visible memory, coherent (no special sync needed)
			Helpers::Mapped	// get a pointer to the memory
		);

		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,	// going to use as a uniform buffer, also going to have GPU copy into this memory
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,	// GPU-local memory
			Helpers::Unmapped	// don't get a pointer to the memory
		);

		{	// allocate descriptor set for Camera descriptor:
			VkDescriptorSetAllocateInfo alloc_info {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera,
			};

			VK(vkAllocateDescriptorSets(rtg.device,	 &alloc_info, &workspace.Camera_descriptors));
		}
		
		{	// point descriptor to Camera buffer:
			VkDescriptorBufferInfo Camera_info {
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			std::array<VkWriteDescriptorSet, 1> writes {
				VkWriteDescriptorSet {
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
				},
			};

			vkUpdateDescriptorSets (
				rtg.device,					// device
				uint32_t(writes.size()),	// descriptorWriteCount
				writes.data(),				// pDescriptorWrites
				0,							// descriptorCopyCount
				nullptr						// pDescriptorCopies
			);
		}
	}
}

// Destructor
Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);
		
		// line vertices buffers get cleaned up when the application is finished
		if (workspace.lines_vertices_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if (workspace.lines_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}

		// Camera_descriptors freed when pool is destroyed
		if (workspace.Camera_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}
		if (workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
	}
	workspaces.clear();

	if (descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr;
		// (this also frees the descriptor sets allocated from the pool)
	}

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);

	refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}

void Tutorial::destroy_framebuffers() {
	refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}


void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	//get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	//record (into `workspace.command_buffer`) commands that run a `render_pass` that just clears `framebuffer`:
	// refsol::Tutorial_render_record_blank_frame(rtg, render_pass, framebuffer, &workspace.command_buffer);
	
	// reset the command buffer (clear old commands):
	VK( vkResetCommandBuffer(workspace.command_buffer, 0));
	{	// begin recording:
		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,	// set to the proper type for this structure
			// .pNext set to nullptr by zero-initialization!
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,	// will record again every submit
		};
		VK(vkBeginCommandBuffer(workspace.command_buffer, &begin_info));
	}

	// resize line buffers if needed
	{
		if (!lines_vertices.empty()){ // upload lines vertices:
			// [re-]allocate lines buffers if needed
			size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
			if (workspace.lines_vertices_src.handle == VK_NULL_HANDLE || workspace.lines_vertices_src.size < needed_bytes) {
				// round to the next multiupole of 4k to avoid re-allocating continuously if vertex count grows slowly:
				size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
				
				// clean up code for the buffers if they are already allocated
				if (workspace.lines_vertices_src.handle) {
					rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
				}

				if (workspace.lines_vertices.handle) {
					rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
				}

				// actual allocation
				workspace.lines_vertices_src = rtg.helpers.create_buffer(	// CPU visible staging buffer
					new_bytes,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,	// going to have GPU copy from this memory
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,	// host-visible memory, coherent (no special sync needed)
					Helpers::Mapped	// get a pointer to the memory
				);
				workspace.lines_vertices = rtg.helpers.create_buffer(	// GPU-local vertex buffer
					new_bytes,
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,	// going to use as vertex buffer, also going to have GPU into this memory
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,	// GPU-local memory
					Helpers::Unmapped	// don't get a pointer to the memory
				);

				std::cout << "Re-allocated lines buffers to " << new_bytes << " bytes." << std::endl;

				// host-side (CPU) copy from lines_vertices into workspace.lines_vertices_src:
				assert(workspace.lines_vertices_src.allocation.mapped);
				std::memcpy(workspace.lines_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);
			
				// device-side copy from workspace.lines_vertices_src -> workspace.lines_vertices:
				VkBufferCopy copy_region{
					.srcOffset = 0,
					.dstOffset = 0,
					.size = needed_bytes,
				};
				vkCmdCopyBuffer(workspace.command_buffer, workspace.lines_vertices_src.handle, workspace.lines_vertices.handle, 1, &copy_region);
			}

			assert(workspace.lines_vertices_src.size == workspace.lines_vertices.size);
			assert(workspace.lines_vertices_src.size >= needed_bytes);
		}

	}	// end of line buffers resize

	{	// upload camera info:
		LinesPipeline::Camera camera {
			.CLIP_FROM_WORLD = CLIP_FROM_WORLD
		};
		assert(workspace.Camera_src.size == sizeof(camera));

		// host-side copy into Camera_src:
		memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));

		// add device-side copy from Camera_src -> Camera:
		assert(workspace.Camera_src.size == workspace.Camera.size);
		VkBufferCopy copy_region {
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Camera_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &copy_region);
	}

	{	// memory barrier to make sure copies complete before rendering happens:
		VkMemoryBarrier memory_barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};
		
		vkCmdPipelineBarrier(workspace.command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,	// srcStageMask
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,	// dstStageMask
			0,	// dependencyFlags
			1, &memory_barrier,	// memoryBarriers (count, data)
			0, nullptr,	// bufferMemoryBarriers (count, data)
			0, nullptr	// imageMemoryBarriers (count, data)
		);
	}

	// GPU commands
	{	// render pass
		std::array<VkClearValue, 2> clear_values{
			VkClearValue{.color{.float32{0.1768f, 0.3636f, 0.0231f, 1.0f}}},		// color buffer, already powered by 2.2 from sRGB to linear
			VkClearValue{.depthStencil{.depth = 1.0f, .stencil = 0}},				// depth buffer
		};

		VkRenderPassBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass,
			.framebuffer = framebuffer,				// specific image reference to render to
			.renderArea{							// the pixel area that will be rendered to
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,		// current size of the swapchain, whole size of the image being rendered
			},
			.clearValueCount = uint32_t(clear_values.size()),
			.pClearValues = clear_values.data(),	// "loaded" by being cleared to a constant value
		};

		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

		// run pipeline
		{	// set scissor rectangle (the subset of the screen that gets drawn to):
			VkRect2D scissor{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);	// State commands
		}
		{	// configure viewport transform (how device coordinates map to window coordinates):
			VkViewport viewport{
				.x = 0.0f,
				.y = 0.0f,
				.width = float(rtg.swapchain_extent.width),
				.height = float(rtg.swapchain_extent.height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);	// State commands
		}
		// the above two settings make sure that our pipeline's output will exactly cover the swapchain image getting rendered to

		{	// draw with the background pipeline:

			// any subsequent draw commands should use our freshly created background pipeline
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);	// State commands

			{	// push time:
				BackgroundPipeline::Push push{
					.time = time,
				};
				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}

			// Action command, uses parameters set by state commands, runs the pipeline for - reading the parameters -
			// 3 vertices and 1 instance, starting at vertex 0 and instance 0 - draws exactly one triangle
			vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		}

		{	// draw with the lines pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);

			{	// use workspace.lines_vertices (offset 0) as vertex buffer binding 0:
				std::array<VkBuffer, 1 > vertex_buffers{workspace.lines_vertices.handle};
				std::array<VkDeviceSize, 1 > offsets{0};
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			// draw lines vertices
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		{	// bind Camera descriptor set:
			std::array<VkDescriptorSet, 1> descriptor_sets {
				workspace.Camera_descriptors,	// 0： Camera
			};
			vkCmdBindDescriptorSets(
				workspace.command_buffer,	// command buffer
				VK_PIPELINE_BIND_POINT_GRAPHICS,	// pipeline bind point
				lines_pipeline.layout,	// pipeline layout
				0,	// first set
				uint32_t(descriptor_sets.size()), descriptor_sets.data(),	// descriptor sets count, ptr
				0, nullptr	// dynamic offsets count, ptr
			);
		}

		// draw lines vertices:
		vkCmdEndRenderPass(workspace.command_buffer);
	}

	// end recording:
	VK(vkEndCommandBuffer(workspace.command_buffer));

	//submit `workspace.command buffer` for the GPU to run:
	refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
}


void Tutorial::update(float dt) {
	// modify time in every update
	time += dt;

	{	// camera orbiting the origin:
		float ang = float(M_PI) * 2.0f * 10.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
			60.0f * float(M_PI) /180.0f,	// vfov
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),	// aspect
			0.1f,	// near
			1000.0f	// far
		) * look_at(
			3.0f * std::cos(ang), 3.0f * std::sin(ang), 1.0f, 	//eye
			0.0f, 0.0f, 0.5f, 	// target
			0.0f, 0.0f, 1.0f	// up
		);
	}

	{	//make an 'x':
	// 	lines_vertices.clear();
	// 	lines_vertices.reserve(4);
	// 	lines_vertices.emplace_back(PosColVertex{
	// 		.Position{ .x = -2.0f, .y = -2.0f, .z = 0.0f },
	// 		.Color{ .r = 0xff, .g = 0xff, .b = 0xff, .a = 0xff }
	// 	});	// (-1, -1, 0)
	// 	lines_vertices.emplace_back(PosColVertex{
	// 		.Position{ .x =  2.0f, .y =  2.0f, .z = 0.0f },
	// 		.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff }
	// 	});	// (1, 1, 0)
	// 	lines_vertices.emplace_back(PosColVertex{
	// 		.Position{ .x = -1.0f, .y =  1.0f, .z = 0.0f },
	// 		.Color{ .r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff }
	// 	});	// (-1, 1, 0)
	// 	lines_vertices.emplace_back(PosColVertex{
	// 		.Position{ .x =  1.0f, .y = -1.0f, .z = 0.0f },
	// 		.Color{ .r = 0x00, .g = 0x00, .b = 0xff, .a = 0xff }
	// 	});	// (1, -1, 0)
	// 	assert(lines_vertices.size() == 4);
	}

	{	// make some crossing lines at different depths：
		// lines_vertices.clear();
		// constexpr size_t count = 2 * 30 + 2 * 30;
		// lines_vertices.reserve(count);
		
		// // horizontal lines at z = 0,5f;
		// for (uint32_t i = 0; i < 30; i++) {
		// 	float y = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
		// 	lines_vertices.emplace_back(PosColVertex{
		// 		.Position{.x = -1.0f, .y = y, .z = 0.5f},
		// 		.Color{.r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff},
		// 	});
		// 	lines_vertices.emplace_back(PosColVertex{
		// 		.Position{.x = 1.0f, .y = y, .z = 0.5f},
		// 		.Color{.r = 0xff, .g = 0xff, .b = 0x00, .a = 0xff},
		// 	});
		// }

		// // vertical lines at z = 0.0f (near) through 1.0f (far):
		// for (uint32_t i = 0; i < 30; i++) {
		// 	float x = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
		// 	float z = (i + 0.5f) / 30.0f;
		// 	lines_vertices.emplace_back(PosColVertex{
		// 		.Position{.x = x, .y = -1.0f, .z = z},
		// 		.Color{.r = 0x44, .g = 0x00, .b = 0xff, .a = 0xff},
		// 	});
		// 	lines_vertices.emplace_back(PosColVertex{
		// 		.Position{.x = x, .y = 1.0f, .z = z},
		// 		.Color{.r = 0x44, .g = 0x00, .b = 0xff, .a = 0xff},
		// 	});
		// }
		// assert(lines_vertices.size() == count);
	}

	{	// cube
		lines_vertices.clear();
		const uint32_t lines_per_axis = 3;
		constexpr size_t count = (int)lines_per_axis * 2 * 6 + 12;
		float cube_edge_length = .5f;
		lines_vertices.reserve(count);
		
		// the center of the cube is at (0, 0, .5), 3 axes are bounded by [-1, 1], [-1, 1], [0, 1]
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f
			},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// a
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f
			},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// b

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// a
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// c

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// c
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// d

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// b
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// d

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// a
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// e

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// e
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// g

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// g
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// c

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// g
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// h

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// h
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// d

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// e
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// f

		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f - (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// b
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// f
		
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = -(cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// f
		lines_vertices.emplace_back(PosColVertex{
			.Position{
				.x = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.y = (cube_edge_length + 0.4f * sinf(time)) / 2.0f,
				.z = 0.5f + (cube_edge_length + 0.4f * sinf(time)) / 2.0f},
			.Color{.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xff},
		});	// h
	}
}


void Tutorial::on_input(InputEvent const &) {
}
