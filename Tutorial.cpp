#include "Tutorial.hpp"

#include "VK.hpp"
// #include "refsol.hpp"
#include "print_scene.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>

// Constructor
Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	// refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);
	// select a depth format:
	// (at least one of these two must be supported, according to the spec; but neither are required)
	depth_format = rtg.helpers.find_image_format(
		{VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32},
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	{	// create render pass
		// attachments
		std::array<VkAttachmentDescription, 2> attachments{
			VkAttachmentDescription {	// 0 - color attachment:
				.format = rtg.surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = rtg.present_layout,
			},
			VkAttachmentDescription {	// 1 - depth attachment:
				.format = depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
		};
		
		// subpass
		VkAttachmentReference color_attachment_ref {
			.attachment  = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference depth_attachment_ref {
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass {
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_ref,
			.pDepthStencilAttachment = &depth_attachment_ref,
		};

		// dependencies
		// this defers the image load actions for the attachments:
		std::array<VkSubpassDependency, 2> dependencies {
			VkSubpassDependency {
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			},
			VkSubpassDependency {
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			}
		};

		VkRenderPassCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = uint32_t(dependencies.size()),
			.pDependencies = dependencies.data(),
		};

		VK(vkCreateRenderPass(rtg.device, &create_info, nullptr, &render_pass));
	}

	{	// create command pool
		VkCommandPoolCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value(),
		};
		VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
	}

	// pipeline creation, whcih is after the render pass creation
	// because pipeline creation requires a render pass to describe the output attachments the pipeline will be used with
	// before workspoace creation because will eventually create some per-pipeline, per workspace data
	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{	// create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size());	// for easier-to-read counting

		std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace,	// one descriptor per set, two set per workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1 * per_workspace,	// one descriptor per set, one set per workspace
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,	// because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors alocated from this pool
			.maxSets = 3 * per_workspace,	// three sets per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}

	workspaces.resize(rtg.workspaces.size());
	// std::cout << "workspaces.size(): " << workspaces.size() << std::endl;
	for (Workspace &workspace : workspaces) {
		// refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);
		
		{	// allocate command buffer:
			VkCommandBufferAllocateInfo alloc_info {
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};

			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.command_buffer));
		}

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

		workspace.World_src = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);
		workspace.World = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		{ // allocate descriptor set for World descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set0_World,
			};

			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors) );
			// NOTE: will actually fill in this descriptor set just a bit lower
		}

		{	// allocate descriptor set for Transforms descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set1_Transforms,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors));

			// NOTE: will fill in this descriptor set in render when buffers are [re-]allocated
		}
		
		{	// point descriptor to Camera buffer:
			VkDescriptorBufferInfo Camera_info {
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			VkDescriptorBufferInfo World_info{
				.buffer = workspace.World.handle,
				.offset = 0,
				.range = workspace.World.size,
			};

			std::array<VkWriteDescriptorSet, 2> writes {
				VkWriteDescriptorSet {
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &World_info,
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
	}	// end of the for-loop for per-workspace descriptor set allocation

	// A1: scene load and info print if specified
	if (!rtg.configuration.scene_file.empty())
	{
		try {
			scene_S72 = S72::load(rtg.configuration.scene_file.data());
			if (rtg.configuration.print_scene)
			{
				print_info(scene_S72);
				print_scene_graph(scene_S72);
			}
		} catch (std::exception &e) {
			std::cerr << "Failed to load s72-format scene from " << rtg.configuration.scene_file << "\n" << e.what() << std::endl;
		}
	}	// end of scene load and info print

	// A1: load .b72 files into memory if the scene is successfully loaded
	if (scene_S72.scene.name.empty() && scene_S72.scene.roots.empty())
	{
		std::cout << "No valid scene loaded." << std::endl;
	}
	else
	{
		std::cout << "\n[Tutorial.cpp]: loading .b72 binary files into memory." << std::endl;
		// iterate through all data files referenced by the scene
		for (auto& [src_name, data_file] : scene_S72.data_files)	// <std::string, DataFile>
		{
			// Open file in binary mode, positioned at end
			std::ifstream file(data_file.path, std::ios::binary | std::ios::ate);
			
			// Get file size (we're at end due to ios::ate)
			size_t size = file.tellg();
			
			// Rewind to beginning
			file.seekg(0, std::ios::beg);
			
			// Allocate buffer and read all bytes
			std::vector<uint8_t> bytes(size);
			file.read(reinterpret_cast<char*>(bytes.data()), size);
			
			// Store in map
			loaded_data[src_name] = std::move(bytes);
		}

		// === DEBUG: Print loaded binary file info ===
		std::cout << "--- Loaded Binary Data Files ---" << std::endl;
		std::cout << "Total files loaded: " << loaded_data.size() << std::endl;

		size_t total_bytes = 0;
		for (auto& [src_name, bytes] : loaded_data) {
			std::cout << "  " << src_name << ": " << bytes.size() << " bytes" << std::endl;
			total_bytes += bytes.size();
		}

		std::cout << "Total bytes loaded: " << total_bytes;
		if (total_bytes > 1024 * 1024) {
			std::cout << " (" << (total_bytes / (1024.0 * 1024.0)) << " MB)";
		} else if (total_bytes > 1024) {
			std::cout << " (" << (total_bytes / 1024.0) << " KB)";
		}
		std::cout << std::endl;
		std::cout << "--------------------------------" << std::endl;

	}	// end of loading .b72 files

	{	// A1: construct scene meshes from loaded binary files and upload to the GPU
		// Per A1 spec:
		//  - No indices (non-indexed TRIANGLE_LIST only)
		//  - All attributes interleaved at stride 48:
		//      POSITION  offset  0  R32G32B32_SFLOAT    (12 bytes)
		//      NORMAL    offset 12  R32G32B32_SFLOAT    (12 bytes)
		//      TANGENT   offset 24  R32G32B32A32_SFLOAT (16 bytes, skipped for now)
		//      TEXCOORD  offset 40  R32G32_SFLOAT       ( 8 bytes)
		//  - Materials are always lambertian

		if (!loaded_data.empty())
		{
			std::vector<PosNorTexVertex> all_vertices;

			for (auto& [mesh_name, mesh] : scene_S72.meshes)
			{
				SceneMesh scene_mesh;
				scene_mesh.material = mesh.material;
				scene_mesh.vertices.first = uint32_t(all_vertices.size());

				// attribute
				auto pos_it = mesh.attributes.find("POSITION");
				auto nor_it = mesh.attributes.find("NORMAL");
				auto tex_it = mesh.attributes.find("TEXCOORD");

				if (pos_it == mesh.attributes.end()) {
					std::cerr << "[Tutorial.cpp]: Mesh '" << mesh_name << "' missing POSITION attribute, skipping.\n";
					continue;
				}

				auto& pos_attr = pos_it->second;

				// All attributes should reference the same src file with stride 48 per spec.
				// We use POSITION's src as the base buffer.
				auto data_it = loaded_data.find(pos_attr.src.src);
				if (data_it == loaded_data.end()) {
					std::cerr << "[Tutorial.cpp]: Mesh '" << mesh_name << "' references data file '" << pos_attr.src.src << "' which was not loaded, skipping.\n";
					continue;
				}
				const uint8_t* base_data = data_it->second.data();
				const size_t   base_size = data_it->second.size();

				// Expected offsets within each 48-byte vertex stride:
				const uint32_t stride   = pos_attr.stride;  // should be 48
				const uint32_t pos_off  = pos_attr.offset;  // should be 0
				const uint32_t nor_off  = (nor_it != mesh.attributes.end()) ? nor_it->second.offset : 12;
				const uint32_t tex_off  = (tex_it != mesh.attributes.end()) ? tex_it->second.offset : 40;

				// Non-indexed: mesh.count is the vertex count directly
				const uint32_t vertex_count = mesh.count;

				// Bounds check
				if (pos_off + vertex_count * stride > base_size) {
					std::cerr << "[Tutorial.cpp]: Mesh '" << mesh_name << "' attribute data exceeds buffer size, skipping.\n";
					continue;
				}

				// Extract vertices, skipping TANGENT (we don't need it for now)
				all_vertices.reserve(all_vertices.size() + vertex_count);
				for (uint32_t i = 0; i < vertex_count; ++i)
				{
					const uint8_t* vertex_base = base_data + i * stride;

					const float* pos = reinterpret_cast<const float*>(vertex_base + pos_off);
					const float* nor = reinterpret_cast<const float*>(vertex_base + nor_off);
					const float* tex = reinterpret_cast<const float*>(vertex_base + tex_off);

					// setting model-space aabb
					scene_mesh.min_x = std::min(scene_mesh.min_x, pos[0]);
					scene_mesh.max_x = std::max(scene_mesh.max_x, pos[0]);
					scene_mesh.min_y = std::min(scene_mesh.min_y, pos[1]);
					scene_mesh.max_y = std::max(scene_mesh.max_y, pos[1]);
					scene_mesh.min_z = std::min(scene_mesh.min_z, pos[2]);
					scene_mesh.max_z = std::max(scene_mesh.max_z, pos[2]);

					all_vertices.push_back(PosNorTexVertex{
						.Position{.x = pos[0], .y = pos[1], .z = pos[2]},
						.Normal  {.x = nor[0], .y = nor[1], .z = nor[2]},
						.TexCoord{.s = tex[0], .t = tex[1]},
					});
				}

				scene_mesh.vertices.count = vertex_count;
				scene_meshes[mesh_name] = scene_mesh;

				std::cout << "[Tutorial.cpp]: Loaded mesh '" << mesh_name << "': " << vertex_count
				          << " vertices (" << vertex_count / 3 << " triangles)" << std::endl;
			}	// end of the for-loop for scene mesh traversal

			// upload to the GPU
			if (!all_vertices.empty())
			{
				size_t bytes = all_vertices.size() * sizeof(PosNorTexVertex);
				scene_vertices = rtg.helpers.create_buffer(
					bytes,
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					Helpers::Unmapped
				);
				rtg.helpers.transfer_to_buffer(all_vertices.data(), bytes, scene_vertices);

				std::cout << "[Tutorial.cpp]: Uploaded " << all_vertices.size() << " scene vertices ("
				          << bytes << " bytes) to GPU." << std::endl;
			}
		}
		else
		{
			std::cout << "[Tutorial.cpp]: no binary data loaded when trying to construct scene meshes." << std::endl;
		}
	}	// end of scene mesh construction

	{	// A1: build SecneCamera instances from the scene and set camera (index & mode) if specified
		if (!rtg.configuration.scene_file.empty())
		{
			for (auto &root : scene_S72.scene.roots)
			{
				collect_cameras(root, mat4_identity());
			}
		}
		
		if (!rtg.configuration.scene_camera.empty())
		{
			for (uint32_t i = 0; i < scene_cameras.size(); ++i) {
				if (scene_cameras[i].camera->name == rtg.configuration.scene_camera) {
					scene_camera_index = i;
					break;
				}
			}

			if (scene_camera_index == -1)
			{
				std::cerr << "[Tutorial.cpp]: Cannot find a scene camera with the specified name." << std::endl;
				std::exit(1);
			}
			else
			{
				camera_mode = CameraMode::Scene;
			}
		}
	}

	{	// create object vertices
		std::vector<PosNorTexVertex> vertices;

		{	// A [-1,1]x[-1,1]x{0} quadrilateral:
			plane_vertices.first = uint32_t(vertices.size());

			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.0f, .y = -1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 0.0f, .t = 0.0f},
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.0f, .y = -1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 1.0f, .t = 0.0f},
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.0f, .y = 1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 0.0f, .t = 1.0f},
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.0f, .y = 1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 1.0f, .t = 1.0f},
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.0f, .y = 1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 0.0f, .t = 1.0f},
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.0f, .y = -1.0f, .z = 0.0f },
				.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{.s = 1.0f, .t = 0.0f},
			});

			plane_vertices.count = uint32_t(vertices.size()) - plane_vertices.first;
		}

		{	// A torus:
			torus_vertices.first = uint32_t(vertices.size());	// always be on index after the current last element in the vector

			// will parameterize with (u,v) where:
			// -u is angle around main axis (+z)
			// -v is angle around the cube

			constexpr float R1 = 0.75f;	// main radius
			constexpr float R2 = 0.15f;	// tube radius

			constexpr uint32_t U_STEPS = 20;
			constexpr uint32_t V_STEPS = 16;

			// texture repeats around the torus:
			constexpr float V_REPEATS = 2.0f;
			constexpr float U_REPEATS = int(V_REPEATS / R2 * R1 + 0.999f);	// approxiamtely square, rounded up

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				// convert steps to angles:
				// (doing the mod since trig on 2 M_PI may not exactly match 0)
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.0f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.0f * float(M_PI);

				vertices.emplace_back(PosNorTexVertex{
					.Position{
						.x = (R1 + R2 * std::cos(va)) * std::cos(ua),
						.y = (R1 + R2 * std::cos(va)) * std::sin(ua),
						.z = R2 * std::sin(va),
					},
					.Normal{
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui, vi+1);

					emplace_vertex(ui, vi+1);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui+1, vi+1);
				}
			}

			torus_vertices.count = uint32_t(vertices.size()) - torus_vertices.first;
		}

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		// copy data to buffer:
		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	{ 	// make some textures
		textures.reserve(2);

		{ // texture 0 will be a dark grey / light grey checkerboard with a red square at the origin.
			// actually make the texture:
			uint32_t size = 128;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				float fy = (y + 0.5f) / float(size);
				for (uint32_t x = 0; x < size; ++x) {
					float fx = (x + 0.5f) / float(size);
					// highlight the origin:
					if      (fx < 0.05f && fy < 0.05f) data.emplace_back(0xff0000ff); //red
					else if ( (fx < 0.5f) == (fy < 0.5f)) data.emplace_back(0xff444444); //dark grey
					else data.emplace_back(0xffbbbbbb); //light grey
				}
			}
			assert(data.size() == size*size);

			//make a place for the texture to live on the GPU
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size , .height = size }, //size of image
				VK_FORMAT_R8G8B8A8_UNORM, //how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped
			));

			// transfer data
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}

		{ // texture 1 will be a classic 'xor' texture
			//actually make the texture:
			uint32_t size = 256;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					uint8_t r = uint8_t(x) ^ uint8_t(y);
					uint8_t g = uint8_t(x + 128) ^ uint8_t(y);
					uint8_t b = uint8_t(x) ^ uint8_t(y + 27);
					uint8_t a = 0xff;
					data.emplace_back( uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24) );
				}
			}
			assert(data.size() == size*size);

			//make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size , .height = size }, //size of image
				VK_FORMAT_R8G8B8A8_SRGB, //how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped
			));

			//transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}
	}	// end of making some textures

	{ 	// make image views for the textures
		texture_views.reserve(textures.size());
		for (Helpers::AllocatedImage const &image : textures) {
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = image.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = image.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VkImageView image_view = VK_NULL_HANDLE;
			VK( vkCreateImageView(rtg.device, &create_info, nullptr, &image_view) );

			texture_views.emplace_back(image_view);
		}
		assert(texture_views.size() == textures.size());
	}

	{ // make a sampler for the textures
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f,	// doesn't matter if anisotropy isn't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS,	// doesn't matter if compare isn't enabled
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK( vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler) );
	}
		
	{ // create the texture descriptor pool
		uint32_t per_texture = uint32_t(textures.size()); //for easier-to-read counting

		std::array< VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 * 1 * per_texture, //one descriptor per set, one set per texture
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, //because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 1 * per_texture, //one set per texture
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK( vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool) );
	}

	{ // allocate and write the texture descriptor sets

		//allocate the descriptors (using the same alloc_info):
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = texture_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &objects_pipeline.set2_Texture,
		};
		texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);
		for (VkDescriptorSet &descriptor_set : texture_descriptors) {
			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set) );
		}

		// write descriptors for textures
		std::vector< VkDescriptorImageInfo > infos(textures.size());
		std::vector< VkWriteDescriptorSet > writes(textures.size());

		for (Helpers::AllocatedImage const &image : textures) {
			size_t i = &image - &textures[0];
			
			infos[i] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[i],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			writes[i] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = texture_descriptors[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i],
			};
		}

		vkUpdateDescriptorSets( rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr );
	}
}	// end of Tutorial constructor

