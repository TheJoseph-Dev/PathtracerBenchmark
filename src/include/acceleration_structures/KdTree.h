#ifndef KDTREE_H
#define KDTREE_H
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "include/io/OBJLoader.h"
#include "AccelerationStructure.h"

class KdTree: public AccelerationStructure {

    static constexpr float Ct = 1.0f;
    static constexpr float Ci = 1.0f;

    static constexpr uint8_t leafSize = 8;

    //OBJLoader::MeshGeometry meshgeo;
public:
    struct alignas(16) Node {
        union {
            float splitPos;
            uint32_t triIdx;
        };
        union {
            uint32_t axis;
            uint32_t triCount;
        };
        int32_t left;
        int32_t right;
    };

    KdTree(const OBJLoader::MeshGeometry& meshgeo);
    ~KdTree() override = default;

    void Build() override;

    int GetHeight() const;

    void Print() const {
        for (size_t i = 0; i < tree.size(); i++) {
            const Node& node = tree[i];
            if (node.left == -1)
                printf("%d | leaf (idx: %d, cnt: %d)\n", i, node.triIdx, node.triCount);
            else
                printf("%d | inner (lidx: %d, ridx: %d) => (axis: %d, split: %.2f)\n", i, node.left, node.right, node.axis, node.splitPos);
        }
    }

    const std::vector<Node>& GetTree() const {
        return this->tree;
    }

    const std::vector<uint32_t>& GetIndices() const {
        return this->outBuffer;
    }

    /*
    const std::vector<OBJLoader::Vertex>& GetVertices() const {
        return this->meshgeo.vertices;
    }
    
    const std::vector<OBJLoader::Triangle>& GetTriangles() const {
        return this->meshgeo.triangles;
    }
    */
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