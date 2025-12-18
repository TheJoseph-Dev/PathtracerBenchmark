#include "BVH.h"

#include <algorithm>

BVH::BVH(const OBJLoader::MeshGeometry& meshgeo) {
    static_assert(sizeof(Node) == 48);
    this->triangles.reserve(meshgeo.triangles.size()/3);
    for (size_t i = 0; i < meshgeo.triangles.size(); i += 3) {
        Triangle tri;
        tri.v0 = meshgeo.triangles[i];
        tri.v1 = meshgeo.triangles[i + 1];
        tri.v2 = meshgeo.triangles[i + 2];

        const glm::vec4& a = meshgeo.vertices[tri.v0].position;
        const glm::vec4& b = meshgeo.vertices[tri.v1].position;
        const glm::vec4& c = meshgeo.vertices[tri.v2].position;

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

// Cost: C = Ct + Ci * (SAl/SA * Nl + SAr/SA * Nr)
// Ct = traversal cost
// Ci = intersction cost
// SA = current node surface
// SAl = Left child surface area; SAr = Right child surface area
// Nl = number of triangles at left; Nr = number of triangles at right
int BVH::SplitSAH(const AABB& bounds, int l, int r) {
    const int count = r - l;
    if (count <= leafSize)
        return (l + r) >> 1;

    float bestCost = std::numeric_limits<float>::infinity();
    int bestAxis = -1;
    int bestSplit = -1;

    const float SA = bounds.surfaceArea();

    for (int axis = 0; axis < 3; axis++) {
        struct Bin {
            AABB bounds;
            int count = 0;
        };

        Bin bins[BINS];

        float minAxis = bounds.min[axis];
        float maxAxis = bounds.max[axis];
        float scale = BINS / (maxAxis - minAxis + 1e-5f);

        // 1. Fill bins
        // Discretizes all the n-1 splits in only some bins so that SAH becomes viable
        for (int i = l; i < r; i++) {
            const AABB& b = triangles[i].bbox;
            float center = b.centroid()[axis];
            int32_t bin = int32_t((center - minAxis) * scale);
            bin = std::min<int32_t>(BINS - 1, std::max(0, bin));

            bins[bin].count++;
            bins[bin].bounds.expand(b);
        }

        // 2. Prefix sums
        // These will be used to test different bins splits
        AABB leftBounds[BINS];
        int leftCount[BINS] = {};

        AABB rightBounds[BINS];
        int rightCount[BINS] = {};

        AABB acc;
        int cnt = 0;
        for (int i = 0; i < BINS; i++) {
            acc.expand(bins[i].bounds);
            cnt += bins[i].count;
            leftBounds[i] = acc;
            leftCount[i] = cnt;
        }

        acc = {};
        cnt = 0;
        for (int i = BINS - 1; i >= 0; i--) { // Different direction to make calc easier
            acc.expand(bins[i].bounds);
            cnt += bins[i].count;
            rightBounds[i] = acc;
            rightCount[i] = cnt;
        }

        // 3. Evaluate SAH
        // Test which split has the lower cost
        for (int i = 0; i < BINS - 1; i++) {
            if (!leftCount[i] || !rightCount[i + 1])
                continue;
            float SAlC = leftBounds[i].surfaceArea() / SA * leftCount[i];
            float SArC = rightBounds[i + 1].surfaceArea() / SA * rightCount[i + 1];
            float cost = Ct + Ci * ( SAlC + SArC );

            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestSplit = i;
            }
        }
    }

    // 4. Fallback
    if (bestAxis == -1)
        return (l + r) >> 1;

    // 5. Partition primitives by best axis
    float splitPos = bounds.min[bestAxis] + (bounds.max[bestAxis] - bounds.min[bestAxis]) * (float(bestSplit + 1) / BINS);

    auto mid = std::partition(triangles.begin() + l, triangles.begin() + r,
        [&](const Triangle& tri) {
            return tri.bbox.centroid()[bestAxis] < splitPos;
        }
    );

    int splitIndex = int(mid - triangles.begin());
    if (splitIndex == l || splitIndex == r)
        splitIndex = (l + r) >> 1;

    return splitIndex;
}

void BVH::Build(DynamicNode** root, int l, int r) {
    *root = new DynamicNode();
    DynamicNode* node = *root;
    node->id = this->size++;

    AABB bounds;
    for (int i = l; i < r; i++)
        bounds.expand(triangles[i].bbox);
    node->bbox = bounds;

    const int triCount = r-l;
    if(triCount <= leafSize) {
        node->left = node->right = nullptr;
        node->triCount = triCount;
        node->triIdx = l;
        return void();
    }

    int mid = SplitSAH(bounds, l, r);

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