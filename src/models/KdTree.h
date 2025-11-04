#include <vector>

class KdTree {
    struct DynamicNode {
        int32_t id;
        //AABB bounds;
        DynamicNode* left;
        DynamicNode* right;
    };

    struct Node {
        //AABB bounds;  
        int32_t left;
        int32_t right;
    };

    DynamicNode* root;
    uint32_t size;
public:

    KdTree();
    ~KdTree();

private:

// DFS
std::vector<Node> Flatten() const {
    std::vector<Node> tree(this->size);
    std::vector<DynamicNode*> stk;
    stk.reserve(this->size);
    stk.push_back(root);
    while(!stk.empty()) {
        DynamicNode* node = stk.back(); 
        stk.pop_back();
        tree[node->id] = { node->left ? node->left->id : -1, node->right ? node->right->id : -1 };
        if(node->left) stk.push_back(node->left);
        if(node->right) stk.push_back(node->right);
    }
    return tree;
}

public:

    std::vector<Node> GetTree() const {
        return this->Flatten();
    }
};