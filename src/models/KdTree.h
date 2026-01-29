#ifndef KDTREE_H
#define KDTREE_H
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "OBJLoader.h"
#include "AccelerationStructure.h"

class KdTree: public AccelerationStructure {

    struct Node: public TreeNode {
        uint32_t axis;
        float splitPos;
    };

    std::vector<Node> tree;
    
public:

    KdTree(const OBJLoader::MeshGeometry& meshgeo);
    ~KdTree() override = default;

    void Build() override;

    int GetHeight() const;

    void Print() const {
        //for (size_t i = 0; i < tree.size(); i++) printf("%d | (%d, %d | %d, %d) => bbmin(%.2f %.2f %.2f) bbmax(%.2f %.2f %.2f)\n", i, tree[i].left, tree[i].right, tree[i].triIdx, tree[i].triCount, tree[i].bbox.min.x, tree[i].bbox.min.y, tree[i].bbox.min.z, tree[i].bbox.max.x, tree[i].bbox.max.y, tree[i].bbox.max.z);
    }

    const std::vector<Node>& GetTree() const {
        return this->tree;
    }

private:

    std::vector<size_t> indexArena;
    std::array<int, 2> SplitSAH(const AABB& bounds, int l, int r, uint32_t axis);
    uint32_t Build(int l, int r, uint32_t axis);
};

#endif