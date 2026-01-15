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

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);
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
	}
	workspaces.clear();

	background_pipeline.destroy(rtg);

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

		vkCmdEndRenderPass(workspace.command_buffer);
	}



	// end recording:
	VK(vkEndCommandBuffer(workspace.command_buffer));

	//submit `workspace.command buffer` for the GPU to run:
	refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
}


void Tutorial::update(float dt) {
	time += dt;

}


void Tutorial::on_input(InputEvent const &) {
}
