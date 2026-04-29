#ifndef KDTREE_BINNED_H
#define KDTREE_BINNED_H
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "include/io/OBJLoader.h"
#include "AccelerationStructure.h"

class KdTreeBinned final : public AccelerationStructure {
    
    static constexpr uint32_t BINS = 16; // Makes SAH viable by reducing build time complexity from O(n^2) to O(BINS*n)
    static constexpr float Ct = 1.0f;
    static constexpr float Ci = 1.0f;

    static constexpr uint8_t leafSize = 8;

public:
    struct alignas(16) Node : public TreeNode {
        float splitPos;
        uint32_t axis;
    };

    KdTreeBinned(const OBJLoader::MeshGeometry& meshgeo);
    ~KdTreeBinned() override = default;

    void Build() override;

    int GetHeight() const;

    void Print() const {
        for (size_t i = 0; i < tree.size(); i++) printf("%d | (lidx: %d, ridx: %d | idx: %d, cnt: %d) => (axis: %d, split: %.2f)\n", i, tree[i].left, tree[i].right, tree[i].triIdx, tree[i].triCount, tree[i].axis, tree[i].splitPos);
    }

    const std::vector<Node>& GetTree() const {
        return this->tree;
    }

    const std::vector<uint32_t>& GetIndices() const {
        return this->outBuffer;
    }

private:

    std::vector<Node> tree;

    std::vector<uint32_t> indexArena;
    std::vector<uint32_t> outBuffer; // Needed to avoid uploading the whole arena to the GPU

    struct SplitInfo {
        int32_t lEnd;
        int32_t rBegin;
        float splitPos;
        uint32_t axis;
        bool shouldLeaf = false;
    };

    SplitInfo SplitSAH(const AABB& bounds, int l, int r);
    SplitInfo SplitCentroid(const AABB& bounds, int l, int r);
    uint32_t Build(int l, int r, const AABB& bounds);
};

#endif