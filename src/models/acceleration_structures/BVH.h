#ifndef BVH_H
#define BVH_H
#include <vector>
#include <glm/glm.hpp>
#include "models/io/OBJLoader.h"
#include "AccelerationStructure.h"

class BVH final : public AccelerationStructure {

    static constexpr uint32_t BINS = 16; // Makes SAH viable by reducing build time complexity from O(n^2) to O(BINS*n)
    static constexpr float Ct = 1.0f;
    static constexpr float Ci = 1.0f;

    static constexpr uint8_t leafSize = 8;

public:
    struct alignas(16) Node : public TreeNode {
        AABB bbox;
    };

    BVH(const OBJLoader::MeshGeometry& meshgeo);
    ~BVH() override = default;

    void Build() override;

    int GetHeight() const;

    void Print() const {
        for (size_t i = 0; i < tree.size(); i++) printf("%d | (%d, %d | %d, %d) => bbmin(%.2f %.2f %.2f) bbmax(%.2f %.2f %.2f)\n", i, tree[i].left, tree[i].right, tree[i].triIdx, tree[i].triCount, tree[i].bbox.min.x, tree[i].bbox.min.y, tree[i].bbox.min.z, tree[i].bbox.max.x, tree[i].bbox.max.y, tree[i].bbox.max.z);
    }

    const std::vector<Node>& GetTree() const {
        return this->tree;
    }

private:

    std::vector<Node> tree;

    int SplitMedian(const AABB& bounds, int l, int r);
    int SplitSAH(const AABB& bounds, int l, int r);

    uint32_t Build(int l, int r);
};

#endif