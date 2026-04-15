#include <cassert>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <memory>

#include "Tutorial.hpp"
#include "print_scene.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "SceneViewer/stb_image.h"

void Tutorial::load_scene()
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
}	// end of load scene

void Tutorial::load_scene_binaries()
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

uint32_t Tutorial::color_to_hex(S72::color const *col)
{
	uint32_t r = static_cast<uint32_t>(std::round(std::clamp(col->r, 0.0f, 1.0f) * 255.0f));
	uint32_t g = static_cast<uint32_t>(std::round(std::clamp(col->g, 0.0f, 1.0f) * 255.0f));
	uint32_t b = static_cast<uint32_t>(std::round(std::clamp(col->b, 0.0f, 1.0f) * 255.0f));
	uint32_t a = 255;

	// 0xAABBGGRR
	return (a << 24) | (b << 16) | (g << 8) | r;

}	// end of color to hex

void Tutorial::build_scene_materials()
{
	// initialize mat-tex look-up table
	mat_to_tex.clear();

	std::cout << "[SceneViewer.cpp]: Number of materials in the scene: " << scene_S72.materials.size() << std::endl;
	std::cout << "[SceneViewer.cpp]: current textures size: " << textures.size() << std::endl;
	std::cout << "[SceneViewer.cpp]: Current mat_to_tex size: " << mat_to_tex.size() << std::endl;
	
	// for (auto it : scene_S72.materials)
	// Must iterate by reference: `&it.second` must point at Materials *in* the map. With
	// `for (auto it : ...)`, `it` is a copy each iteration, so `&it.second` is the same
	// stack address every time and mat_to_tex would only keep one (wrong) entry.
	for (auto const &it : scene_S72.materials)
	{
		if (std::holds_alternative<S72::Material::PBR>(it.second.brdf))
		{
			
		}
		else if (std::holds_alternative<S72::Material::Lambertian>(it.second.brdf))
		{
			std::cout << "[SceneViewer.cpp]: Building a Lambertian ";
			
			auto const &lamb = std::get<S72::Material::Lambertian>(it.second.brdf);
			if (std::holds_alternative<S72::color>(lamb.albedo))	// solid color albedo
			{	
				auto const &col = std::get<S72::color>(lamb.albedo);

				std::cout << "solid color albedo of color " << it.second.name << std::endl;

				// make the albedo texture
				uint8_t size = 1;
				std::vector<uint32_t> data{};
				data.emplace_back(color_to_hex(&col));

				// make a place for the texture to live on the GPU
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{ .width = size, .height = size },	// size of image
					VK_FORMAT_R8G8B8A8_UNORM,	// how to interpret image data
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,	// will sample and upload
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device local
					Helpers::Unmapped
				));

				// save to the table to be looked up
				mat_to_tex.emplace(&it.second, uint32_t(textures.size() - 1));

				std::cout << "[SceneViewer.cpp]: current textures size: " << textures.size() << std::endl;
				std::cout << "[SceneViewer.cpp]: Current mat_to_tex size: " << mat_to_tex.size() << std::endl;

				// transfer data
				rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
			}
			else if (std::holds_alternative<S72::Texture *>(lamb.albedo))
			{
				std::cout << "texture albedo" << std::endl;

				// Albedo is an image: lamb.albedo holds S72::Texture* (not S72::Texture by value)
				S72::Texture *tex = std::get<S72::Texture *>(lamb.albedo);
				if (tex == nullptr)
				{
					std::cerr << "[SceneViewer.cpp]: Lambertian texture albedo has null Texture*.\n";
					continue;
				}
				if (tex->type != S72::Texture::Type::flat)
				{
					std::cerr << "[SceneViewer.cpp]: Lambertian albedo expects a flat 2D texture; got non-flat: "
						<< tex->src << "\n";
					continue;
				}
				if (tex->format == S72::Texture::Format::rgbe)
				{
					std::cerr << "[SceneViewer.cpp]: Lambertian texture albedo RGBE/HDR not supported yet: "
						<< tex->src << "\n";
					continue;
				}

				int w = 0, h = 0;
				std::unique_ptr<unsigned char, void (*)(void *)> pixels(
					stbi_load(tex->path.c_str(), &w, &h, nullptr, 4),
					[](void *p) { stbi_image_free(p); }
				);
				if (!pixels)
				{
					std::cerr << "[SceneViewer.cpp]: Failed to load texture \"" << tex->path
						<< "\": " << stbi_failure_reason() << "\n";
					continue;
				}
				if (w <= 0 || h <= 0)
				{
					std::cerr << "[SceneViewer.cpp]: Invalid texture dimensions for \"" << tex->path << "\".\n";
					continue;
				}

				VkFormat vk_format = (tex->format == S72::Texture::Format::srgb)
					? VK_FORMAT_R8G8B8A8_SRGB
					: VK_FORMAT_R8G8B8A8_UNORM;

				size_t byte_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;

				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{ .width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h) },
					vk_format,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					Helpers::Unmapped
				));

				mat_to_tex[&it.second] = uint32_t(textures.size() - 1);

				rtg.helpers.transfer_to_image(pixels.get(), byte_size, textures.back());
			}
			else
			{
				std::cout << "unknown albedo" << std::endl;
				std::cerr << "[SceneViewer.cpp]: Trying to build a Lambertian material from an unknown source." << std::endl;
				continue;
			}

		}
		else if (std::holds_alternative<S72::Material::Mirror>(it.second.brdf))
		{
			// A2-env
			mat_to_tex[&it.second] = UINT32_MAX;	// sentinel: no 2D texture
		}
		else if (std::holds_alternative<S72::Material::Environment>(it.second.brdf))
		{
			// A2-env
			mat_to_tex[&it.second] = UINT32_MAX;	// sentinel: no 2D texture
		}
		else
		{
			std::cerr << "[SceneViewer.cpp]: Trying to build a material of an unknown type." << std::endl;
			continue;
		}

	}	// end of for loop

	std::cout << "[SceneViewer.cpp]: mat_to_tex size: " << mat_to_tex.size() << std::endl;

}	// end of build scene materials

