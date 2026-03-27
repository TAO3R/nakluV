#pragma once

#include "PosColVertex.hpp"
#include "PosNorTexVertex.hpp"

#include "mat4.hpp"

#include "RTG.hpp"

#include "S72.hpp"

#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

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

	// used when camera_mode == CameraMode::User (or Debug):
	struct OrbitCamera {
		float target_x = 0.0f, target_y = 0.0f, target_z = 0.0f;	// where the camera is looking + orbiting
		float radius = 2.0f;	// distance from camera to target
		float azimuth = 0.0f;	// countterclockwise angle around z axis between x axis and camera disrection (radians)
		float elevation = 0.25f * float(M_PI);	// angle up from xy plane to camera direction (radians)

		float fov = 60.0f / 180.0f * float (M_PI);
		float near = 0.1f;	// near clipping plane
		float far = 1000.0f;	// far clipping plane
	}	free_camera;	// user camera mode
	
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

	//--------------------------------------------------------------------
	// A1-Scene Viewer:

	// LOAD
	/** Stores the loaded scene file */
	S72 scene_S72;

	/** Stores loaded .b72 binary files that can be looked up by name: src -> raw bytes */
	std::unordered_map<std::string, std::vector<uint8_t>> loaded_data;

	/**
	 * Called within the constructor of Tutorial
	 * Tries to load the scene file and print info if specified
	 */
	void load_scene();

	/**
	 * Called within the constructor of Tutorial
	 * Tries to load .b72 files into memory
	 */
	void load_scene_binaries();


	// SHOW
	
	/** Struct for storing per-mesh GPU data in a scene
	 *  - no indices (all TRIANGLE_LIST, non-indexed)
	 *  - fixed 48-byte interleaved layout: POSITION(12) + NORMAL(12) + TANGENT(16) + TEXCOORD(8)
	 *  - lambertian-only materials
	 */
	struct SceneMesh {
		ObjectVertices vertices;		// first & count into scene_vertices buffer
		S72::Material *material;		// pointer to material (always lambertian per spec)
		float min_x = INFINITY, min_y = INFINITY, min_z = INFINITY;		// model-sapce aabb
		float max_x = -INFINITY, max_y = -INFINITY, max_z = -INFINITY;	// model-space aabb
	};

	/** A hash table to look up meshes by name */
	std::unordered_map<std::string,	SceneMesh> scene_meshes;

	/** A hash table to look up texture descriptor set index by material */
	std::unordered_map<S72::Material const *, uint32_t> mat_to_tex;

	/**
	 * Helper function that converts normalized rgb value to hex value
	 * @param struct wrapping normalized rgb value
	 * @return hex value
	 */
	uint32_t color_to_hex(S72::color const *col);

	/**
	 * Called when Tutorial is constructed
	 * Iterates scene_s72.materials to build scene materials
	 */
	void build_scene_materials();

	/**
	 * Called ...
	 * Iterates scene_s72.textures to build scene textures
	 */
	void build_scene_textures();

	/** A combined vertex buffer for all scene meshes */
	Helpers::AllocatedBuffer scene_vertices;

	/**
	 * Called every frame in Tutorial::update if a scene is loaded
	 * Recursively traverses down a scene graph from its root node,
	 *  pushing `ObjectInstance`s and `SceneCamera`s into `object_instances` and `scene_cameras`
	 */
	void traverse_node(S72::Node *node, mat4 parent_transform);

	/** Defines different camera modes */
	enum class CameraMode {
		Scene = 0,	// renders through a scene camera; user cannot move it, but can cycle between scene cameras
		User  = 1,	// renders through a user-controlled orbit camera (keyboard+mouse)
		Debug = 2,	// renders through a second user-controlled camera; culling uses the *previously active* camera
	};
	
	/** Stores the current camera mode */
	CameraMode camera_mode = CameraMode::User;

	/** Struct that defines a scene camera */
	struct SceneCamera {
		S72::Camera *camera;
		mat4 WORLD_FROM_CAMERA;
	};

	/** Stores all scene cameras of a scene */
	std::vector<SceneCamera> scene_cameras;

	/** Stores the current scene camera by index, defaults to -1 when no scene is loaded */
	int scene_camera_index = -1;

	/** Records culling matrix when entering debug camera mode */
	mat4 CULLING_CLIP_FROM_WORLD;	// recording culling matrix when entering debug camera mode

	/**
	 * Called within build_scene_camera
	 * Recursively searches for scene cameras from roots of a scene graph and builds scene camera instances
	 */
	void collect_cameras(S72::Node *node, mat4 parent_transform);

	/** An orbit camera instance for debugging */
	OrbitCamera debug_camera;

	/** Stores whether to show debug visuals */
	bool is_showing_debug_lines = false;	// turning on and off debug lines for camera frustum and bounding volumes


	// CULL
	/** Defines different camera modes */
	enum class CullingMode {
		None = 0,
		Frustum = 1,
		Count = 2,
	};
	
	/** Stores the current culling mode */
	CullingMode culling_mode = CullingMode::None;

	/**
	 * Called within Tutorial's constructor
	 * Sets the culling mode to be used according to the config
	 */
	void initialize_cull_mode();

	/** Stores information of 6 frustum planes in the order of left, right, buttom, top, near, far */
	float frustum_planes[6][4];

	/** World-space frustum corners (NDC cube transformed by inverse of CULLING_CLIP_FROM_WORLD). Order: near 0,1,2,3, far 4,5,6,7. */
	float frustum_corners[8][3];
	/** Six unique frustum edge directions (normalized), for SAT cross-product axes. */
	float frustum_edges[6][3];

	/**
	 * Called every frame within Tutorial's update
	 * Computes frustum planes (used for culling) to match the camera position
	 */
	void compute_frustum_planes();

	/**
	 * Called every frame within Tutorial's update
	 * Computes 8 corners of the camera frustum from the inverse of CULLING_CLIP_FROM_WORLD and pushes as lines to lines pipeline
	 */
	void draw_camera_frustum();

	/** Stores a bounding volume information in world space */
	struct WorldBounds {
		// 8 transformed world-space obb corners
		float corners[8][3];
		// world-space aabb
		float min_x = INFINITY, min_y = INFINITY, min_z = INFINITY;
		float max_x = -INFINITY, max_y = -INFINITY, max_z = -INFINITY;
	};
	
	/** Stores bounding volumes that are not culled */
	std::vector<WorldBounds> object_bounds;

	/** Defines different bounding volume modes that will be used in culling */
	enum class BoundingVolumeMode {
		OBB = 0,
		AABB = 1,
		Count = 2,
	};
	
	/** Stores the current bounding volume mode used */
	BoundingVolumeMode bv_mode = BoundingVolumeMode::OBB;

	/**
	 * Called within traverse_node if culling mode is set to frustum culling
	 * Calculates a scene mesh's bounding volume in world space.
	 * @return The bounding volume information
	 */
	WorldBounds get_world_bounds(SceneMesh const &mesh, mat4 const &world_from_local);

	/**
	 * Called within traverse_node if culling mode is set to frustum culling
	 * https://bruop.github.io/improved_frustum_culling/
	 * Employs Separating Axis Theorem to perform a robust frustum culling.
	 * @return Whether a bounding volume is inside the camera's frustum
	 */
	bool is_inside_frustum(WorldBounds &bounds);

	/**
	 * Called every frame in Tutorial::update if has scene vertices, the camera is in debug mode, and is showing debug visuals
	 * Draws debug visuals given a bounding volume.
	 */
	void draw_bounds(const WorldBounds &bounds);


	// MOVE
	
	/**
	 * Called within get_lerp_value
	 * Tries to get the index of the lower bound of the interval where the timestamp should be sampled
	 * @param t
	 *  The timestamp used to sample the driver
	 * @return
	 *  The lower bound index of the time interval the timestamp is sampled within,
	 *  -1 if not found
	 */
	int get_lerp_interval(const std::vector<float> &times, float t);

	/**
	 * Step interpolation for vec3. Holds the value at the start of the interval (a) for t in [0, 1).
	 * @param a Lower bound of the interval
	 * @param b Upper bound of the interval
	 * @param t Normalized fraction [0, 1]
	 * @return a if t < 1, otherwise b
	 */
	S72::vec3 step(S72::vec3 const &a, S72::vec3 const &b, float t);

	/**
	 * Step interpolation for quat. Holds the value at the start of the interval (a) for t in [0, 1).
	 */
	S72::quat step(S72::quat const &a, S72::quat const &b, float t);

	/**
	 * Linear interpolation for vec3.
	 * @param a Lower bound
	 * @param b Upper bound
	 * @param t Normalized fraction [0, 1]
	 */
	S72::vec3 lerp(S72::vec3 const &a, S72::vec3 const &b, float t);

	/**
	 * Linear interpolation for quat. LERPs each component and normalizes the result.
	 */
	S72::quat lerp(S72::quat const &a, S72::quat const &b, float t);

	/**
	 * Spherical linear interpolation. For vec3: falls back to linear interpolation.
	 * For quat: interpolates along the shortest arc on the quaternion sphere.
	 * @param a Lower bound
	 * @param b Upper bound
	 * @param t Normalized fraction [0, 1]
	 */
	S72::vec3 slerp(S72::vec3 const &a, S72::vec3 const &b, float t);
	S72::quat slerp(S72::quat const &a, S72::quat const &b, float t);

	/**
	 * Called ...
	 * Tries to get the interpolated value of a driver given a timestamp
	 * @param t
	 * 	The timestamp used to sample the driver
	 * @return
	 * 	Interpolated values of length 3 (Translation / Scale) or 4 (Rotation)
	 */
	std::vector<float> get_lerp_value(const S72::Driver &d, float t);

	/**
	 * Called every frame for every driver in update
	 * Tries to apply the effect of a driver at a given timestamp
	 * @param d
	 * 	The driver to apply
	 * @param t
	 * 	The timestamp to apply at
	 */
	void apply_driver(const S72::Driver &d, float t);

	/** Stores the time value for sampling animations */
	float anim_time = 0.0f;


	


};
