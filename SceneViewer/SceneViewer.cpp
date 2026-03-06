#pragma once

#include "SceneViewer.hpp"
#include "Tutorial.hpp"
#include "print_scene.hpp"	// includes S72.hpp

#include <cassert>
#include <iostream>
#include <fstream>

SceneViewer::SceneViewer(Tutorial &t) : tutorial(t) {}

SceneViewer::~SceneViewer() = default;

void SceneViewer::load_scene()
{
	try {
		scene_S72 = S72::load(tutorial.rtg.configuration.scene_viewer_config.scene_file.data());
		if (tutorial.rtg.configuration.scene_viewer_config.print_scene)
		{
			print_info(scene_S72);
			print_scene_graph(scene_S72);
		}
	} catch (std::exception &e) {
		std::cerr << "Failed to load s72-format scene from " << tutorial.rtg.configuration.scene_viewer_config.scene_file << "\n" << e.what() << std::endl;
	}
}	// end of load scene

void SceneViewer::load_scene_binaries()
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
}	// end of load scene binaries

void SceneViewer::build_scene_meshes()
{
	// Per A1 spec:
	//  - No indices (non-indexed TRIANGLE_LIST only)
	//  - All attributes interleaved at stride 48:
	//      POSITION  offset  0  R32G32B32_SFLOAT    (12 bytes)
	//      NORMAL    offset 12  R32G32B32_SFLOAT    (12 bytes)
	//      TANGENT   offset 24  R32G32B32A32_SFLOAT (16 bytes, skipped for now)
	//      TEXCOORD  offset 40  R32G32_SFLOAT       ( 8 bytes)
	//  - Materials are always lambertian
	
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
		scene_vertices = tutorial.rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);
		tutorial.rtg.helpers.transfer_to_buffer(all_vertices.data(), bytes, scene_vertices);

		std::cout << "[Tutorial.cpp]: Uploaded " << all_vertices.size() << " scene vertices ("
					<< bytes << " bytes) to GPU." << std::endl;
	}
}	// end of build scene meshes

void SceneViewer::traverse_node(S72::Node *node, mat4 parent_transform)
{
    // node's local transform = T * R * S
	mat4 local_transform = mat4_translation(node->translation.x, node->translation.y, node->translation.z)
						 * mat4_rotation(node->rotation.x, node->rotation.y, node->rotation.z, node->rotation.w)
						 * mat4_scale(node->scale.x, node->scale.y, node->scale.z);

	// Accumulate with parent transform
	mat4 WORLD_FROM_LOCAL = parent_transform * local_transform;

	//	If this node has a mesh, emit an ObjectInstance:
	if (node->mesh != nullptr)
	{
		// look up the mesh by name
		auto it = scene_meshes.find(node->mesh->name);
		if (it != scene_meshes.end())	// found a mesh
		{	
			if (culling_mode == CullingMode::None)
			{
				// push ObjectInstance to object_instances
				// WORLD_FROM_LOCAL_NORMAL = inverse transpose of WORLD_FROM_LOCAL when non-uniform scale is present
				mat4 WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL;
				tutorial.object_instances.emplace_back(Tutorial::ObjectInstance{
					.vertices = it->second.vertices,
					.transform {
						.CLIP_FROM_LOCAL = tutorial.CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL_NORMAL = mat4_inverse_transpose(WORLD_FROM_LOCAL_NORMAL),
					},
					// TODO: texture
					
				});
			}
			else if (culling_mode == CullingMode::Frustum)
			{
				// transform the 8 corners by WORLD_FROM_LOCAL to get world space obb
				WorldBounds bounds = get_world_bounds(it->second, WORLD_FROM_LOCAL);

				// compare against frustum planes
				if (is_inside_frustum(bounds))
				{
					// push ObjectInstance to object_instances
					// WORLD_FROM_LOCAL_NORMAL = inverse transpose of WORLD_FROM_LOCAL when non-uniform scale is present
					mat4 WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL;
					tutorial.object_instances.emplace_back(Tutorial::ObjectInstance{
						.vertices = it->second.vertices,
						.transform {
							.CLIP_FROM_LOCAL = tutorial.CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL_NORMAL = mat4_inverse_transpose(WORLD_FROM_LOCAL_NORMAL),
						},
						// TODO: texture

					});

					// push WorldBounds to object_bounds
					object_bounds.push_back(bounds);

					assert(tutorial.object_instances.size() == object_bounds.size() && "Size mismatch between object instances and bounds.");
				}
			}
			else
			{
				std::cerr << "[Tutorial.cpp]: traversing the scene graph with unknown culling mode, exiting." << std::endl;
				std::exit(1);
			}
		}	// end of mesh found in scene_meshes
	}

	//	Recurse into children, passing WORLD_FROM_LOCAL as their parent_transform
	for (auto& child_node : node->children)
	{
		traverse_node(child_node, WORLD_FROM_LOCAL);
	}
}   // end of traverse_node

