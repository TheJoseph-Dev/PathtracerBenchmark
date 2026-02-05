#ifndef ACCSTRUCT_H
#define ACCSTRUCT_H
#include <glm/glm.hpp>
#include "OBJLoader.h"

class AccelerationStructure {
public:
    virtual ~AccelerationStructure() = default;

    virtual void Build() = 0;

protected:
    static constexpr uint32_t BINS = 12; // Makes SAH viable by reducing build time complexity from O(n^2) to O(BINS*n)
    static constexpr float Ct = 1.0f;
    static constexpr float Ci = 1.0f;

    static constexpr uint8_t leafSize = 8;

    struct AABB {
        glm::vec4 min;
        glm::vec4 max;

        AABB() {
            min = glm::vec4({ FLT_MAX,FLT_MAX,FLT_MAX,0 });
            max = glm::vec4({ -FLT_MAX,-FLT_MAX,-FLT_MAX,0 });
        }

        AABB(const glm::vec4& min_, const glm::vec4& max_)
            : min(min_), max(max_) {
        }

        AABB(const glm::vec3& min_, const glm::vec3& max_)
            : min({ min_.x,min_.y,min_.z,0 }), max({ max_.x,max_.y,max_.z,0 }) {
        }

        void expand(const glm::vec4& p) {
            min = glm::min(min, p);
            max = glm::max(max, p);
            min.w = max.w = 0;
        }

        void expand(const AABB& box) {
            min = glm::min(min, box.min);
            max = glm::max(max, box.max);
        }

        glm::vec4 centroid() const {
            return (min + max) * 0.5f;
        }

        glm::vec4 extent() const {
            return max - min;
        }

        float surfaceArea() const {
            glm::vec4 e = extent();
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

    struct alignas(16) TreeNode {
        int32_t left;      // index of left child (-1 if leaf)
        int32_t right;     // index of right child (-1 if leaf)
        uint32_t triIdx;   // starting triangle index (valid if leaf)
        uint32_t triCount; // number of triangles in leaf
    };

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