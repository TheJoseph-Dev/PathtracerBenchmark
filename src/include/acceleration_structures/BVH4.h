#ifndef BVH4_H
#define BVH4_H
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "include/io/OBJLoader.h"
#include "AccelerationStructure.h"

class BVH4 final : public AccelerationStructure {

    static constexpr uint32_t BINS = 16;
    static constexpr float Ct = 1.0f;
    static constexpr float Ci = 1.0f;

    static constexpr uint8_t leafSize = 8;

public:
    struct alignas(16) Node {
        int32_t child[4];
        uint32_t triIdx;
        uint32_t triCount;
        glm::vec3 bboxMin[4];
        glm::vec3 bboxMax[4];
        int32_t pad[2];
    };

    BVH4(const OBJLoader::MeshGeometry& meshgeo);
    ~BVH4() override = default;

    void Build() override;

    int GetHeight() const;

    void Print() const {
        for (size_t i = 0; i < tree.size(); i++) {
            printf("%d | ch(%d,%d,%d,%d) tri(%u,%u)\n", i,
                tree[i].child[0], tree[i].child[1], tree[i].child[2], tree[i].child[3],
                tree[i].triIdx, tree[i].triCount);
        }
    }

    const std::vector<Node>& GetTree() const {
        return this->tree;
    }

private:
    struct BuildRange {
        int l;
        int r;
        AABB bounds;
    };

    std::vector<Node> tree;

    int SplitSAH(const AABB& bounds, int l, int r);
    uint32_t Build(int l, int r);

    void ComputeBounds(int l, int r, AABB& outBounds) const;
    bool TrySplitRange(const BuildRange& src, BuildRange& left, BuildRange& right);
};

#endif
