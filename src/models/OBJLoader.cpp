#include "OBJLoader.h"

#include <fstream> // file stream
#include <sstream> // string stream
#include <ostream>

//#include "Source/Utils.h"

std::vector<std::string> split(std::string s, const std::string& separator) {
	std::vector<std::string> splittedString = std::vector<std::string>();
	while (s.find(separator) != std::string::npos) {
		std::string token = s.substr(0, s.find(separator));
		splittedString.push_back(token);
		s.erase(0, s.find(separator) + separator.length());
	}

	splittedString.push_back(s);

	return splittedString;
}

OBJLoader::OBJLoader(const char* filepath) {
	
	std::ifstream stream = std::ifstream(filepath);
	std::cout << filepath << "\n\n";

	if (!stream) { std::cerr << "ERROR: OBJLoader got a NULL directory\n"; return; }

	std::string line;

	std::string mtlFilepath = std::string(filepath);
	mtlFilepath[mtlFilepath.size() - 1] = 'l';
	mtlFilepath[mtlFilepath.size() - 2] = 't';
	mtlFilepath[mtlFilepath.size() - 3] = 'm';
	std::ifstream matStream = std::ifstream(mtlFilepath);
	
	// Load Materials
	if(matStream)
		while (getline(matStream, line)) {
			if (line.rfind("newmtl", 0) == 0) {
				this->matIdxMap[line.substr(7)] = this->mats.size();
				this->mats.push_back({});
			}
			else if (line.rfind("Ns", 0) == 0) this->mats.back().specular = stof(line.substr(3)); // Specular
			else if (line.rfind("Kd", 0) == 0) this->mats.back().albedo = LoadVectorData(line); // Albedo
			else if (line.rfind("Ke", 0) == 0) this->mats.back().emissiveColor = LoadVectorData(line); // Emissive Color
			else if (line.rfind("Ni", 0) == 0) this->mats.back().IOR = stof(line.substr(3)); // IOR
			else if (line.rfind("d", 0) == 0) this->mats.back().transmission = 1.0-stof(line.substr(2)); // Transmission
			else if (line.rfind("Pr", 0) == 0) this->mats.back().roughness = stof(line.substr(3)); // Roughness
			else if (line.rfind("pbr_emissive_power", 0) == 0) this->mats.back().roughness = stof(line.substr(19)); // Emissive Power
		}
	else {
		this->mats.push_back({});
		std::cerr << "WARNING: OBJLoader couldn't find .MTL file\n";
	}

	while (getline(stream, line)) {
		if (line.rfind("usemtl", 0) == 0) { this->currentMaterialIndex = this->matIdxMap[line.substr(7)]; continue; }
		if (line.rfind("v ",  0) == 0)  { this->positions.push_back(LoadVectorData(line)); continue; }
		if (line.rfind("vt", 0) == 0) { this->textureCoords.push_back(LoadVectorData(line)); continue; }
		if (line.rfind("vn", 0) == 0) { this->normals.push_back(LoadVectorData(line)); continue; }

		if (line.rfind("f", 0) == 0) {
			auto face = LoadFace(line);
			objVertices.insert(objVertices.end(), face.begin(), face.end());
			this->matIndices.push_back(this->currentMaterialIndex);
			continue;
		}
	}

	this->triangles.reserve(objVertices.size()/3);
	for (int i = 0; i < objVertices.size(); i += 3) {
		glm::uvec4 indices = {i, i+1, i+2, this->matIndices[i/3] };
		glm::vec3 v0 = objVertices[i].position;
		glm::vec3 v1 = objVertices[i + 1].position;
		glm::vec3 v2 = objVertices[i + 2].position;
		float area = 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));
		this->triangles.push_back({ indices, { area, 0,0,0 }});
	}
	//CreateVertexArray(objVertices);
	//CreateSSBuffer(objVertices);
}

// v, vt, vn
glm::vec4 OBJLoader::LoadVectorData(const std::string& data) {
	const std::string separator = " ";
	glm::vec4 vData = glm::vec4(0);
	auto values = split(data, separator);
	values.erase(values.begin());
	
	vData.x = stof(values.at(0));
	vData.y = stof(values.at(1));
	if (values.size() > 2) vData.z = stof(values.at(2));

	return vData;
}

// f
std::vector<OBJLoader::Vertex> OBJLoader::LoadFace(const std::string& face) {
	std::vector<Vertex> vertices;

	// f 1/2/3 4/5/6 7/8/9 => (1/2/3), (4/5/6), (7/8/9),
	const std::string separator = " ";
	auto triangle = split(face, separator);

	triangle.erase(triangle.begin());

	for (std::string vIndicies : triangle) {
		//this->triangles.push_back(stoi(split(vIndicies, "/")[0])-1);
		vertices.push_back(CreateVertex(vIndicies));
	}

	return vertices;
}

// indicies
OBJLoader::Vertex OBJLoader::CreateVertex(const std::string& indicies) {
	Vertex vertex = Vertex();

	// (1/2/3)
	std::string separator = "/";
	auto vertexIndicies = split(indicies, separator);

	int posIndex = stoi(vertexIndicies.at(0)) - 1;
	int texCoordIndex = stoi(vertexIndicies.at(1)) - 1;
	int normIndex = stoi(vertexIndicies.at(2)) - 1;

	
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
