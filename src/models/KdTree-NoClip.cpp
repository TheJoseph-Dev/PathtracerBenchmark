/*
#include "KdTree.h"
#include <algorithm>
#include <queue>


KdTree::KdTree(const OBJLoader::MeshGeometry& meshgeo): AccelerationStructure(meshgeo) {
	static_assert(sizeof(Node) == 24);
    static_assert(std::is_trivially_copyable_v<Node>);
    size_t triCount = this->triangles.size();

	this->tree.resize(4 * triCount / leafSize);
    std::vector<size_t> indexArena;
    indexArena.reserve(triCount << 2); // ~ N log N
    for (size_t i = 0; i < triCount; i++) indexArena.push_back(i);
};


void KdTree::Build() {
    this->size = 0;
    this->Build(0, this->triangles.size(), 0);
    this->tree.resize(this->size);
}

// Cost: C = Ct + Ci * (SAl/SA * Nl + SAr/SA * Nr)
// Ct = traversal cost
// Ci = intersction cost
// SA = current node surface
// SAl = Left child surface area; SAr = Right child surface area
// Nl = number of triangles at left; Nr = number of triangles at right
std::array<int, 2> KdTree::SplitSAH(const AABB& bounds, int l, int r, uint32_t axis) {
    const float SA = bounds.surfaceArea();

    int bestSplit = -1;
    float minCost = std::numeric_limits<float>::max();

    Bin bins[BINS] = {};

    float minAxis = bounds.min[axis];
    float maxAxis = bounds.max[axis];
    float scale = BINS / (maxAxis - minAxis + 1e-5f);

    for (int i = l; i < r; i++) {
        const Triangle& tri = this->triangles[indexArena[i]];
        int bs = std::clamp<int>((tri.bbox.min[axis]-minAxis) * scale, 0, BINS-1);
        int bf = std::clamp<int>((tri.bbox.max[axis]-minAxis) * scale, 0, BINS-1);
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
        const float cost = Ct + Ci * (leftBounds[i].surfaceArea() * leftCount[i] + rightBounds[i+1].surfaceArea() * rightCount[i+1])/SA;
        if (cost < minCost) {
            minCost = cost;
            bestSplit = i;
        }
    }

    if (minCost >= Ct * (r-l)) // SAH wasn't effective enough
        return { (l + r) >> 1, (l + r) >> 1 };
    
    const float binWidth = (maxAxis - minAxis) / BINS;
    float splitPos = minAxis + (bestSplit + 1) * binWidth;

    auto ternary_partition = [this, axis](int l, int r, float splitPos) -> std::array<int, 2> {
        int lEnd = l, rBegin = r;
        for (int i = l; i < rBegin; i++) {
            if (triangles[indexArena[i]].bbox.max[axis] <= splitPos) {
                std::swap(indexArena[i], indexArena[lEnd]);
                lEnd++;
            }
            else if (triangles[indexArena[i]].bbox.min[axis] >= splitPos) {
                rBegin--;
                std::swap(indexArena[i], indexArena[rBegin]);
                i--;
            }
        }
        return {lEnd, rBegin};
    };

    return ternary_partition(l, r, splitPos);
}

uint32_t KdTree::Build(int l, int r, uint32_t axis) {
    uint32_t nodeIdx = this->size++;
    Node& node = this->tree[nodeIdx];

    AABB bounds;
    for (int i = l; i < r; i++)
        bounds.expand(triangles[indexArena[i]].bbox);

    const int triCount = r - l;
    if (triCount <= leafSize) {
        node.left = node.right = -1;
        node.triCount = triCount;
        node.triIdx = l;
        return nodeIdx;
    }

    uint32_t nAxis = (axis + 1) % 3;
    
    auto [lEnd, rBegin] = SplitSAH(bounds, l, r, axis);

    size_t lStart = indexArena.size();
    indexArena.insert(indexArena.end(), indexArena.begin() + l, indexArena.begin() + rBegin);
    
    size_t rStart = indexArena.size();
    indexArena.insert(indexArena.end(), indexArena.begin() + lEnd, indexArena.begin() + r);
    
    node.left = Build(lStart, lStart + (rBegin - l), nAxis);
    node.right = Build(rStart, rStart + (r - lEnd), nAxis);

    node.triIdx = 0;
    node.triCount = 0;
    return nodeIdx;
}

int KdTree::GetHeight() const {
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
*/