void Tutorial::traverse_node(S72::Node *node, mat4 parent_transform)
{
	//	node's local transform = T * R * S
	mat4 local_transform = mat4_translation(node->translation.x, node->translation.y, node->translation.z)
						 * mat4_rotation(node->rotation.x, node->rotation.y, node->rotation.z, node->rotation.w)
						 * mat4_scale(node->scale.x, node->scale.y, node->scale.z);

	//	Accumulate with parent transform
	mat4 WORLD_FROM_LOCAL = parent_transform * local_transform;

	//	If this node has a mesh, emit an ObjectInstance:
	if (node->mesh != nullptr)
	{
		// look up the mesh by name
		auto it = scene_meshes.find(node->mesh->name);
		if (it != scene_meshes.end())	// found a mesh
		{	
			uint32_t tex = 0;
			MaterialType mat_type = MaterialType::Lambertian;
			if (const auto *mat = it->second.material)
			{
				auto itt = mat_to_tex.find(mat);
				if (itt != mat_to_tex.end() && itt->second != UINT32_MAX)
				{
					tex = itt->second;
				}

				if (std::holds_alternative<S72::Material::Mirror>(mat->brdf))
					mat_type = MaterialType::Mirror;
				else if (std::holds_alternative<S72::Material::Environment>(mat->brdf))
					mat_type = MaterialType::Environment;
				else if (std::holds_alternative<S72::Material::PBR>(mat->brdf))
					mat_type = MaterialType::PBR;
			}

			mat4 WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL;

			auto make_instance = [&]() -> ObjectInstance {
				return ObjectInstance{
					.vertices = it->second.vertices,
					.transform {
						.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL_NORMAL = mat4_inverse_transpose(WORLD_FROM_LOCAL_NORMAL),
					},
					.texture = tex,
					.material_type = mat_type,
				};
			};

			if (culling_mode == CullingMode::None)
			{
				object_instances.emplace_back(make_instance());
			}
			else if (culling_mode == CullingMode::Frustum)
			{
				WorldBounds bounds = get_world_bounds(it->second, WORLD_FROM_LOCAL);

				if (is_inside_frustum(bounds))
				{
					object_instances.emplace_back(make_instance());
					object_bounds.push_back(bounds);
					assert(object_instances.size() == object_bounds.size() && "Size mismatch between object instances and bounds.");
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

}	// end of traverse_node

void Tutorial::collect_cameras(S72::Node *node, mat4 parent_transform, bool log_new_cameras)
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
		if (log_new_cameras) {
			std::cout << "[Tutorial.cpp]: Emplacing camera: {" << node->camera->name << "} into scene_cameras." << std::endl;
		}
	}

	for (auto &child_node : node->children)
	{
		collect_cameras(child_node, WORLD_FROM_LOCAL, log_new_cameras);
	}

}	// end of collect_cameras

void Tutorial::refresh_scene_cameras()
{
	scene_cameras.clear();
	for (S72::Node *root : scene_S72.scene.roots)
	{
		collect_cameras(root, mat4_identity(), false);
	}
}

Tutorial::WorldBounds Tutorial::get_world_bounds(SceneMesh const &mesh, mat4 const &world_from_local)
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

}	// end of get_world_bounds

// Helper: project 8 corners onto axis (ax,ay,az), return {min,max}.
static void project_corners(float const corners[8][3], float ax, float ay, float az,
	float &out_min, float &out_max)
{
	out_min = out_max = ax * corners[0][0] + ay * corners[0][1] + az * corners[0][2];
	for (int i = 1; i < 8; ++i) {
		float p = ax * corners[i][0] + ay * corners[i][1] + az * corners[i][2];
		out_min = std::min(out_min, p);
		out_max = std::max(out_max, p);
	}
}

// Helper: returns true if intervals [a_min,a_max] and [b_min,b_max] overlap.
static bool intervals_overlap(float a_min, float a_max, float b_min, float b_max)
{
	return !(a_max < b_min || b_max < a_min);
}

// Helper: test separating axis; returns true if there is a separation (cull).
static bool test_axis(float const frustum_corners[8][3], float const box_corners[8][3],
	float ax, float ay, float az)
{
	float f_min, f_max, b_min, b_max;
	project_corners(frustum_corners, ax, ay, az, f_min, f_max);
	project_corners(box_corners, ax, ay, az, b_min, b_max);
	return !intervals_overlap(f_min, f_max, b_min, b_max);
}

bool Tutorial::is_inside_frustum(WorldBounds &bounds)
{
	// Build box corners: OBB uses bounds.corners; AABB uses min/max.
	float box_corners[8][3];
	float box_axes[3][3];

	if (bv_mode == BoundingVolumeMode::OBB)
	{
		for (int i = 0; i < 8; ++i)
			for (int d = 0; d < 3; ++d)
				box_corners[i][d] = bounds.corners[i][d];
		// 3 OBB axes from edges
		for (int d = 0; d < 3; ++d) {
			box_axes[0][d] = bounds.corners[1][d] - bounds.corners[0][d];
			box_axes[1][d] = bounds.corners[2][d] - bounds.corners[0][d];
			box_axes[2][d] = bounds.corners[4][d] - bounds.corners[0][d];
		}
		for (int a = 0; a < 3; ++a) {
			float len = std::sqrt(box_axes[a][0]*box_axes[a][0] + box_axes[a][1]*box_axes[a][1] + box_axes[a][2]*box_axes[a][2]);
			float inv = (len > 1e-8f) ? (1.0f / len) : 1.0f;
			box_axes[a][0] *= inv; box_axes[a][1] *= inv; box_axes[a][2] *= inv;
		}
	}
	else if (bv_mode == BoundingVolumeMode::AABB)
	{
		// Corner order matches get_world_bounds: ix varies fastest (0=min,1=max)
		box_corners[0][0]=bounds.min_x; box_corners[0][1]=bounds.min_y; box_corners[0][2]=bounds.min_z;
		box_corners[1][0]=bounds.max_x; box_corners[1][1]=bounds.min_y; box_corners[1][2]=bounds.min_z;
		box_corners[2][0]=bounds.min_x; box_corners[2][1]=bounds.max_y; box_corners[2][2]=bounds.min_z;
		box_corners[3][0]=bounds.max_x; box_corners[3][1]=bounds.max_y; box_corners[3][2]=bounds.min_z;
		box_corners[4][0]=bounds.min_x; box_corners[4][1]=bounds.min_y; box_corners[4][2]=bounds.max_z;
		box_corners[5][0]=bounds.max_x; box_corners[5][1]=bounds.min_y; box_corners[5][2]=bounds.max_z;
		box_corners[6][0]=bounds.min_x; box_corners[6][1]=bounds.max_y; box_corners[6][2]=bounds.max_z;
		box_corners[7][0]=bounds.max_x; box_corners[7][1]=bounds.max_y; box_corners[7][2]=bounds.max_z;
		box_axes[0][0]=1; box_axes[0][1]=0; box_axes[0][2]=0;
		box_axes[1][0]=0; box_axes[1][1]=1; box_axes[1][2]=0;
		box_axes[2][0]=0; box_axes[2][1]=0; box_axes[2][2]=1;
	}
	else
	{
		std::cerr << "[SceneViewer.cpp]: unknown bounding volume mode in is_inside_frustum\n";
		return false;
	}

	// 26 SAT axes: 3 box face normals, 5 frustum face normals, 18 cross products (3 box x 6 frustum edges)

	// 3 box axes
	for (int i = 0; i < 3; ++i)
		if (test_axis(frustum_corners, box_corners, box_axes[i][0], box_axes[i][1], box_axes[i][2]))
			return false;

	// 5 frustum face normals (near and far are anti-parallel, so 5 unique)
	for (int i = 0; i < 5; ++i) {
		float ax = frustum_planes[i][0], ay = frustum_planes[i][1], az = frustum_planes[i][2];
		if (test_axis(frustum_corners, box_corners, ax, ay, az))
			return false;
	}

	// 18 cross products: box_axes x frustum_edges
	for (int bi = 0; bi < 3; ++bi) {
		for (int fi = 0; fi < 6; ++fi) {
			float cx = box_axes[bi][1] * frustum_edges[fi][2] - box_axes[bi][2] * frustum_edges[fi][1];
			float cy = box_axes[bi][2] * frustum_edges[fi][0] - box_axes[bi][0] * frustum_edges[fi][2];
			float cz = box_axes[bi][0] * frustum_edges[fi][1] - box_axes[bi][1] * frustum_edges[fi][0];
			float len = std::sqrt(cx*cx + cy*cy + cz*cz);
			if (len < 1e-8f) continue;  // parallel, skip
			float inv = 1.0f / len;
			if (test_axis(frustum_corners, box_corners, cx*inv, cy*inv, cz*inv))
				return false;
		}
	}

	return true;

}	// end of is_inside_frustum

void Tutorial::draw_bounds(const WorldBounds &bounds)
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
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = bounds.corners[a][0], .y = bounds.corners[a][1], .z = bounds.corners[a][2]},
				.Color{.r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},	// red
			});
			lines_vertices.emplace_back(PosColVertex{
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
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = c[a][0], .y = c[a][1], .z = c[a][2]},
				.Color{.r = 0x00, .g = 0xff, .b = 0x00, .a = 0xff},	// green
			});
			lines_vertices.emplace_back(PosColVertex{
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

}	// end of draw_bounds

int Tutorial::get_lerp_interval(const std::vector<float> &times, float t)
{   
    // no valid interval
    if (times.size() < 2)
    {
        return -1;
    }

    // clamp to the first frame
    if (t < times[0])
    {
        return 0;
    }

    // clamp to the last frame
    if (t >= times.back())
    {
        return int(times.size() - 2);
    }
    
    // search for the enclosing interval
    uint32_t index = 0;
    for (; index < times.size() - 1; index++)
    {
        if (t >= times[index] && t < times[index + 1])
        {
            return index;
        }
    }

    return -1;
}   // end of get lerp interval

S72::vec3 Tutorial::step(S72::vec3 const &a, S72::vec3 const &b, float t)
{
    if (t >= 1.0f) { return b; }
    return a;
}

S72::quat Tutorial::step(S72::quat const &a, S72::quat const &b, float t)
{
    if (t >= 1.0f) { return b; }
    return a;
}

S72::vec3 Tutorial::lerp(S72::vec3 const &a, S72::vec3 const &b, float t)
{
    if (t <= 0.0f) { return a; }
    if (t >= 1.0f) { return b; }
    return S72::vec3{
        a.x + t * (b.x - a.x),
        a.y + t * (b.y - a.y),
        a.z + t * (b.z - a.z),
    };
}

S72::quat Tutorial::lerp(S72::quat const &a, S72::quat const &b, float t)
{
    if (t <= 0.0f) { return a; }
    if (t >= 1.0f) { return b; }
    float qx = (1.0f - t) * a.x + t * b.x;
    float qy = (1.0f - t) * a.y + t * b.y;
    float qz = (1.0f - t) * a.z + t * b.z;
    float qw = (1.0f - t) * a.w + t * b.w;
    float len = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (len > 1e-6f) {
        qx /= len; qy /= len; qz /= len; qw /= len;
    }
    return S72::quat{ .x = qx, .y = qy, .z = qz, .w = qw };
}

// For vec3: use linear interpolation (slerp falls back to lerp)
S72::vec3 Tutorial::slerp(S72::vec3 const &a, S72::vec3 const &b, float t)
{
    return lerp(a, b, t);
}

S72::quat Tutorial::slerp(S72::quat const &a, S72::quat const &b, float t)
{
    if (t <= 0.0f) { return a; }
    if (t >= 1.0f) { return b; }
    float q0x = a.x, q0y = a.y, q0z = a.z, q0w = a.w;
    float q1x = b.x, q1y = b.y, q1z = b.z, q1w = b.w;
    float dot = q0x * q1x + q0y * q1y + q0z * q1z + q0w * q1w;
    if (dot < 0.0f) {
        q1x = -q1x; q1y = -q1y; q1z = -q1z; q1w = -q1w;
        dot = -dot;
    }
    if (dot > 0.9995f) {
        return lerp(a, b, t);
    }
    float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
    float sin_theta = std::sin(theta);
    float wa = std::sin((1.0f - t) * theta) / sin_theta;
    float wb = std::sin(t * theta) / sin_theta;
    return S72::quat{
        .x = wa * q0x + wb * q1x,
        .y = wa * q0y + wb * q1y,
        .z = wa * q0z + wb * q1z,
        .w = wa * q0w + wb * q1w,
    };
}

std::vector<float> Tutorial::get_lerp_value(const S72::Driver &d, float t)
{
    int index = get_lerp_interval(d.times, t);

    if (index == -1)
    {
        return {-1.0f};
    }

    // @return
    std::vector<float> values{};

    // timestamps at lb and up of the interval
    float a = d.times[index], b = d.times[index + 1];

    // t
    float fraction = (b - a) > 0.0f ? (t - a) / (b - a) : 0.0f;
    fraction = std::clamp(fraction, 0.0f, 1.0f);

    if (d.channel == S72::Driver::Channel::translation || d.channel == S72::Driver::Channel::scale)
    {
        S72::vec3 va{ d.values[index * 3 + 0], d.values[index * 3 + 1], d.values[index * 3 + 2] };
        S72::vec3 vb{ d.values[(index + 1) * 3 + 0], d.values[(index + 1) * 3 + 1], d.values[(index + 1) * 3 + 2] };
        S72::vec3 result;
        if (d.interpolation == S72::Driver::Interpolation::STEP)
            result = step(va, vb, fraction);
        else if (d.interpolation == S72::Driver::Interpolation::LINEAR)
            result = lerp(va, vb, fraction);
        else if (d.interpolation == S72::Driver::Interpolation::SLERP)
            result = slerp(va, vb, fraction);
        else
            return {-1.0f};
        values.emplace_back(result.x);
        values.emplace_back(result.y);
        values.emplace_back(result.z);
    }
    else if (d.channel == S72::Driver::Channel::rotation)
    {
        S72::quat qa{ d.values[index * 4 + 0], d.values[index * 4 + 1], d.values[index * 4 + 2], d.values[index * 4 + 3] };
        S72::quat qb{ d.values[(index + 1) * 4 + 0], d.values[(index + 1) * 4 + 1], d.values[(index + 1) * 4 + 2], d.values[(index + 1) * 4 + 3] };
        S72::quat result;
        if (d.interpolation == S72::Driver::Interpolation::STEP)
            result = step(qa, qb, fraction);
        else if (d.interpolation == S72::Driver::Interpolation::LINEAR)
            result = lerp(qa, qb, fraction);
        else if (d.interpolation == S72::Driver::Interpolation::SLERP)
            result = slerp(qa, qb, fraction);
        else
            return {-1.0f};
        values.emplace_back(result.x);
        values.emplace_back(result.y);
        values.emplace_back(result.z);
        values.emplace_back(result.w);
    }
    else
    {
        return {-1.0f};
    }

    return values;

}   // end of get lerp value

void Tutorial::apply_driver(const S72::Driver &d, float t)
{
    std::vector<float> vals = get_lerp_value(d, t);
	
    if (vals.size() == 1 && vals[0] == -1.0f) {
        // Error sentinel from get_lerp_value
        return;
    }

    // Apply to the node based on channel
    if (d.channel == S72::Driver::Channel::translation) {
        if (vals.size() != 3) return;
        d.node.translation = S72::vec3{
            .x = vals[0],
            .y = vals[1],
            .z = vals[2],
        };
    }
    else if (d.channel == S72::Driver::Channel::scale) {
        if (vals.size() != 3) return;
        d.node.scale = S72::vec3{
            .x = vals[0],
            .y = vals[1],
            .z = vals[2],
        };
    }
    else if (d.channel == S72::Driver::Channel::rotation) {
        if (vals.size() != 4) return;
        d.node.rotation = S72::quat{
            .x = vals[0],
            .y = vals[1],
            .z = vals[2],
            .w = vals[3],
        };
    }
}   // end of apply driver

