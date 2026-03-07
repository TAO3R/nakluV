#include <cassert>
#include <iostream>

#include "Tutorial.hpp"



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
			if (culling_mode == CullingMode::None)
			{
				// push ObjectInstance to object_instances
				// WORLD_FROM_LOCAL_NORMAL = inverse transpose of WORLD_FROM_LOCAL when non-uniform scale is present
				mat4 WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL;
				object_instances.emplace_back(ObjectInstance{
					.vertices = it->second.vertices,
					.transform {
						.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
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
					object_instances.emplace_back(ObjectInstance{
						.vertices = it->second.vertices,
						.transform {
							.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL_NORMAL = mat4_inverse_transpose(WORLD_FROM_LOCAL_NORMAL),
						},
						// TODO: texture

					});

					// push WorldBounds to object_bounds
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

bool Tutorial::is_inside_frustum(WorldBounds &bounds)
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

std::vector<float> Tutorial::get_lerp_value(const S72::Driver &d, float t)
{
    int index = get_lerp_interval(d.times, t);

    if (index == -1)
    {
        return {-1};
    }

    std::vector<float> values{};
    float a = d.times[index], b = d.times[index + 1];   // timestamps at lb and up of the interval

    if (d.interpolation == S72::Driver::Interpolation::STEP)
    {
        if (d.channel == S72::Driver::Channel::translation || d.channel == S72::Driver::Channel::scale)
        {
            if (t < (a + b) / 2.0f)
            {   // closer to lb
                values.emplace_back(d.times[index * 3]);
                values.emplace_back(d.times[index * 3 + 1]);
                values.emplace_back(d.times[index * 3 + 2]);
            }
            else
            {   // closer to ub
                values.emplace_back(d.times[(index + 1) * 3]);
                values.emplace_back(d.times[(index + 1) * 3 + 1]);
                values.emplace_back(d.times[(index + 1) * 3 + 2]);
            }
        }
        else if (d.channel == S72::Driver::Channel::rotation)
        {
            if (t < (a + b) / 2.0f)
            {   // closer to lb
                values.emplace_back(d.times[index * 3]);
                values.emplace_back(d.times[index * 3 + 1]);
                values.emplace_back(d.times[index * 3 + 2]);
                values.emplace_back(d.times[index * 3 + 3]);
            }
            else
            {   // closer to ub
                values.emplace_back(d.times[(index + 1) * 3]);
                values.emplace_back(d.times[(index + 1) * 3 + 1]);
                values.emplace_back(d.times[(index + 1) * 3 + 2]);
                values.emplace_back(d.times[(index + 1) * 3 + 3]);
            }
        }
        else
        {
            return {-1};
        }
        
    }
    else if (d.interpolation == S72::Driver::Interpolation::LINEAR)
    {
        float fraction = (t - a) / (b - a);

        if (d.channel == S72::Driver::Channel::translation || d.channel == S72::Driver::Channel::scale)
        {
            // v = a + t(b - a)
            values.emplace_back(
                d.times[index * 3] + fraction * (d.times[(index + 1) * 3] - d.times[index * 3]));
            values.emplace_back(
                d.times[index * 3 + 1] + fraction * (d.times[(index + 1) * 3 + 1] - d.times[index * 3 + 1]));
            values.emplace_back(
                d.times[index * 3 + 2] + fraction * (d.times[(index + 1) * 3 + 2] - d.times[index * 3 + 2]));
        }
        else if (d.channel == S72::Driver::Channel::rotation)
        {
            values.emplace_back(
                d.times[index * 3] + fraction * (d.times[(index + 1) * 3] - d.times[index * 3]));
            values.emplace_back(
                d.times[index * 3 + 1] + fraction * (d.times[(index + 1) * 3 + 1] - d.times[index * 3 + 1]));
            values.emplace_back(
                d.times[index * 3 + 2] + fraction * (d.times[(index + 1) * 3 + 2] - d.times[index * 3 + 2]));
            values.emplace_back(
                d.times[index * 3 + 3] + fraction * (d.times[(index + 1) * 3 + 3] - d.times[index * 3 + 3]));
        }
        else
        {
            return {-1};
        }
    }
    else if (d.interpolation == S72::Driver::Interpolation::SLERP)
    {
        float fraction = (t - a) / (b - a);

        if (d.channel == S72::Driver::Channel::translation || d.channel == S72::Driver::Channel::scale)
        {
            
        }
        else if (d.channel == S72::Driver::Channel::rotation)
        {

        }
        else
        {
            return {-1};
        }
    }
    else
    {
        return {-1};
    }

    return values;

}   // end of get lerp value

void Tutorial::apply_driver(const S72::Driver &d, float t)
{

}   // end of apply driver