// Destructor
Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	// clean up for texture
	if (texture_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, texture_descriptor_pool, nullptr);
		texture_descriptor_pool = nullptr;

		//this also frees the descriptor sets allocated from the pool:
		texture_descriptors.clear();
	}

	if (texture_sampler) {
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
	}

	for (VkImageView &view : texture_views) {
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}
	texture_views.clear();

	for (auto &texture : textures) {
		rtg.helpers.destroy_image(std::move(texture));
	}
	textures.clear();

	rtg.helpers.destroy_buffer(std::move(object_vertices));

	// A1: scene data cleanup
	if (scene_vertices.handle != VK_NULL_HANDLE)
	{
		rtg.helpers.destroy_buffer(std::move(scene_vertices));
	}

	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		// refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);
		
		if (workspace.command_buffer != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
			workspace.command_buffer = VK_NULL_HANDLE;
		}

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

		// World_descriptors freed when pool is destroyed.
		if (workspace.World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}
		if (workspace.World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}
		
		// Transforms_descriptors freed when pool is destroyed
		if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}
		if (workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
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
	objects_pipeline.destroy(rtg);

	// refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
	// destroy command pool
	if (command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
	}

	if (render_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(rtg.device, render_pass, nullptr);
		render_pass = VK_NULL_HANDLE;
	}
}

