#ifndef BVH_H
#define BVH_H
#include <vector>
#include <glm/glm.hpp>
#include "OBJLoader.h"

class BVH {
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
            : min(min_), max(max_) {}

        AABB(const glm::vec3& min_, const glm::vec3& max_)
            : min({min_.x,min_.y,min_.z,0}), max({max_.x,max_.y,max_.z,0}) {}

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

    struct Node {
        AABB bbox;  
        int32_t left;      // index of left child (-1 if leaf)
        int32_t right;     // index of right child (-1 if leaf)
        uint32_t triIdx;   // starting triangle index (valid if leaf)
        uint32_t triCount; // number of triangles in leaf
    };

    uint32_t size;

public:

    BVH(const OBJLoader::MeshGeometry& meshgeo);
    ~BVH() {};

    struct Triangle {
        uint32_t oIdx;
        uint32_t v0, v1, v2;
        AABB bbox;
    };

    void Build();

private:

int SplitMedian(const AABB& bounds, int l, int r);
int SplitSAH(const AABB& bounds, int l, int r);

std::vector<Triangle> triangles;
std::vector<Node> tree;

uint32_t Build(int l, int r);

public:

    std::vector<Node> GetTree() const {
        return this->tree;
    }

    int GetHeight() const;

    void Print() const {
        for (size_t i = 0; i < tree.size(); i++) printf("%d | (%d, %d | %d, %d) => bbmin(%.2f %.2f %.2f) bbmax(%.2f %.2f %.2f)\n", i, tree[i].left, tree[i].right, tree[i].triIdx, tree[i].triCount, tree[i].bbox.min.x, tree[i].bbox.min.y, tree[i].bbox.min.z, tree[i].bbox.max.x, tree[i].bbox.max.y, tree[i].bbox.max.z);
    }

    std::vector<Triangle> GetTriangles() const {
        return this->triangles;
    }
};

#endif