#include "BVH4.h"

#include <algorithm>
#include <cstddef>
#include <queue>

BVH4::BVH4(const OBJLoader::MeshGeometry& meshgeo) : AccelerationStructure(meshgeo) {
    static_assert(alignof(Node) == 16);
    static_assert(std::is_trivially_copyable_v<Node>);
    static_assert(sizeof(Node) == 128);

    const size_t reserveCount = std::max<size_t>(1, this->triangles.size() * 2);
    this->tree.resize(reserveCount);
}

void BVH4::Build() {
    this->size = 0;
    this->Build(0, static_cast<int>(this->triangles.size()));
    this->tree.resize(this->size);
}

void BVH4::ComputeBounds(int l, int r, AABB& outBounds) const {
    outBounds = {};
    for (int i = l; i < r; i++)
        outBounds.expand(this->triangles[i].bbox);
}

bool BVH4::TrySplitRange(const BuildRange& src, BuildRange& left, BuildRange& right) {
    if (src.r - src.l <= leafSize)
        return false;

    const int mid = SplitSAH(src.bounds, src.l, src.r);
    if (mid <= src.l || mid >= src.r)
        return false;

    left = { src.l, mid, {} };
    right = { mid, src.r, {} };
    ComputeBounds(left.l, left.r, left.bounds);
    ComputeBounds(right.l, right.r, right.bounds);
    return true;
}

int BVH4::SplitSAH(const AABB& bounds, int l, int r) {
    float bestCost = std::numeric_limits<float>::infinity();
    int bestAxis = -1;
    int bestSplit = -1;

    const float SA = bounds.surfaceArea();

    for (int axis = 0; axis < 3; axis++) {
        Bin bins[BINS];

        float minAxis = bounds.min[axis];
        float maxAxis = bounds.max[axis];
        float scale = BINS / (maxAxis - minAxis + 1e-5f);

        for (int i = l; i < r; i++) {
            const AABB& b = triangles[i].bbox;
            float center = b.centroid()[axis];
            int32_t bin = int32_t((center - minAxis) * scale);
            bin = std::min<int32_t>(BINS - 1, std::max(0, bin));

            bins[bin].count++;
            bins[bin].bounds.expand(b);
        }

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
        for (int i = BINS - 1; i >= 0; i--) {
            acc.expand(bins[i].bounds);
            cnt += bins[i].count;
            rightBounds[i] = acc;
            rightCount[i] = cnt;
        }

        for (int i = 0; i < BINS - 1; i++) {
            if (!leftCount[i] || !rightCount[i + 1])
                continue;

            float SAlC = leftCount[i] * leftBounds[i].surfaceArea() / SA;
            float SArC = rightCount[i + 1] * rightBounds[i + 1].surfaceArea() / SA;
            float cost = Ct + Ci * (SAlC + SArC);

            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestSplit = i;
            }
        }
    }

    if (bestAxis == -1)
        return (l + r) >> 1;

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

uint32_t BVH4::Build(int l, int r) {
    uint32_t nodeIdx = this->size++;
    Node& node = this->tree[nodeIdx];

    node.triIdx = 0;
    node.triCount = 0;
    for (int i = 0; i < 4; i++) {
        node.child[i] = -1;
        node.bboxMin[i] = glm::vec4(0.0f);
        node.bboxMax[i] = glm::vec4(0.0f);
    }

    const int triCount = r - l;
    if (triCount <= leafSize) {
        node.triIdx = l;
        node.triCount = triCount;
        return nodeIdx;
    }

    AABB parentBounds;
    ComputeBounds(l, r, parentBounds);

    std::array<BuildRange, 4> ranges{};
    uint32_t rangeCount = 1;
    ranges[0] = { l, r, parentBounds };

    while (rangeCount < 4) {
        int bestRange = -1;
        int maxTris = 0;

        for (uint32_t i = 0; i < rangeCount; i++) {
            int count = ranges[i].r - ranges[i].l;
            if (count > maxTris && count > leafSize) {
                maxTris = count;
                bestRange = static_cast<int>(i);
            }
        }

        if (bestRange < 0)
            break;

        BuildRange left{}, right{};
        if (!TrySplitRange(ranges[bestRange], left, right))
            break;

        ranges[bestRange] = left;
        ranges[rangeCount++] = right;
    }

    if (rangeCount == 1) {
        int mid = SplitSAH(parentBounds, l, r);
        if (mid <= l || mid >= r) {
            node.triIdx = l;
            node.triCount = triCount;
            return nodeIdx;
        }

        BuildRange left{ l, mid, {} };
        BuildRange right{ mid, r, {} };
        ComputeBounds(left.l, left.r, left.bounds);
        ComputeBounds(right.l, right.r, right.bounds);

        ranges[0] = left;
        ranges[1] = right;
        rangeCount = 2;
    }

    glm::vec3 e = parentBounds.extent();
    int axis = 0;
    if (e.y > e.x && e.y > e.z) axis = 1;
    else if (e.z > e.x) axis = 2;

    std::sort(ranges.begin(), ranges.begin() + rangeCount,
        [axis](const BuildRange& a, const BuildRange& b) {
            return a.bounds.centroid()[axis] < b.bounds.centroid()[axis];
        }
    );

    for (uint32_t i = 0; i < rangeCount; i++) {
        node.child[i] = static_cast<int32_t>(Build(ranges[i].l, ranges[i].r));
        node.bboxMin[i] = ranges[i].bounds.min;
        node.bboxMax[i] = ranges[i].bounds.max;
    }

    return nodeIdx;
}

int BVH4::GetHeight() const {
    if (tree.empty()) return 0;
    int h = 0;
    std::queue<int32_t> q;
    q.push(0);

    while (!q.empty()) {
        size_t sz = q.size();
        while (sz--) {
            int32_t nodeIdx = q.front();
            q.pop();

            const Node& n = this->tree[nodeIdx];
            for (int i = 0; i < 4; i++) {
                if (n.child[i] >= 0)
                    q.push(n.child[i]);
            }
        }
        h++;
    }

    return h;
}