// A1
void Tutorial::traverse_node(S72::Node *node, mat4 parent_transform)
{
	//	Compute this node's local transform from its TRS components:
	// 	local_transform = T * R * S  (scale first, then rotate, then translate)
	mat4 local_transform = mat4_translation(node->translation.x, node->translation.y, node->translation.z)
						 * mat4_rotation(node->rotation.x, node->rotation.y, node->rotation.z, node->rotation.w)
						 * mat4_scale(node->scale.x, node->scale.y, node->scale.z);

	//	Accumulate with parent: WORLD_FROM_LOCAL = parent_transform * local_transform
	mat4 WORLD_FROM_LOCAL = parent_transform * local_transform;

	//	If this node has a mesh, emit an ObjectInstance:
	//  a. Look up the mesh name in scene_meshes to get the ObjectVertices (first/count)
	//  b. Compute CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL
	//  c. Compute WORLD_FROM_LOCAL_NORMAL = inverse transpose of WORLD_FROM_LOCAL
	//     (needed to correctly transform normals when non-uniform scale is present;
	//      if scale is uniform, WORLD_FROM_LOCAL itself works fine as a shortcut)
	//  d. Push an ObjectInstance with vertices, transform, and texture index
	if (node->mesh != nullptr)
	{
		auto it = scene_meshes.find(node->mesh->name);
		if (it == scene_meshes.end()) { return; }

		mat4 WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL;
		object_instances.emplace_back(ObjectInstance{
			.vertices = it->second.vertices,
			.transform {
				.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
				.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
				.WORLD_FROM_LOCAL_NORMAL = mat4_inverse_transpose(WORLD_FROM_LOCAL_NORMAL),
			}
		});
	}

	//	Recurse into children, passing WORLD_FROM_LOCAL as their parent_transform
	for (auto& child_node : node->children)
	{
		traverse_node(child_node, WORLD_FROM_LOCAL);
	}

}	// end of traverse_node

