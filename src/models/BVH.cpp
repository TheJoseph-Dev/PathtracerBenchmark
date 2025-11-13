#include "BVH.h"

#include <algorithm>

BVH::BVH(const MeshGeometry& meshgeo) {
    this->triangles.reserve(meshgeo.triangles.size());
    for(auto& triIdx : meshgeo.triangles) {
        Triangle tri;
        tri.v0 = triIdx[0];
        tri.v1 = triIdx[1];
        tri.v2 = triIdx[2];

        const glm::vec3& a = meshgeo.positions[tri.v0];
        const glm::vec3& b = meshgeo.positions[tri.v1];
        const glm::vec3& c = meshgeo.positions[tri.v2];

        tri.bbox = AABB(glm::min(glm::min(a,b), c), glm::max(glm::max(a,b), c));
        triangles.push_back(tri);
    }

    this->size = 0;
    this->root = nullptr;
    this->Build(&this->root, 0, this->triangles.size());
};

int BVH::SplitMedian(const AABB& bounds, int l, int r) {
    // Choose split axis
    glm::vec3 extent = bounds.extent();
    int axis = 0;
    if (extent.y > extent.x && extent.y > extent.z) axis = 1;
    else if (extent.z > extent.x) axis = 2;

    // Compute split position (by centroid)
    float cmid = bounds.centroid()[axis];

    // Partition triangles by centroid
    auto midIter = std::partition(triangles.begin() + l, triangles.begin() + r,
        [axis, cmid](const Triangle& t) {
            glm::vec3 c = (t.bbox.min + t.bbox.max) * 0.5f;
            return c[axis] < cmid;
        });
    
    int mid = static_cast<int>(midIter - triangles.begin());
    if (mid == l || mid == r)
        mid = (l + r) >> 1; // fallback
    return mid;
}

int BVH::SplitSAH(const AABB& bounds, int l, int r) {
    return 0;
}

void BVH::Build(DynamicNode** root, int l, int r) {
    *root = new DynamicNode();
    DynamicNode* node = *root;
    node->id = this->size++;

    AABB bounds;
    for (int i = l; i < r; i++)
        bounds.expand(triangles[i].bbox);
    node->bbox = bounds;

    int triCount = r-l;
    if(triCount <= 4) {
        node->left = node->right = nullptr;
        node->triCount = triCount;
        node->triIdx = l;
        return void();
    }

    int mid = SplitMedian(bounds, l, r);

    Build(&node->left, l, mid);
    Build(&node->right, mid, r);
    node->triIdx = l;
    node->triCount = (node->left ? node->left->triCount : 0) +
                     (node->right ? node->right->triCount : 0);
}

BVH::~BVH() {
    this->FreeTree(this->root);
    this->root = nullptr;
};