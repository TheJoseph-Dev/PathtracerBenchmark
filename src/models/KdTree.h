#ifndef KDTREE_H
#define KDTREE_H
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "OBJLoader.h"
#include "AccelerationStructure.h"

class KdTree: public AccelerationStructure {

    OBJLoader::MeshGeometry meshgeo;
public:
    struct alignas(16) Node : public TreeNode {
        float splitPos;
        uint32_t axis;
    };

    KdTree(const OBJLoader::MeshGeometry& meshgeo);
    ~KdTree() override = default;

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

    const std::vector<OBJLoader::Vertex>& GetVertices() const {
        return this->meshgeo.vertices;
    }
    
    const std::vector<OBJLoader::Triangle>& GetTriangles() const {
        return this->meshgeo.triangles;
    }
private:

    enum EventType {
        START = 0,
        END = 2
    };

    struct Event {
        float pos;
        uint32_t idx;
        EventType type;

        bool operator<(const Event& other) const {
            if (pos != other.pos) return pos < other.pos;
            return this->type > other.type;
        }
    };

    struct SplitInfo {
        int32_t nl;
        int32_t nr;
        float split;
        uint32_t axis;
        bool shouldLeaf = false;
    };

    std::vector<Node> tree;
    std::vector<Event> events[3];
    std::vector<uint32_t> indexArena;
    std::vector<uint32_t> outBuffer;
    SplitInfo SplitSAH(const AABB& bounds, int l, int r);
    uint32_t Build(int l, int r, const AABB& bounds);
};

#endif