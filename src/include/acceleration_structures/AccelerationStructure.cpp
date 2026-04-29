#include "AccelerationStructure.h"
#include <algorithm>

AccelerationStructure::AccelerationStructure(const OBJLoader::MeshGeometry& meshgeo) {
    this->triangles.reserve(meshgeo.triangles.size());
    for (size_t i = 0; i < meshgeo.triangles.size(); i++) {
        Triangle tri;
        tri.oIdx = i;
        tri.v0 = meshgeo.triangles[i].indices.x;
        tri.v1 = meshgeo.triangles[i].indices.y;
        tri.v2 = meshgeo.triangles[i].indices.z;
        tri.m = meshgeo.triangles[i].indices.w;

        const glm::vec4& a = meshgeo.vertices[tri.v0].position;
        const glm::vec4& b = meshgeo.vertices[tri.v1].position;
        const glm::vec4& c = meshgeo.vertices[tri.v2].position;

        tri.bbox = AABB(glm::min(glm::min(a, b), c), glm::max(glm::max(a, b), c));
        triangles.emplace_back(tri);
    }

    this->size = 0;
    //this->tree.resize(4 * triangles.size());
}