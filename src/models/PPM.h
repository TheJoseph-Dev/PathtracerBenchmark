#ifndef PPM_H
#define PPM_H
#include <string>
#include <vector>
#include <glm/glm.hpp>
class PPM {
public:
	static void Save(const std::string& filepath, const std::vector<glm::vec4>& pixels, uint32_t width, uint32_t height, bool useGamma = true);
	static void Load(const std::string& filepath, std::vector<glm::vec4>& pixels, uint32_t& width, uint32_t& height);
};
#endif