// A1
void Tutorial::collect_cameras(S72::Node *node, mat4 parent_transform)
{
	mat4 local_transform = mat4_translation(node->translation.x, node->translation.y, node->translation.z)
						 * mat4_rotation(node->rotation.x, node->rotation.y, node->rotation.z, node->rotation.w)
						 * mat4_scale(node->scale.x, node->scale.y, node->scale.z);

	mat4 WORLD_FROM_LOCAL = parent_transform * local_transform;
	
	//	If this node has a camera, emit a SceneCamera:
	if (node->camera != nullptr)
	{
		scene_cameras.emplace_back(
			SceneCamera{
				.camera = node->camera,
				.WORLD_FROM_CAMERA = WORLD_FROM_LOCAL,
			});
		std::cout << "[Tutorial.cpp]: Emplacing camera: {" << node->camera->name << "} into scene_cameras." << std::endl;
	}

	for (auto &child_node : node->children)
	{
		collect_cameras(child_node, WORLD_FROM_LOCAL);
	}
}	// end of collect_cameras

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	// refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
	
	// clean up existing framebuffers (and depth image):
	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	// allocate depth image for framebuffers to share
	swapchain_depth_image = rtg.helpers.create_image(
		swapchain.extent,
		depth_format,	// determined during startup
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);

	{	// create an image view of te depth image
		VkImageViewCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &swapchain_depth_image_view));
	}

	// create framebuffers pointing to each swapchain image view and the shared depth image view
	swapchain_framebuffers.assign(swapchain.image_views.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < swapchain.image_views.size(); i++) {
		std::array<VkImageView, 2> attachments {
			swapchain.image_views[i],
			swapchain_depth_image_view,
		};
		VkFramebufferCreateInfo create_info {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.layers = 1,
		};

		VK(vkCreateFramebuffer(rtg.device, &create_info, nullptr, &swapchain_framebuffers[i]));
	}

}

