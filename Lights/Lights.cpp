#include "../Tutorial.hpp"

#include <iostream>
#include <variant>

void Tutorial::log_lights() {
	static bool logged = false;
	if (logged || light_instances.empty()) return;
	logged = true;

	std::cout << "[A3-load]: Collected " << light_instances.size() << " light instance(s):" << std::endl;
	for (size_t i = 0; i < light_instances.size(); ++i) {
		auto &li = light_instances[i];
		const char *type = "unknown";
		if (std::holds_alternative<S72::Light::Sun>(li.light->source)) type = "sun";
		else if (std::holds_alternative<S72::Light::Sphere>(li.light->source)) type = "sphere";
		else if (std::holds_alternative<S72::Light::Spot>(li.light->source)) type = "spot";
		std::cout << "  [" << i << "] \"" << li.light->name << "\" type=" << type
			<< " pos=(" << li.world_position.x << "," << li.world_position.y << "," << li.world_position.z << ")"
			<< " dir=(" << li.world_direction.x << "," << li.world_direction.y << "," << li.world_direction.z << ")"
			<< " shadow=" << li.shadow
			<< " tint=(" << li.light->tint.r << "," << li.light->tint.g << "," << li.light->tint.b << ")"
			<< std::endl;
	}
}
