#ifndef TESTS_MESH_FACTORY_H
#define TESTS_MESH_FACTORY_H

#include <vector>
#include <glm/glm.hpp>
#include "include/io/OBJLoader.h"

namespace TestFixtures {

inline OBJLoader::MeshGeometry MakeSingleTriangle() {
    OBJLoader::Vertex v0{};
    v0.position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    OBJLoader::Vertex v1{};
    v1.position = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    OBJLoader::Vertex v2{};
    v2.position = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

    OBJLoader::Triangle tri{};
    tri.indices = glm::uvec4(0, 1, 2, 0);
    tri.area = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);

    OBJLoader::MeshGeometry geo;
    geo.vertices = { v0, v1, v2 };
    geo.triangles = { tri };
    return geo;
}

inline OBJLoader::MeshGeometry MakeTwoTriangles() {
    OBJLoader::Vertex v0{};
    v0.position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    OBJLoader::Vertex v1{};
    v1.position = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    OBJLoader::Vertex v2{};
    v2.position = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    OBJLoader::Vertex v3{};
    v3.position = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);

    OBJLoader::Triangle t0{};
    t0.indices = glm::uvec4(0, 1, 2, 0);
    t0.area = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);

    OBJLoader::Triangle t1{};
    t1.indices = glm::uvec4(1, 3, 2, 0);
    t1.area = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);

    OBJLoader::MeshGeometry geo;
    geo.vertices = { v0, v1, v2, v3 };
    geo.triangles = { t0, t1 };
    return geo;
}

} // namespace TestFixtures

#endif