void Tutorial::destroy_framebuffers() {
	// refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
	for (VkFramebuffer &framebuffer : swapchain_framebuffers) {
		assert(framebuffer != VK_NULL_HANDLE);
		vkDestroyFramebuffer(rtg.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}
	swapchain_framebuffers.clear();

	assert(swapchain_depth_image_view != VK_NULL_HANDLE);
	vkDestroyImageView(rtg.device, swapchain_depth_image_view, nullptr);
	swapchain_depth_image_view = VK_NULL_HANDLE;

	rtg.helpers.destroy_image(std::move(swapchain_depth_image));
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

	{	// upload world info:
		assert(workspace.Camera_src.size == sizeof(world));

		// host-side copy into World_src:
		memcpy(workspace.World_src.allocation.data(), &world, sizeof(world));

		// add device-side copy from World_src -> World:
		assert(workspace.World_src.size == workspace.World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.World_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.World_src.handle, workspace.World.handle, 1, &copy_region);
	}

	// resize Object Transforms buffers if needed
	{
		if (!object_instances.empty()){ // upload object transforms:
			// [re-]allocate transforms buffers if needed
			size_t needed_bytes = object_instances.size() * sizeof(ObjectsPipeline::Transform);
			if (workspace.Transforms_src.handle == VK_NULL_HANDLE || workspace.Transforms_src.size < needed_bytes) {
				// round to the next multiupole of 4k to avoid re-allocating continuously if vertex count grows slowly:
				size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
				
				// clean up code for the buffers if they are already allocated
				if (workspace.Transforms_src.handle) {
					rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
				}

				if (workspace.Transforms.handle) {
					rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
				}

				// actual allocation
				workspace.Transforms_src = rtg.helpers.create_buffer(	// CPU visible staging buffer
					new_bytes,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,	// going to have GPU copy from this memory
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,	// host-visible memory, coherent (no special sync needed)
					Helpers::Mapped	// get a pointer to the memory
				);
				workspace.Transforms = rtg.helpers.create_buffer(	// GPU-local vertex buffer
					new_bytes,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,	// going to use as vertex buffer, also going to have GPU into this memory
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,	// GPU-local memory
					Helpers::Unmapped	// don't get a pointer to the memory
				);

				// update the descriptor set:
				VkDescriptorBufferInfo Transforms_info {
					.buffer = workspace.Transforms.handle,
					.offset = 0,
					.range = workspace.Transforms.size,
				};

				std::array< VkWriteDescriptorSet, 1> writes {
					VkWriteDescriptorSet {
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.dstSet = workspace.Transforms_descriptors,
						.dstBinding = 0,
						.dstArrayElement = 0,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
						.pBufferInfo = &Transforms_info,
					},
				};

				vkUpdateDescriptorSets(
					rtg.device,
					uint32_t(writes.size()), writes.data(),	// descriptorWrites count, data
					0, nullptr	// descriptorCopies count, data
				);

				std::cout << "Re-allocated object transforms buffers to " << new_bytes << " bytes." << std::endl;
			}

			assert(workspace.Transforms_src.size == workspace.Transforms.size);
			assert(workspace.Transforms_src.size >= needed_bytes);
			
			{	// copy transforms into Transforms_src:
				assert(workspace.Transforms_src.allocation.mapped);
				ObjectsPipeline::Transform *out = reinterpret_cast<ObjectsPipeline::Transform *>(workspace.Transforms_src.allocation.data());	// strict aliasing violation, but it doesn't matter

				for (ObjectInstance const &inst : object_instances) {
					*out = inst.transform;
					++out;
				}
			}	// end of copy transforms

			// device-side copy from Transforms_src -> Transforms:
			VkBufferCopy copy_region{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = needed_bytes,
			};
			vkCmdCopyBuffer(workspace.command_buffer, workspace.Transforms_src.handle, workspace.Transforms.handle, 1, &copy_region);
		}	// end of if

	}	// end of object transforms buffers resize

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
			
			{	// bind Camera descriptor set:
				std::array<VkDescriptorSet, 1> descriptor_sets{
					workspace.Camera_descriptors,	// 0: Camera
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

			// draw lines vertices
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		if (!object_instances.empty())
		{	// draw with the objects pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);

			{	// A1: bind the appropriate vertex buffer:
				// if a scene was loaded, use scene_vertices; otherwise use the hardcoded object_vertices
				VkBuffer vb = (scene_vertices.handle != VK_NULL_HANDLE) ? scene_vertices.handle : object_vertices.handle;
				std::array<VkBuffer, 1> vertex_buffers{vb};
				std::array<VkDeviceSize, 1> offsets{0};
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind World and Transforms descriptor set:
				std::array< VkDescriptorSet, 2 > descriptor_sets{
					workspace.World_descriptors,	// 0: World
					workspace.Transforms_descriptors, // 1: Transforms
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer,	// command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS,	// pipeline bind point
					objects_pipeline.layout,	// pipeline layout
					0,	// first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(),	// descriptor sets count, ptr
					0, nullptr	// dynamic offsets count, ptr
				);
			}
		
			// draw all instances:
			for (ObjectInstance const &inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);

				vkCmdBindDescriptorSets(
					workspace.command_buffer,	// command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS,	// pipeline bind point
					objects_pipeline.layout,	// pipeline layout
					2,	// second set
					1, &texture_descriptors[inst.texture],	// descriptor sets count, prt
					0, nullptr	// dynamic offsets count, ptr
				);

				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}
		}	// end of draw with objects pipeline

		vkCmdEndRenderPass(workspace.command_buffer);
	}

	// end recording:
	VK(vkEndCommandBuffer(workspace.command_buffer));

	{	//submit `workspace.command buffer` for the GPU to run:
		// refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
		std::array<VkSemaphore, 1> wait_semaphores {
			render_params.image_available
		};	// signalled when the image is done being presented and is ready to render to
		std::array<VkPipelineStageFlags, 1> wait_stages {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

		std::array<VkSemaphore, 1> signal_semaphores {
			render_params.image_done
		};	// should be signalled after the rendering work in this batch is done
		VkSubmitInfo submit_info {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = uint32_t(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &workspace.command_buffer,
			.signalSemaphoreCount = uint32_t(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data(),
		};

		// workspace_available is used to make sure that nothing is still using members of the workspace
		// when it is recycled for use in a new `render` call
		VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, render_params.workspace_available));
	}

}

void Tutorial::update(float dt) {
	// modify time in every update
	time = std::fmod(time + dt, 60.0f);

	if (camera_mode == CameraMode::Scene) {
		//  1. Get the currently-selected scene camera (by scene_camera_index).
		auto& sc = scene_cameras[scene_camera_index];
		//  2. Get the camera's S72::Camera::Perspective params (vfov, aspect, near, far).
		auto& persp = std::get<S72::Camera::Perspective>(sc.camera->projection);
		//  3. Get the camera node's accumulated world transform from the scene graph.
		//  4. Invert the world transform to get WORLD_FROM_CAMERA -> CAMERA_FROM_WORLD (view matrix).
		//  5. CLIP_FROM_WORLD = perspective(...) * CAMERA_FROM_WORLD.
		CLIP_FROM_WORLD = perspective(
			persp.vfov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),	// aspect of the actual window
			persp.near,
			persp.far
		) * mat4_inverse(sc.WORLD_FROM_CAMERA);
		//  Note: the user cannot move this camera; they can only cycle to the next one (handled in on_input).

		// placeholder: orbiting camera (replace with scene camera logic)
		// float ang = float(M_PI) * 2.0f * 10.0f * (time / 60.0f);
		// CLIP_FROM_WORLD = perspective(
		// 	60.0f * float(M_PI) /180.0f,	// vfov
		// 	rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),	// aspect
		// 	0.1f,	// near
		// 	1000.0f	// far
		// ) * look_at(
		// 	3.0f * std::cos(ang), 3.0f * std::sin(ang), 1.0f, 	//eye
		// 	0.0f, 0.0f, 0.5f, 	// target
		// 	0.0f, 0.0f, 1.0f	// up
		// );
	} else if (camera_mode == CameraMode::User) {
		CLIP_FROM_WORLD = perspective(
			free_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),	// aspect
			free_camera.near,
			free_camera.far
		) * orbit(
			free_camera.target_x, free_camera.target_y, free_camera.target_z,
			free_camera.azimuth, free_camera.elevation, free_camera.radius
		);
	} else if (camera_mode == CameraMode::Debug) {
		//  1. Compute CLIP_FROM_WORLD using the debug_camera's orbit params (same as User mode
		//     but from the debug_camera struct). This is the matrix used for *rendering*.
		//  2. Keep a separate mat4 (e.g., culling_CLIP_FROM_WORLD) that holds the CLIP_FROM_WORLD
		//     of whichever camera was active *before* entering debug mode. Use that for *culling*.
		CLIP_FROM_WORLD = perspective(
			debug_camera.fov,
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height),	// aspect
			debug_camera.near,
			debug_camera.far
		) * orbit(
			debug_camera.target_x, debug_camera.target_y, debug_camera.target_z,
			debug_camera.azimuth, debug_camera.elevation, debug_camera.radius
		);

		//  3. Draw debug visualizations using the lines pipeline:
		//     a. Draw the frustum of the culling camera as lines (extract the 8 frustum corners
		//        from the inverse of culling_CLIP_FROM_WORLD and connect them with 12 edges).
		//     b. Draw axis-aligned bounding boxes (AABBs) for each object instance as lines
		//        (12 edges per box).
	} else {
		assert(0 && "unknown camera mode");
	}

	{	// static sun and sky:
		world.SKY_DIRECTION.x = 0.0f;
		world.SKY_DIRECTION.y = 0.0f;
		world.SKY_DIRECTION.z = 1.0f;

		world.SKY_ENERGY.r = 0.1f;
		world.SKY_ENERGY.g = 0.1f;
		world.SKY_ENERGY.b = 0.2f;

		world.SUN_DIRECTION.x = 6.0f / 23.0f;
		world.SUN_DIRECTION.y = 13.0f / 23.0f;
		world.SUN_DIRECTION.z = 18.0f / 23.0f;

		world.SUN_ENERGY.r = 1.0f;
		world.SUN_ENERGY.g = 1.0f;
		world.SUN_ENERGY.b = 0.9f;
	}

	{	// day/night cycle sun and sky:
		// // 1. Define cycle parameters
		// const float dayDuration = 60.0f; // Full day/night cycle in seconds
		// float sunAngle = (time / dayDuration) * 2.0f * 3.14159f;

		// // 2. Calculate Sun Direction (Rotating around the X or Y axis)
		// // Here we rotate in the YZ plane to simulate the sun rising and setting
		// world.SUN_DIRECTION.x = 0.0f;
		// world.SUN_DIRECTION.y = sin(sunAngle); 
		// world.SUN_DIRECTION.z = cos(sunAngle); // Height in the sky

		// // 3. Calculate Sun Intensity/Color based on height (z-component)
		// float sunHeight = world.SUN_DIRECTION.z;
		
		// if (sunHeight > 0.0f) {
		// 	// Daytime: Sun is above the horizon
		// 	// Use smoothstep or clamp to transition colors during sunrise/sunset
		// 	float intensity = sunHeight > 0.0f ? sunHeight : 0.0f;
			
		// 	world.SUN_ENERGY.r = 1.0f;
		// 	world.SUN_ENERGY.g = 0.8f + (0.2f * intensity); // Whiter at noon, yellower at sunset
		// 	world.SUN_ENERGY.b = 0.5f + (0.4f * intensity);
			
		// 	// Sky gets brighter during the day
		// 	world.SKY_ENERGY.r = 0.1f * intensity;
		// 	world.SKY_ENERGY.g = 0.2f * intensity;
		// 	world.SKY_ENERGY.b = 0.5f * intensity;
		// } else {
		// 	// Nighttime: Sun is below the horizon
		// 	world.SUN_ENERGY.r = 0.0f;
		// 	world.SUN_ENERGY.g = 0.0f;
		// 	world.SUN_ENERGY.b = 0.0f;

		// 	// Dim ambient moon/star light for the sky
		// 	world.SKY_ENERGY.r = 0.01f;
		// 	world.SKY_ENERGY.g = 0.01f;
		// 	world.SKY_ENERGY.b = 0.03f;
		// }

		// // Sky direction stays constant (up) or can subtly shift
		// world.SKY_DIRECTION.x = 0.0f;
		// world.SKY_DIRECTION.y = 0.0f;
		// world.SKY_DIRECTION.z = 1.0f;
	}

	{	// draw a cube with LinesPipeline
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
	}	// end of cube

	{	// make some objects:
		object_instances.clear();

		if (scene_vertices.handle != VK_NULL_HANDLE)
		{	// scene loaded: create instances from scene meshes
			
			// traverse scene graph to compute proper world transforms and push object instances
			for (auto& root : scene_S72.scene.roots)
			{
				traverse_node(root, mat4_identity());
			}
		}
		else
		{	// no scene: use hardcoded plane and torus
			{	// plane translated +x by one unit:
				mat4 WORLD_FROM_LOCAL {
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					1.0f, 0.0f, 0.0f, 1.0f,
				};

				object_instances.emplace_back(ObjectInstance{
					.vertices = plane_vertices,
					.transform{
						.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
					},
					.texture = 1,
				});
			}	// end of plane translation
			{	// torus translated -x by one unit and rotated CCW around +y:
				float ang = time / 60.0f * 2.0f * float(M_PI) * 10.0f;
				float ca = std::cos(ang);
				float sa = std::sin(ang);
				mat4 WORLD_FROM_LOCAL{
					ca, 0.0f,  -sa, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					  sa, 0.0f,   ca, 0.0f,
					-1.0f,0.0f, 0.0f, 1.0f,
				};

				object_instances.emplace_back(ObjectInstance{
					.vertices = torus_vertices,
					.transform{
						.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
					},
				});
			}	// end of torus translation and rotation
		}

	}	// end of make some objects
}	// end of update