void SceneViewer::collect_cameras(S72::Node *node, mat4 parent_transform)
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
}   // end of collect_cameras

void SceneViewer::initialize_scene_cameras()
{
	// looking for scene cameras
	for (auto &root : scene_S72.scene.roots)
	{
		collect_cameras(root, mat4_identity());
	}
	std::cout << "[Tutorial.cpp]: Collected " << scene_cameras.size() << " scene cameras." << std::endl;

	// a scene camera is specified
	if (!tutorial.rtg.configuration.scene_viewer_config.scene_camera.empty())
	{
		for (uint32_t i = 0; i < scene_cameras.size(); ++i) {
			if (scene_cameras[i].camera->name == tutorial.rtg.configuration.scene_viewer_config.scene_camera) {
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
	else	// a scene camera is not specified
	{
		// no scene camera was found
		if (scene_cameras.size() == 0)
		{
			std::cout << "[Tutorial.cpp]: Found no scene camera within the specified scene file." << std::endl;
		}
		else	// default to the first scene camera
		{
			scene_camera_index = 0;
		}
	}
}	// end of build scene cameras

void SceneViewer::initialize_cull_mode()
{
	if (!tutorial.rtg.configuration.scene_viewer_config.culling_mode.empty())
	{
		if (tutorial.rtg.configuration.scene_viewer_config.culling_mode == "none")
		{
			culling_mode = CullingMode::None;
		}
		else if (tutorial.rtg.configuration.scene_viewer_config.culling_mode == "frustum")
		{
			culling_mode = CullingMode::Frustum;
		}
		else
		{
			std::cerr << "[Tutorial.cpp]: an unknown culling mode is specified, exiting." << std::endl;
			std::exit(1);
		}
	}

	std::cout << "[Tutorial.cpp]: using culling mode: " << int(culling_mode) << std::endl;
}	// end of initialize cull mode

void SceneViewer::compute_frustum_planes()
{
	// https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
		
	// left
	frustum_planes[0][0] = CULLING_CLIP_FROM_WORLD[ 3] + CULLING_CLIP_FROM_WORLD[ 0];
	frustum_planes[0][1] = CULLING_CLIP_FROM_WORLD[ 7] + CULLING_CLIP_FROM_WORLD[ 4];
	frustum_planes[0][2] = CULLING_CLIP_FROM_WORLD[11] + CULLING_CLIP_FROM_WORLD[ 8];
	frustum_planes[0][3] = CULLING_CLIP_FROM_WORLD[15] + CULLING_CLIP_FROM_WORLD[12];

	// right
	frustum_planes[1][0] = CULLING_CLIP_FROM_WORLD[ 3] - CULLING_CLIP_FROM_WORLD[ 0];
	frustum_planes[1][1] = CULLING_CLIP_FROM_WORLD[ 7] - CULLING_CLIP_FROM_WORLD[ 4];
	frustum_planes[1][2] = CULLING_CLIP_FROM_WORLD[11] - CULLING_CLIP_FROM_WORLD[ 8];
	frustum_planes[1][3] = CULLING_CLIP_FROM_WORLD[15] - CULLING_CLIP_FROM_WORLD[12];

	// buttom
	frustum_planes[2][0] = CULLING_CLIP_FROM_WORLD[ 3] + CULLING_CLIP_FROM_WORLD[ 1];
	frustum_planes[2][1] = CULLING_CLIP_FROM_WORLD[ 7] + CULLING_CLIP_FROM_WORLD[ 5];
	frustum_planes[2][2] = CULLING_CLIP_FROM_WORLD[11] + CULLING_CLIP_FROM_WORLD[ 9];
	frustum_planes[2][3] = CULLING_CLIP_FROM_WORLD[15] + CULLING_CLIP_FROM_WORLD[13];

	// top
	frustum_planes[3][0] = CULLING_CLIP_FROM_WORLD[ 3] - CULLING_CLIP_FROM_WORLD[ 1];
	frustum_planes[3][1] = CULLING_CLIP_FROM_WORLD[ 7] - CULLING_CLIP_FROM_WORLD[ 5];
	frustum_planes[3][2] = CULLING_CLIP_FROM_WORLD[11] - CULLING_CLIP_FROM_WORLD[ 9];
	frustum_planes[3][3] = CULLING_CLIP_FROM_WORLD[15] - CULLING_CLIP_FROM_WORLD[13];

	// near
	frustum_planes[4][0] = CULLING_CLIP_FROM_WORLD[ 2];
	frustum_planes[4][1] = CULLING_CLIP_FROM_WORLD[ 6];
	frustum_planes[4][2] = CULLING_CLIP_FROM_WORLD[10];
	frustum_planes[4][3] = CULLING_CLIP_FROM_WORLD[14];

	// far
	frustum_planes[5][0] = CULLING_CLIP_FROM_WORLD[ 3] - CULLING_CLIP_FROM_WORLD[ 2];
	frustum_planes[5][1] = CULLING_CLIP_FROM_WORLD[ 7] - CULLING_CLIP_FROM_WORLD[ 6];
	frustum_planes[5][2] = CULLING_CLIP_FROM_WORLD[11] - CULLING_CLIP_FROM_WORLD[10];
	frustum_planes[5][3] = CULLING_CLIP_FROM_WORLD[15] - CULLING_CLIP_FROM_WORLD[14];

}	// end of update frustum planes

void SceneViewer::draw_camera_frustum()
{
	mat4 inv = mat4_inverse(CULLING_CLIP_FROM_WORLD);
	// the 8 corners of the Normalized Device Coordinates (NDC) cube in clip space (Vulkan: x,y in [-1,1], z in [0,1])
	float ndc[8][4] = {
		{-1, -1, 0, 1}, { 1, -1, 0, 1}, {-1,  1, 0, 1}, { 1,  1, 0, 1},  // near
		{-1, -1, 1, 1}, { 1, -1, 1, 1}, {-1,  1, 1, 1}, { 1,  1, 1, 1},  // far
	};
	float fc[8][3]; // frustum corners in world space
	for (int i = 0; i < 8; ++i) {
		vec4 clip = {ndc[i][0], ndc[i][1], ndc[i][2], ndc[i][3]};
		vec4 w = inv * clip;
		fc[i][0] = w[0] / w[3];
		fc[i][1] = w[1] / w[3];
		fc[i][2] = w[2] / w[3];
	}

	// 12 edges of the frustum (same corner ordering as ndc above)
	// near face: 0-1, 2-3, 0-2, 1-3
	// far face:  4-5, 6-7, 4-6, 5-7
	// connecting: 0-4, 1-5, 2-6, 3-7
	int edges[12][2] = {
		{0,1},{2,3},{0,2},{1,3},
		{4,5},{6,7},{4,6},{5,7},
		{0,4},{1,5},{2,6},{3,7},
	};
	for (int e = 0; e < 12; ++e) {
		int a = edges[e][0], b = edges[e][1];
		tutorial.lines_vertices.emplace_back(PosColVertex{
			.Position{.x = fc[a][0], .y = fc[a][1], .z = fc[a][2]},
			.Color{.r = 0xff, .g = 0x00, .b = 0xff, .a = 0xff},	// blue
		});
		tutorial.lines_vertices.emplace_back(PosColVertex{
			.Position{.x = fc[b][0], .y = fc[b][1], .z = fc[b][2]},
			.Color{.r = 0xff, .g = 0x00, .b = 0xff, .a = 0xff},	// blue
		});
	}
}	// end of draw camera frustum

SceneViewer::WorldBounds SceneViewer::get_world_bounds(SceneViewer::SceneMesh const &mesh, mat4 const &world_from_local)
{
    WorldBounds bounds;
	uint8_t index = 0;
	for (uint8_t iz = 0; iz < 2; iz++)
	{
		for (uint8_t iy = 0; iy < 2; iy++)
		{
			for (uint8_t ix = 0; ix < 2; ix++)
			{
				vec4 local = {
					ix ? mesh.max_x : mesh.min_x,
					iy ? mesh.max_y : mesh.min_y,
					iz ? mesh.max_z : mesh.min_z,
					1.0f
				};

				// transform
				vec4 world_trans = world_from_local * local;

				// world space obb
				bounds.corners[index][0] = world_trans[0];
				bounds.corners[index][1] = world_trans[1];
				bounds.corners[index][2] = world_trans[2];
				
				// world space aabb
				bounds.min_x = std::min(bounds.min_x, world_trans[0]);
				bounds.min_y = std::min(bounds.min_y, world_trans[1]);
				bounds.min_z = std::min(bounds.min_z, world_trans[2]);
				bounds.max_x = std::max(bounds.max_x, world_trans[0]);
				bounds.max_y = std::max(bounds.max_y, world_trans[1]);
				bounds.max_z = std::max(bounds.max_z, world_trans[2]);

				++index;
			}
		}
	}

	return bounds;
}   // end of get_world_bounds

bool SceneViewer::is_inside_frustum(SceneViewer::WorldBounds &bounds)
{
    for (uint8_t i = 0; i < 6; i++)
	{
		float a = frustum_planes[i][0],
			  b = frustum_planes[i][1],
			  c = frustum_planes[i][2],
			  d = frustum_planes[i][3];

		// test 8 OBB corners against each of the 6 planes
		if (bv_mode == BoundingVolumeMode::OBB)
		{
			// cull if all 8 corners are outside of this plane
			bool all_outside = true;
			for (uint8_t j = 0; j < 8; j++)
			{
				float dist = a * bounds.corners[j][0]
						   + b * bounds.corners[j][1]
						   + c * bounds.corners[j][2]
						   + d;
				if (dist >= 0.0f) {
					// at least one corner is inside this plane
					all_outside = false;
					break;
				}
			}
			if (all_outside) return false;
		}
		else if (bv_mode == BoundingVolumeMode::AABB)
		{
			// p-vertex test: pick the corner most in the direction of the plane normal.
			// If even that corner is outside, the whole box is outside.
			float px = (a >= 0.0f) ? bounds.max_x : bounds.min_x;
			float py = (b >= 0.0f) ? bounds.max_y : bounds.min_y;
			float pz = (c >= 0.0f) ? bounds.max_z : bounds.min_z;

			if (a * px + b * py + c * pz + d < 0.0f) return false;
		}
		else
		{
			std::cout << "[Tutorial.cpp]: bounding volume with unknown type is presented. Returning from is_inside_frustum" << std::endl;
			return false;
		}
	}

	return true;
}   // end of is_inside_frustum

void SceneViewer::draw_bounds(const SceneViewer::WorldBounds &bounds)
{
    if (bv_mode == BoundingVolumeMode::OBB)
	{
		// 12 edges using the same corner ordering as get_world_bounds:
		// ix varies fastest: 0=min,1=max
		// (0,1),(2,3),(4,5),(6,7) - x edges
		// (0,2),(1,3),(4,6),(5,7) - y edges
		// (0,4),(1,5),(2,6),(3,7) - z edges
		int edges[12][2] = {
			{0,1},{2,3},{4,5},{6,7},
			{0,2},{1,3},{4,6},{5,7},
			{0,4},{1,5},{2,6},{3,7},
		};
		for (int e = 0; e < 12; ++e) {
			int a = edges[e][0], b = edges[e][1];
			tutorial.lines_vertices.emplace_back(PosColVertex{
				.Position{.x = bounds.corners[a][0], .y = bounds.corners[a][1], .z = bounds.corners[a][2]},
				.Color{.r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},	// red
			});
			tutorial.lines_vertices.emplace_back(PosColVertex{
				.Position{.x = bounds.corners[b][0], .y = bounds.corners[b][1], .z = bounds.corners[b][2]},
				.Color{.r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},	// red
			});
		}
	}	// end of OBB case
	else if (bv_mode == BoundingVolumeMode::AABB)
	{
		// 8 corners of the world-space AABB
		float c[8][3] = {
			{bounds.min_x, bounds.min_y, bounds.min_z},
			{bounds.max_x, bounds.min_y, bounds.min_z},
			{bounds.min_x, bounds.max_y, bounds.min_z},
			{bounds.max_x, bounds.max_y, bounds.min_z},
			{bounds.min_x, bounds.min_y, bounds.max_z},
			{bounds.max_x, bounds.min_y, bounds.max_z},
			{bounds.min_x, bounds.max_y, bounds.max_z},
			{bounds.max_x, bounds.max_y, bounds.max_z},
		};
		int edges[12][2] = {
			{0,1},{2,3},{4,5},{6,7},
			{0,2},{1,3},{4,6},{5,7},
			{0,4},{1,5},{2,6},{3,7},
		};
		for (int e = 0; e < 12; ++e) {
			int a = edges[e][0], b = edges[e][1];
			tutorial.lines_vertices.emplace_back(PosColVertex{
				.Position{.x = c[a][0], .y = c[a][1], .z = c[a][2]},
				.Color{.r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff},	// green
			});
			tutorial.lines_vertices.emplace_back(PosColVertex{
				.Position{.x = c[b][0], .y = c[b][1], .z = c[b][2]},
				.Color{.r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff},	// green
			});
		}
	}	// end of AABB case
	else
	{
		std::cout << "[Tutorial.cpp]: bounding volume with unknown type is presented. Returning from draw_bounds" << std::endl;
		return;
	}
}   // end of draw bounds
