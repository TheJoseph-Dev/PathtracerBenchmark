#include "OBJLoader.h"

#include <fstream> // file stream
#include <cstdlib>
#include <cctype>
#include <string>

namespace {
	inline const char* SkipSpaces(const char* p) {
		while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
		return p;
	}

	inline const char* SkipToken(const char* p) {
		while (*p && *p != ' ' && *p != '\t' && *p != '\r') ++p;
		return p;
	}

	inline bool ParseFloat(const char*& p, float& out) {
		p = SkipSpaces(p);
		if (!*p) return false;
		char* end = nullptr;
		out = std::strtof(p, &end);
		if (end == p) return false;
		p = end;
		return true;
	}

	inline bool ParseIndexTriplet(const char*& p, int& posIndex, int& texIndex, int& normIndex) {
		p = SkipSpaces(p);
		if (!*p) return false;
		char* end = nullptr;
		long v = std::strtol(p, &end, 10);
		if (end == p) return false;
		posIndex = static_cast<int>(v);
		p = end;
		if (*p != '/') return false;
		++p;
		long t = std::strtol(p, &end, 10);
		if (end == p) return false;
		texIndex = static_cast<int>(t);
		p = end;
		if (*p != '/') return false;
		++p;
		long n = std::strtol(p, &end, 10);
		if (end == p) return false;
		normIndex = static_cast<int>(n);
		p = end;
		return true;
	}
}

OBJLoader::OBJLoader(const char* filepath) {
	std::cout << filepath << "\n\n";

	std::size_t positionCount = 0;
	std::size_t texCoordCount = 0;
	std::size_t normalCount = 0;
	std::size_t faceCount = 0;
	{
		std::ifstream countStream = std::ifstream(filepath);
		if (countStream) {
			std::string countLine;
			while (std::getline(countStream, countLine)) {
				if (countLine.size() < 2) continue;
				if (countLine[0] == 'v') {
					if (countLine[1] == ' ') ++positionCount;
					else if (countLine[1] == 't') ++texCoordCount;
					else if (countLine[1] == 'n') ++normalCount;
				}
				else if (countLine[0] == 'f' && countLine[1] == ' ') {
					++faceCount;
				}
			}
		}
	}

	std::ifstream stream = std::ifstream(filepath);
	if (!stream) { std::cerr << "ERROR: OBJLoader got a NULL directory\n"; return; }

	if (positionCount) positions.reserve(positionCount);
	if (texCoordCount) textureCoords.reserve(texCoordCount);
	if (normalCount) normals.reserve(normalCount);
	if (faceCount) {
		objVertices.reserve(faceCount * 3);
		matIndices.reserve(faceCount);
		triangles.reserve(faceCount);
	}

	std::string line;

	std::string mtlFilepath = std::string(filepath);
	mtlFilepath[mtlFilepath.size() - 1] = 'l';
	mtlFilepath[mtlFilepath.size() - 2] = 't';
	mtlFilepath[mtlFilepath.size() - 3] = 'm';
	std::ifstream matStream = std::ifstream(mtlFilepath);
	
	// Load Materials
	if(matStream)
		while (std::getline(matStream, line)) {
			if (line.rfind("newmtl", 0) == 0) {
				this->matIdxMap[line.substr(7)] = this->mats.size();
				this->mats.emplace_back();
			}
			else if (line.rfind("Ns", 0) == 0) this->mats.back().specular = stof(line.substr(3)); // Specular
			else if (line.rfind("Kd", 0) == 0) this->mats.back().albedo = LoadVectorData(line); // Albedo
			else if (line.rfind("Ke", 0) == 0) this->mats.back().emissiveColor = LoadVectorData(line); // Emissive Color
			else if (line.rfind("Ni", 0) == 0) this->mats.back().IOR = stof(line.substr(3)); // IOR
			else if (line.rfind("d", 0) == 0) this->mats.back().transmission = 1.0-stof(line.substr(2)); // Transmission
			else if (line.rfind("Pr", 0) == 0) this->mats.back().roughness = stof(line.substr(3)); // Roughness
			else if (line.rfind("pbr_emissive_power", 0) == 0) this->mats.back().emissivePower = stof(line.substr(19)); // Emissive Power
		}
	else {
		this->mats.emplace_back();
		std::cerr << "WARNING: OBJLoader couldn't find .MTL file\n";
	}

	while (getline(stream, line)) {
		if (line.rfind("usemtl", 0) == 0) { this->currentMaterialIndex = this->matIdxMap[line.substr(7)]; continue; }
		if (line.size() >= 2 && line[0] == 'v') {
			if (line[1] == ' ') { this->positions.emplace_back(LoadVectorData(line)); continue; }
			if (line[1] == 't') { this->textureCoords.emplace_back(LoadVectorData(line)); continue; }
			if (line[1] == 'n') { this->normals.emplace_back(LoadVectorData(line)); continue; }
		}

		if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') {
			this->matIndices.emplace_back(this->currentMaterialIndex);
			AppendFaceVertices(line);
			continue;
		}
	}
	//CreateVertexArray(objVertices);
	//CreateSSBuffer(objVertices);
}