void Tutorial::on_input(InputEvent const &evt) {
	// if there ia a current action, it gets input priority:
	if (action) {
		action(evt);
		return;
	}
	
	// A1: general controls
	if (evt.type == InputEvent::KeyDown && evt.key.key == GLFW_KEY_TAB) {
		// cycle camera modes: Scene -> User -> Debug -> Scene -> ...
		// TODO: when switching INTO Debug mode, snapshot the current CLIP_FROM_WORLD
		//       into culling_CLIP_FROM_WORLD so culling stays locked to the previous camera.
		// TODO: when switching OUT OF Debug mode, clear any debug line visualizations.
		CameraMode next = CameraMode((int(camera_mode) + 1) % 3);
		if (next == CameraMode::Debug)
		{
			CULLING_CLIP_FROM_WORLD = CLIP_FROM_WORLD;
		}
		camera_mode = next;

		// skip scene mode if no scene file is loaded or no camera is found in the scene
		if (camera_mode == CameraMode::Scene && (rtg.configuration.scene_file.empty() || scene_cameras.empty()))
		{
			std::cout << "[Tutorial.cpp]: Trying to switch the camera to scene mode while no scene file was loaded or no camera was found. Switching to the next state." << std::endl;
			camera_mode = CameraMode((int(camera_mode) + 1) % 3);
		}
		return;
	}

	//  A1: handling scene camera inputs
	if (camera_mode == CameraMode::Scene)
	{
		//  When camera_mode == Scene, handle a key (e.g., left/right arrow, or a specific key)
		//  to cycle scene_camera_index through the list of scene cameras.
		//  No mouse controls — the scene camera transform is fixed by the scene graph.
		if (evt.type == InputEvent::KeyDown)
		{
			if (evt.key.key == GLFW_KEY_RIGHT)
			{
				scene_camera_index = (scene_camera_index + 1) % uint32_t(scene_cameras.size());
			}
			else if (evt.key.key == GLFW_KEY_LEFT)
			{
				scene_camera_index = (scene_camera_index + scene_cameras.size() - 1) % uint32_t(scene_cameras.size());
			}
		}
	}	// end of scene camera inputs handling

	// A1: debug camera debug line toggles
	if (camera_mode == CameraMode::Debug)
	{
		if (evt.type == InputEvent::KeyDown && (evt.button.button == GLFW_KEY_RIGHT || evt.button.button == GLFW_KEY_LEFT))
		{
			is_showing_debug_lines = !is_showing_debug_lines;
		}
	}	// end of debug camera input handling

	// User camera controls (also used for Debug camera):
	if (camera_mode == CameraMode::User || camera_mode == CameraMode::Debug) {
		// select which camera to modify based on current mode:
		OrbitCamera &active_cam = (camera_mode == CameraMode::User) ? free_camera : debug_camera;
		
		if (evt.type == InputEvent::MouseWheel) {
			// change distance by 10% every scroll click:
			active_cam.radius *= std::exp(std::log(1.1f) * -evt.wheel.y);
			// make sure camera isn't too close or too far from target:
			active_cam.radius = std::max(active_cam.radius, 0.5f * active_cam.near);
			active_cam.radius = std::min(active_cam.radius, 2.0f * active_cam.far);
			return;
		}
		
		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT && (evt.button.mods & GLFW_MOD_SHIFT)) {
			// start panning
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = active_cam;
			// capture which camera mode we're in so the lambda modifies the right camera,
			// even if the mode changes while dragging:
			CameraMode captured_mode = camera_mode;

			action = [this, init_x, init_y, init_camera, captured_mode] (InputEvent const &evt) {
				OrbitCamera &cam = (captured_mode == CameraMode::User) ? free_camera : debug_camera;
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel upon button lifted:
					action = nullptr;
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					// image height at plane of target point:
					float height = 2.0f * std::tan(cam.fov * 0.5f) * cam.radius;

					// motion, therefore, at target point:
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height * height;
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height * height;	// note: negated because glfw uses y-down coordinate system

					// compute camera transform to extract right (first row) and up (second row):
					mat4 camera_from_world = orbit(
						init_camera.target_x, init_camera.target_y, init_camera.target_z,
						init_camera.azimuth, init_camera.elevation, init_camera.radius
					);

					// move the desired distance:
					cam.target_x = init_camera.target_x - dx * camera_from_world[0] - dy * camera_from_world[1];
					cam.target_y = init_camera.target_y - dx * camera_from_world[4] - dy * camera_from_world[5];
					cam.target_z = init_camera.target_z - dx * camera_from_world[8] - dy * camera_from_world[9];
				
					return;
				}
			};

			return;
		}

		if (evt.type == InputEvent::MouseButtonDown && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
			// start tumbling
			float init_x = evt.button.x;
			float init_y = evt.button.y;
			OrbitCamera init_camera = active_cam;
			CameraMode captured_mode = camera_mode;

			action = [this, init_x, init_y, init_camera, captured_mode](InputEvent const &evt) {
				OrbitCamera &cam = (captured_mode == CameraMode::User) ? free_camera : debug_camera;
				if (evt.type == InputEvent::MouseButtonUp && evt.button.button == GLFW_MOUSE_BUTTON_LEFT) {
					// cancel upon button lifted:
					action = nullptr;
					return;
				}
				if (evt.type == InputEvent::MouseMotion) {
					// motion, normalized so 1.0 is window height:
					float dx = (evt.motion.x - init_x) / rtg.swapchain_extent.height;
					float dy = -(evt.motion.y - init_y) / rtg.swapchain_extent.height;	// note: negated because glfw uses y-down coordinate system

					// rotate camera based on motion:
					float speed = float(M_PI);	// how much rotation happens at one full window height
					float flip_x = (std::abs(init_camera.elevation) > 0.5f * float(M_PI) ? -1.0f : 1.0f);	// switch azimuth rotation when camera is upside-down
					cam.azimuth = init_camera.azimuth - dx * speed * flip_x;
					cam.elevation = init_camera.elevation - dy * speed;

					// reduce azimuth and elevation to [-pi, pi] range
					const float twopi = 2.0f * float(M_PI);
					cam.azimuth -= std::round(cam.azimuth / twopi) * twopi;
					cam.elevation -= std::round(cam.elevation / twopi) * twopi;

					return;
				}
			};

			return;
		}
	}
}
