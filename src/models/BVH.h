#include <vector>
#include <glm/glm.hpp>
#include "OBJLoader.h"

class BVH {

    struct AABB {
        glm::vec4 min;
        glm::vec4 max;

        AABB() {
            min = glm::vec4(FLT_MAX);
            max = glm::vec4(-FLT_MAX);
        }

        AABB(const glm::vec4& min_, const glm::vec4& max_) 
            : min(min_), max(max_) {}

        void expand(const glm::vec4& p) {
            min = glm::min(min, p);
            max = glm::max(max, p);
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
        uint32_t v0, v1, v2;
        AABB bbox;
    };

    std::vector<Triangle> triangles;

    struct DynamicNode {
        int32_t id;
        AABB bbox;
        uint32_t triIdx;
        uint32_t triCount;
        DynamicNode* left;
        DynamicNode* right;
    };

    struct Node {
        AABB bbox;  
        int32_t left;      // index of left child (-1 if leaf)
        int32_t right;     // index of right child (-1 if leaf)
        uint32_t triIdx;   // starting triangle index (valid if leaf)
        uint32_t triCount; // number of triangles in leaf
    };

    DynamicNode* root;
    uint32_t size;

public:

    BVH(const OBJLoader::MeshGeometry& meshgeo);
    ~BVH();

private:

int SplitMedian(const AABB& bounds, int l, int r);
int SplitSAH(const AABB& bounds, int l, int r);

void Build(DynamicNode** root, int l, int r);

// DFS
std::vector<Node> Flatten() const {
    std::vector<Node> tree(this->size);
    std::vector<DynamicNode*> stk;
    stk.reserve(this->size);
    stk.push_back(root);
    while(!stk.empty()) {
        DynamicNode* node = stk.back(); 
        stk.pop_back();
        tree[node->id] = { node->bbox, node->left ? node->left->id : -1, node->right ? node->right->id : -1, node->triIdx, node->triCount };
        if(node->left) stk.push_back(node->left);
        if(node->right) stk.push_back(node->right);
    }
    return tree;
}

void FreeTree(DynamicNode* root) {
    if(!root) return;
    FreeTree(root->right);
    FreeTree(root->left);
    delete root;
}

public:

    std::vector<Node> GetTree() const {
        return this->Flatten();
    }
};