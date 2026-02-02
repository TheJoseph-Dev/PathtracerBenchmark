#include "KdTreeBinned.h"
#include <algorithm>
#include <queue>


KdTreeBinned::KdTreeBinned(const OBJLoader::MeshGeometry& meshgeo): AccelerationStructure(meshgeo) {
	static_assert(alignof(Node) == 16);
    static_assert(std::is_trivially_copyable_v<Node>);
    size_t triCount = this->triangles.size();

	this->tree.resize(4 * triCount / leafSize);
    indexArena.resize(triCount << 3); // ~ N log N
    for (size_t i = 0; i < triCount; i++) indexArena[i] = i;

    this->outBuffer.reserve(triCount << 1);
};


void KdTreeBinned::Build() {
    this->size = 0;
    this->Build(0, this->triangles.size());
    this->tree.resize(this->size);
}

// Cost: C = Ct + Ci * (SAl/SA * Nl + SAr/SA * Nr)
// Ct = traversal cost
// Ci = intersction cost
// SA = current node surface
// SAl = Left child surface area; SAr = Right child surface area
// Nl = number of triangles at left; Nr = number of triangles at right
KdTreeBinned::SplitInfo KdTreeBinned::SplitSAH(const AABB& bounds, int l, int r) {
    const float SA = bounds.surfaceArea();

    int bestSplit = -1;
    float minCost = std::numeric_limits<float>::max();
    uint32_t bestAxis = 0;
    
    auto computeSAH = [&](int axis) {
        Bin bins[BINS] = {};

        float minAxis = bounds.min[axis];
        float maxAxis = bounds.max[axis];
        float scale = BINS / (maxAxis - minAxis + 1e-5f);

        for (int i = l; i < r; i++) {
            const Triangle& tri = this->triangles[indexArena[i]];
            int bs = std::clamp<int>((tri.bbox.min[axis] - minAxis) * scale, 0, BINS - 1);
            int bf = std::clamp<int>((tri.bbox.max[axis] - minAxis) * scale, 0, BINS - 1);
            for (int b = bs; b <= bf; b++) {
                bins[b].bounds.expand(tri.bbox);
                bins[b].count++;
            }
        }

        AABB leftBounds[BINS];
        int leftCount[BINS] = {};

        AABB rightBounds[BINS];
        int rightCount[BINS] = {};

        AABB acc = {};
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
            const float cost = Ct + Ci * (leftBounds[i].surfaceArea() * leftCount[i] + rightBounds[i + 1].surfaceArea() * rightCount[i + 1]) / SA;
            if (cost < minCost) {
                minCost = cost;
                bestSplit = i;
                bestAxis = axis;
            }
        }
    };

    for (int axis = 0; axis < 3; axis++) computeSAH(axis);

    //if (minCost >= Ct * (r-l)) // SAH wasn't effective enough => cheaper to make a leaf
    //    return { -1, -1, 0, 0, true };
    
    auto ternary_partition = [this, bestAxis](int l, int r, float splitPos) -> std::array<int, 2> {
        int lEnd = l, rBegin = r;
        for (int i = l; i < rBegin; i++) {
            if (triangles[indexArena[i]].bbox.max[bestAxis] <= splitPos) {
                std::swap(indexArena[i], indexArena[lEnd]);
                lEnd++;
            }
            else if (triangles[indexArena[i]].bbox.min[bestAxis] >= splitPos) {
                rBegin--;
                std::swap(indexArena[i], indexArena[rBegin]);
                i--;
            }
        }
        return {lEnd, rBegin};
    };

    int lEnd = 0, rBegin = 0;
    float splitPos = 0;
    bool degenerated = false;
    int tryCnt = 2;
    do {
        const float binWidth = (bounds.max[bestAxis] - bounds.min[bestAxis]) / BINS;
        splitPos = bounds.min[bestAxis] + (bestSplit + 1) * binWidth;

        auto [pl, pr] = ternary_partition(l, r, splitPos);
        lEnd = pl; rBegin = pr;
        degenerated = lEnd == l || rBegin == r;
        if (degenerated && tryCnt) {
            minCost = std::numeric_limits<float>::max();
            computeSAH((bestAxis + 1) % 3);
        }
    } while(tryCnt-- && degenerated);


    if (degenerated) {
        int axis = ((bounds.extent().x > bounds.extent().y && bounds.extent().x > bounds.extent().z) ? 0 : ((bounds.extent().y > bounds.extent().z) ? 1 : 2));
        splitPos = bounds.centroid()[axis];
        int mid = int(std::partition(indexArena.begin() + l, indexArena.begin() + r, [&](const uint32_t idx) {return this->triangles[idx].bbox.centroid()[axis] < splitPos; }) - indexArena.begin());
        if (mid == l || mid == r) return { -1,-1,0,0,true };
        lEnd = rBegin = mid;
        bestAxis = axis;
    }

    return { lEnd, rBegin, splitPos, bestAxis };
}

uint32_t KdTreeBinned::Build(int l, int r) {
    uint32_t nodeIdx = this->size++;
    Node& node = this->tree[nodeIdx];

    AABB bounds;
    for (int i = l; i < r; i++)
        bounds.expand(triangles[indexArena[i]].bbox);

    const int triCount = r - l;
    auto makeLeaf = [&]() {
        node.left = node.right = -1;
        node.triCount = triCount;
        node.triIdx = outBuffer.size();
        outBuffer.insert(outBuffer.end(), indexArena.begin() + l, indexArena.begin() + r);
        return nodeIdx;
    };

    if (triCount <= leafSize) return makeLeaf();

    auto [lEnd, rBegin, split, bestAxis, shouldLeaf] = SplitSAH(bounds, l, r);
    if (shouldLeaf) return makeLeaf();

    node.splitPos = split;
    node.axis = bestAxis;

    //indexArena.insert(indexArena.end(), indexArena.begin() + l, indexArena.begin() + rBegin);
    std::memcpy(&indexArena[r], &indexArena[l], (rBegin - l) * sizeof(uint32_t));
    node.left = Build(r, r + (rBegin - l));

    std::memcpy(&indexArena[r], &indexArena[lEnd], (r - lEnd) * sizeof(uint32_t));
    node.right = Build(r, r + (r - lEnd));

    node.triIdx = 0;
    node.triCount = 0;
    return nodeIdx;
}

int KdTreeBinned::GetHeight() const {
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