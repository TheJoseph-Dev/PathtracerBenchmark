#ifndef ACCSTRUCT_H
#define ACCSTRUCT_H
#include <glm/glm.hpp>
#include "include/io/OBJLoader.h"

class AccelerationStructure {
public:
    virtual ~AccelerationStructure() = default;

    virtual void Build() = 0;

protected:
    struct AABB {
        glm::vec3 min;
        glm::vec3 max;

        AABB() {
            min = glm::vec3({ FLT_MAX,FLT_MAX,FLT_MAX});
            max = glm::vec3({ -FLT_MAX,-FLT_MAX,-FLT_MAX});
        }

        AABB(const glm::vec4& min_, const glm::vec4& max_)
            : min(min_), max(max_) {
        }

        AABB(const glm::vec3& min_, const glm::vec3& max_)
            : min(min_), max(max_) {
        }

        void expand(const glm::vec3& p) {
            min = glm::min(min, p);
            max = glm::max(max, p);
        }

        void expand(const AABB& box) {
            min = glm::min(min, box.min);
            max = glm::max(max, box.max);
        }

        glm::vec3 centroid() const {
            return (min + max) * 0.5f;
        }

        glm::vec3 extent() const {
            return max - min;
        }

        float surfaceArea() const {
            glm::vec3 e = extent();
            return 2.0f * (e.x * e.y + e.x * e.z + e.y * e.z);
        }
    };

    struct Triangle {
        uint32_t oIdx;
        uint32_t v0, v1, v2;
        uint32_t m;
        AABB bbox;
    };

    uint32_t size;

    AccelerationStructure(const OBJLoader::MeshGeometry& meshgeo);

    std::vector<Triangle> triangles;

    struct Bin {
        AABB bounds;
        int count = 0;
    };

public:
    std::vector<Triangle> GetTriangles() const {
        return this->triangles;
    };
};
#endif