// v, vt, vn
glm::vec4 OBJLoader::LoadVectorData(const std::string& data) {
	glm::vec4 vData = glm::vec4(0.0f);
	const char* p = data.c_str();
	p = SkipToken(p);

	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	if (!ParseFloat(p, x)) return vData;
	vData.x = x;
	if (ParseFloat(p, y)) vData.y = y;
	if (ParseFloat(p, z)) vData.z = z;

	return vData;
}

// f
void OBJLoader::AppendFaceVertices(const std::string& face) {
	const char* p = face.c_str();
	p = SkipToken(p);

	while (true) {
		int posIndex = 0;
		int texCoordIndex = 0;
		int normIndex = 0;
		if (!ParseIndexTriplet(p, posIndex, texCoordIndex, normIndex)) break;

		objVertices.emplace_back(CreateVertex(posIndex - 1, texCoordIndex - 1, normIndex - 1));
		if ((objVertices.size() % 3) == 0) {
			const size_t i = objVertices.size() - 3;
			const size_t triIndex = (objVertices.size() / 3) - 1;
			glm::uvec4 indices = { static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2), this->matIndices[triIndex] };
			glm::vec3 v0 = objVertices[i].position;
			glm::vec3 v1 = objVertices[i + 1].position;
			glm::vec3 v2 = objVertices[i + 2].position;
			float area = 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));
			this->triangles.push_back({ indices, { area, 0,0,0 }});
		}
	}
}

// indicies
OBJLoader::Vertex OBJLoader::CreateVertex(int posIndex, int texCoordIndex, int normIndex) {
	Vertex vertex = Vertex();

	vertex.position = this->positions.at(posIndex);
	vertex.textureCoord = this->textureCoords.at(texCoordIndex);
	vertex.normal = this->normals.at(normIndex);

	return vertex;
}


void OBJLoader::CreateVertexArray(const std::vector<OBJLoader::Vertex>& loadedVertices) {
	uint32_t arrSize = Vertex::GetStride() * loadedVertices.size();
	this->vData.vertices = new float[arrSize];
	this->vData.verticesSize = arrSize;
	this->vData.verticesCount = loadedVertices.size();
	const std::vector<Vertex>& lv = loadedVertices;
	//std::reverse(lv.begin(), lv.end());

	float* vertices = (this->vData.vertices);
	std::ofstream outfile("Project/Resources/vertices.txt");

	for (int i = 0, k = 0; k < lv.size(); i += OBJLoader::Vertex::GetStride(), k++) {
		*(vertices + i + 0) = lv.at(k).position.x;
		*(vertices + i + 1) = lv.at(k).position.y;
		*(vertices + i + 2) = lv.at(k).position.z;

		*(vertices + i + 3) = lv.at(k).textureCoord.x;
		*(vertices + i + 4) = lv.at(k).textureCoord.y;

		*(vertices + i + 5) = lv.at(k).normal.x;
		*(vertices + i + 6) = lv.at(k).normal.y;
		*(vertices + i + 7) = lv.at(k).normal.z;

		outfile << lv.at(k).position.x << ", ";
		outfile << lv.at(k).position.y << ", ";
		outfile << lv.at(k).position.z << ", \t";

		outfile << lv.at(k).textureCoord.x << ", ";
		outfile << lv.at(k).textureCoord.y << ", \t\t";

		outfile << lv.at(k).normal.x << ", ";
		outfile << lv.at(k).normal.y << ", ";
		outfile << lv.at(k).normal.z << ", ";
		outfile << std::endl;
		if ((k + 1) % 6 == 0) outfile << std::endl;
	}

	outfile.close();


}

void OBJLoader::CreateSSBuffer(const std::vector<OBJLoader::Vertex>& loadedVertices) {
	// SSBData
	constexpr uint32_t fill = 4;
	const uint32_t filledStride = Vertex::GetStride() + fill;
	uint32_t arrSize = filledStride * loadedVertices.size();
	this->ssbVData.vertices = new float[arrSize];
	this->ssbVData.verticesSize = arrSize;
	this->ssbVData.verticesCount = loadedVertices.size();
	const std::vector<Vertex>& lv = loadedVertices;

	float* vertices = (this->ssbVData.vertices);
	for (int i = 0, k = 0; k < lv.size(); i += filledStride, k++) {
		*(vertices + i + 0) = lv.at(k).position.x;
		*(vertices + i + 1) = lv.at(k).position.y;
		*(vertices + i + 2) = lv.at(k).position.z;
		*(vertices + i + 3) = 0.0f;

		*(vertices + i + 4) = lv.at(k).textureCoord.x;
		*(vertices + i + 5) = lv.at(k).textureCoord.y;
		*(vertices + i + 6) = 0.0f;
		*(vertices + i + 7) = 0.0f;

		*(vertices + i + 8)  = lv.at(k).normal.x;
		*(vertices + i + 9)  = lv.at(k).normal.y;
		*(vertices + i + 10) = lv.at(k).normal.z;
		*(vertices + i + 11) = 0.0f;
	}
}
