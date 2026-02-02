#ifndef OBJLOADER_H
#define OBJLOADER_H

#include <vector>
#include <iostream>
#include <array>
#include <unordered_map>
#include <numeric>


#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

class OBJLoader {
	// Won't use the Vertex struct because these are only the vertex data, not the faces
	std::vector<glm::vec4> positions = std::vector<glm::vec4>();
	std::vector<glm::vec4> textureCoords = std::vector<glm::vec4>();
	std::vector<glm::vec4> normals = std::vector<glm::vec4>();

	struct VertexData {
		float* vertices = nullptr;
		int verticesSize = 0; // Considers Stride, but not type
		int verticesCount = 0; // Just the vertices count
	};

	VertexData vData;
	VertexData ssbVData;

public:
	OBJLoader(const char* filepath);

	~OBJLoader() { delete[] vData.vertices; delete[] ssbVData.vertices; };

	struct Vertex {
		glm::vec4 position;
		glm::vec4 textureCoord;
		glm::vec4 normal;

		static constexpr int GetStride() { return (4 * 3); }
	};

	struct Triangle {
		glm::uvec4 indices;
		glm::vec4 area;
	};

	struct MeshGeometry {
		std::vector<Vertex> vertices;
		std::vector<glm::uvec3> triangles;
	};

	struct Material {
		glm::vec4 albedo = glm::vec4(1.0f);
		float specular = 0.0f;
		float roughness = 1.0f;
		float IOR = 0.0f;
		float transmission = 0.0f;

		glm::vec3 emissiveColor = glm::vec3(0.0f);
		float emissivePower = 0.0f;
	};

private:
	std::vector<Vertex> objVertices;
	std::vector<Triangle> triangles = std::vector<Triangle>();

	std::vector<Material> mats;
	std::unordered_map<std::string, uint32_t> matIdxMap;
	
	uint32_t currentMaterialIndex = 0;
	std::vector<uint32_t> matIndices;
	glm::vec4 LoadVectorData(const std::string& data);
	std::vector<Vertex> LoadFace(const std::string& face);
	Vertex CreateVertex(const std::string& indicies);
	void CreateVertexArray(const std::vector<Vertex>& loadedVertices);
	void CreateSSBuffer(const std::vector<Vertex>& loadedVertices);

public:
	VertexData GetVerticesAsSSBuffer() const { return ssbVData; };

	VertexData GetVertices() const { return vData; }

	std::vector<glm::vec4> GetPositions() const { return positions; }
	std::vector<Triangle> GetTriangles() const { return this->triangles; }
	std::vector<Material> GetMaterials() const { return this->mats; }
	std::vector<Vertex> GetObjVertices() const { return this->objVertices; }

	MeshGeometry GetMeshGeometry() const {
		std::vector<glm::uvec3> triIdxs(triangles.size());
		for (int i = 0; i < triangles.size(); i++) triIdxs[i] = triangles[i].indices;
		return { this->objVertices, triIdxs };
	}
};

#endif // !OBJLOADER_H
