#include "print_scene.hpp"

#include <iostream>
#include <string>

void print_info(S72 &s72){
	std::cout << "--- Scene Objects ---"<< std::endl;
	std::cout << "Scene: " << s72.scene.name << std::endl;
	std::cout << "Roots: ";
	for (S72::Node* root : s72.scene.roots) {
		std::cout << root->name << ", ";
	}
	std::cout << std::endl;

	std::cout << "Nodes: ";
	for (auto const& pair : s72.nodes) {
		std::cout << pair.first << ", ";
	}
	std::cout << std::endl;

	std::cout << "Meshes: ";
	for (auto const& pair : s72.meshes) {
		std::cout << pair.first << ", ";
	}
	std::cout << std::endl;

	std::cout << "Cameras: ";
	for (auto const& pair : s72.cameras) {
		std::cout << pair.first << ", ";
	}
	std::cout << std::endl;

	 std::cout << "Drivers: ";
	for (auto const& driver : s72.drivers) {
		std::cout << driver.name << ", ";
	}
	std::cout << std::endl;

	std::cout << "Materials: ";
	for (auto const& pair : s72.materials) {
		std::cout << pair.first << ", ";
	}
	std::cout << std::endl;

	std::cout << "Environment: ";
	for (auto const& pair : s72.environments) {
		std::cout << pair.first << ", ";
	}
	std::cout << std::endl;

	std::cout << "Lights: ";
	for (auto const& pair : s72.lights) {
		std::cout << pair.first << ", ";
	}
	std::cout << std::endl;
}

void traverse_children(S72 &s72, S72::Node* node, std::string prefix){
	//Print node information
	std::cout << prefix << node->name << ": {";
	if(node->camera != nullptr){
		std::cout << "Camera: " << node->camera->name;
	}
	if(node->mesh != nullptr){
		std::cout << "Mesh: " << node->mesh->name;
		if(node->mesh->material != nullptr){
			std::cout << " {Material: " <<node->mesh->material->name << "}";
		}
	}
	if(node->environment != nullptr){
		std::cout << "Environment: " << node->environment->name;
	}
	if(node->light != nullptr){
		std::cout << "Light: " << node->light->name;
	}

	std::cout << "}" <<std::endl;

	std::string new_prefix = prefix + "- ";
	for(S72::Node* child : node->children){
		traverse_children(s72, child, new_prefix);
	}
}
void print_scene_graph(S72 &s72){
	std::cout << std::endl << "--- Scene Graph ---"<< std::endl;
	for (S72::Node* root : s72.scene.roots) {
		std::cout << "Root: ";
		std::string prefix = "";
		traverse_children(s72, root, prefix);
	}
}
