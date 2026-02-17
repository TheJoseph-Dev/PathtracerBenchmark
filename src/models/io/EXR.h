#ifndef EXR_H
#define EXR_H
#include <string>
#include <vector>
#include <glm/glm.hpp>
class EXR {
public:
	static void Save(const std::string& filepath, const std::vector<glm::vec4>& pixels, uint32_t width, uint32_t height);
	static void Load(const std::string& filepath, std::vector<glm::vec4>& pixels, uint32_t& width, uint32_t& height);
};
#endif