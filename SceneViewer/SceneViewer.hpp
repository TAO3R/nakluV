#pragma once

#include "S72.hpp"
#include "Helpers.hpp"
#include "mat4.hpp"

struct Tutorial;	// forward declaration

struct SVConfig {
    std::string scene_file = "";	// --scene
    bool print_scene = false;		// --print
    std::string scene_camera = "";	// --camera
    std::string culling_mode = "";	// --cull

};	// end of SVConfig

struct SceneViewer {
	SceneViewer(Tutorial &);
	SceneViewer(SceneViewer const &) = delete;		// copy construction disabled
	~SceneViewer();

	Tutorial &tutorial;

    // load
	/** References the loaded scene file */
    S72 scene_S72;

	/** .b72 binary files: src -> raw bytes */
    std::unordered_map<std::string, std::vector<uint8_t>> loaded_data;

	/** Tries to load the scene file and print info if specified */
	void load_scene();

	/** Tries to load .b72 files into memory */
	void load_scene_binaries();


    // show
	/** Struct for storing per-mesh GPU data in a scene
	 *  - no indices (all TRIANGLE_LIST, non-indexed)
	 *  - fixed 48-byte interleaved layout: POSITION(12) + NORMAL(12) + TANGENT(16) + TEXCOORD(8)
	 *  - lambertian-only materials
	 */
    struct SceneMesh {
		Tutorial::ObjectVertices vertices;	// first & count into scene_vertices buffer
		S72::Material *material;		    // pointer to material (always lambertian per spec)
		float min_x = INFINITY, min_y = INFINITY, min_z = INFINITY;		// model-sapce aabb
		float max_x = -INFINITY, max_y = -INFINITY, max_z = -INFINITY;	// model-space aabb
	};

	/** A hash table to look up meshes by name */
	std::unordered_map<std::string,	SceneMesh> scene_meshes;

	/** A combined vertex buffer for all scene meshes */
    Helpers::AllocatedBuffer scene_vertices;

	/** Tries to construct scene meshes from loaded binary files and upload to the GPU */
	void build_scene_meshes();

	/**
	 * Called every frame in Tutorial::update if a scene is loaded
	 * Recursively traverses down a scene graph from its root node.
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
	mat4 CULLING_CLIP_FROM_WORLD;

	/**
	 * Called within build_scene_camera
	 * Recursively searches for scene cameras from roots of a scene graph and builds scene camera instances
	 */
	void collect_cameras(S72::Node *node, mat4 parent_transform);

	/**
	 * Called in the constructor of Tutorial if a scene is loaded
	 * Collects scene cameras from root of a scene graph and initialize camera mode and scene camera index if specified in the config
	 */
	void initialize_scene_cameras();

	/** An orbit camera instance for debugging */
	Tutorial::OrbitCamera debug_camera;

	/** Stores whether to show debug visuals */
	bool is_showing_debug_lines = false;


	// cull
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

	/** Stores information of 6 frustum planes */
	float frustum_planes[6][4];	// left, right, buttom, top, near, far

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
	 * Judges whether a bounding volume is inside the camera's frustum.
	 * @return Whether a bounding volume is inside the camera's frustum
	 */
	bool is_inside_frustum(WorldBounds &bounds);

	/**
	 * Called every frame in Tutorial::update if has scene vertices, the camera is in debug mode, and is showing debug visuals
	 * Draws debug visuals given a bounding volume.
	 */
	void draw_bounds(const WorldBounds &bounds);


	// move



	// fast



	// test



	// make


};	// end of SceneViewer