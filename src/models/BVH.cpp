#include "BVH.h"

#include <algorithm>
#include <queue>

BVH::BVH(const OBJLoader::MeshGeometry& meshgeo) {
    static_assert(sizeof(Node) == 48);
    this->triangles.reserve(meshgeo.triangles.size());
    for (size_t i = 0; i < meshgeo.triangles.size(); i++) {
        Triangle tri;
        tri.oIdx = i;
        tri.v0 = meshgeo.triangles[i].x;
        tri.v1 = meshgeo.triangles[i].y;
        tri.v2 = meshgeo.triangles[i].z;

        const glm::vec4& a = meshgeo.vertices[tri.v0].position;
        const glm::vec4& b = meshgeo.vertices[tri.v1].position;
        const glm::vec4& c = meshgeo.vertices[tri.v2].position;

        tri.bbox = AABB(glm::min(glm::min(a, b), c), glm::max(glm::max(a, b), c));
        triangles.push_back(tri);
    }

    this->size = 0;
    this->tree.resize(4*triangles.size());
    // Build => Made build public so it's easier to benchmark
};

void BVH::Build() {
    this->Build(0, this->triangles.size());
}

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

uint32_t BVH::Build(int l, int r) {
    uint32_t nodeIdx = this->size++;
    Node& node = this->tree[nodeIdx];

    AABB bounds;
    for (int i = l; i < r; i++)
        bounds.expand(triangles[i].bbox);
    node.bbox = bounds;

    const int triCount = r-l;
    if(triCount <= leafSize) {
        node.left = node.right = -1;
        node.triCount = triCount;
        node.triIdx = l;
        return nodeIdx;
    }

    int mid = SplitSAH(bounds, l, r);

    node.left = Build(l, mid);
    node.right = Build(mid, r);
    node.triIdx = 0;
    node.triCount = 0;
    return nodeIdx;
}

int BVH::GetHeight() const {
    if (tree.empty()) return 0;
    int h = 0;
    std::queue<int32_t> q;
    q.push(0);
    while (!q.empty()) {
        size_t sz = q.size();
        while (sz--) {
            int32_t node = q.front();
            q.pop();
            if (this->tree[node].left >= 0) q.push(this->tree[node].left);
            if (this->tree[node].right >= 0) q.push(this->tree[node].right);
        }
        h++;
    }
    return h;
}