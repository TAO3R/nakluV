#pragma once

#include "PosColVertex.hpp"
#include "PosNorTexVertex.hpp"

#include "mat4.hpp"

#include "RTG.hpp"

#include "S72.hpp"

struct Tutorial : RTG::Application {

	Tutorial(RTG &);
	Tutorial(Tutorial const &) = delete; //you shouldn't be copying this object
	~Tutorial();

	//kept for use in destructor:
	RTG &rtg;

	//--------------------------------------------------------------------
	//Resources that last the lifetime of the application:

	//chosen format for depth buffer:
	VkFormat depth_format{};
	//Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;

	//Pipelines:
	//none, yet

	//pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	//workspaces hold per-render resources:
	struct Workspace {
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; //from the command pool above; reset at the start of every render.
		
		// location for lines data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer lines_vertices_src;	// host coherent; mapped
		Helpers::AllocatedBuffer lines_vertices;		// device-local

		// location for LinesPipeline::Camera data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer Camera_src;	// host coherent; mapped
		Helpers::AllocatedBuffer Camera;		// device-local
		VkDescriptorSet Camera_descriptors;		// references Camera

		// location for ObjectsPipeline::World data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer World_src;	// host coherent; mapped
		Helpers::AllocatedBuffer World;	// device-local
		VkDescriptorSet World_descriptors;	// references World
		
		// location for ObjectsPipeline::Transforms data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer Transforms_src;	// host coherent; mapped
		Helpers::AllocatedBuffer Transforms;	// device-local
		VkDescriptorSet Transforms_descriptors;	// references Transforms
	};
	std::vector< Workspace > workspaces;

	// a struct that manages a 'VkPipelineLayout' which gives the type of the global inputs to the pipeline,
	// as well as a handle to the pipeline itself
	struct BackgroundPipeline {
		// no descriptor set layouts

		// push constants
		struct Push {
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// no vertex bindings


		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} background_pipeline;

	struct LinesPipeline {
		// descriptor set layouts:
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		// types for descriptors:
		struct Camera {
			mat4 CLIP_FROM_WORLD;
		};
		static_assert(sizeof(Camera) == 16*4, "camera buffer structure is packed");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// vertex bindings
		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} lines_pipeline;

	struct ObjectsPipeline {
		// descriptor set layouts:
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_Texture = VK_NULL_HANDLE;

		// types for descriptors:
		struct World {
			struct {float x, y, z, padding_;} SKY_DIRECTION;
			struct {float r, g, b, padding_;} SKY_ENERGY;
			struct {float x, y, z, padding_;} SUN_DIRECTION;
			struct {float r, g, b, padding_;} SUN_ENERGY;
		};	// padings are required by the std140 layout, which aligns vec3s on 4-element boundaries.
		static_assert(sizeof(World) == 4*4 + 4*4 + 4*4 + 4*4, "World is the expected size.");

		struct Transform  {
			mat4 CLIP_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL_NORMAL;
		};
		static_assert(sizeof(Transform) == 16*4 + 16 * 4 + 16 * 4, "Transform is the expected size.");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// vertex bindings
		using Vertex = PosNorTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} objects_pipeline;

	//-------------------------------------------------------------------
	//static scene resources:

	Helpers::AllocatedBuffer object_vertices;	// stores vertex data for all meshes
	struct ObjectVertices {
		uint32_t first = 0;	// index of the first vertex
		uint32_t count = 0;	// count of vertices
	};	
	ObjectVertices plane_vertices;
	ObjectVertices torus_vertices;

	std::vector<Helpers::AllocatedImage> textures;	// handles to the actual image data
	std::vector<VkImageView> texture_views;	// references to portions of the texture
	VkSampler texture_sampler = VK_NULL_HANDLE;	// gives the sampler state (wrapping, interpolation, etc) for reading from the textures
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;	// the pool from which we allocate texture descriptor sets
	std::vector<VkDescriptorSet> texture_descriptors;	// allocated from texture_descriptor_pool

	// A1
	S72 scene_S72;
	// .b72 binary files: src -> raw bytes
	std::unordered_map<std::string, std::vector<uint8_t>> loaded_data;
	
	// Per-mesh GPU data, simplified per A1 spec:
	//  - no indices (all TRIANGLE_LIST, non-indexed)
	//  - fixed 48-byte interleaved layout: POSITION(12) + NORMAL(12) + TANGENT(16) + TEXCOORD(8)
	//  - lambertian-only materials
	struct SceneMesh {
		ObjectVertices vertices;		// first & count into scene_vertices buffer
		S72::Material *material;		// pointer to material (always lambertian per spec)
	};
	std::unordered_map<std::string,	SceneMesh> scene_meshes;

	// combined vertex buffer for all scene meshes
	Helpers::AllocatedBuffer scene_vertices;
	// recursively travere through the scene graph and pushes `ObjectInstance`s into `object_instances`
	void traverse_node(S72::Node *node, mat4 parent_transform);

	//--------------------------------------------------------------------
	//Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector< VkFramebuffer > swapchain_framebuffers;
	//used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	//Resources that change when time passes or the user interacts:

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;

	// modal action, intercepts inputs:
	std::function<void(InputEvent const &)> action;

	float time = 0.0f;

	// for selecting between caemras:
	enum class CameraMode {
		Scene = 0,
		Free = 1,
	} camera_mode = CameraMode::Free;

	// used when camera_mode == CameraMode::Free:
	struct OrbitCamera {
		float target_x = 0.0f, target_y = 0.0f, target_z = 0.0f;	// where the camera is looking + orbiting
		float radius = 2.0f;	// distance from camera to target
		float azimuth = 0.0f;	// countterclockwise angle around z axis between x axis and camera disrection (radians)
		float elevation = 0.25f * float(M_PI);	// angle up from xy plane to camera direction (radians)

		float fov = 60.0f / 180.0f * float (M_PI);
		float near = 0.1f;	// near clipping plane
		float far = 1000.0f;	// far clipping plane
	}	free_camera;

	// computed from the current camera (as set by camera_mode) during update():
	mat4 CLIP_FROM_WORLD;

	std::vector<LinesPipeline::Vertex> lines_vertices;

	ObjectsPipeline::World world;

	struct ObjectInstance {
		ObjectVertices vertices;
		ObjectsPipeline::Transform transform;
		uint32_t texture = 0;	// an index that indicates which texture descriptor to bind when drawing each instance
	};
	std::vector<ObjectInstance> object_instances;

	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
