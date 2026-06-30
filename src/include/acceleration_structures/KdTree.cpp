#include "KdTree.h"
#include <algorithm>
#include <queue>
#include <type_traits>


KdTree::KdTree(const OBJLoader::MeshGeometry& meshgeo): AccelerationStructure(meshgeo) /*, meshgeo(meshgeo)*/ {
	static_assert(alignof(Node) == 16);
    static_assert(sizeof(Node) == 16);
    static_assert(std::is_trivially_copyable_v<Node>);
    size_t triCount = this->triangles.size();

	this->tree.resize(32 * triCount / leafSize);
    indexArena.resize(triCount << 3); // ~ N log N
    for (uint32_t i = 0; i < triCount; i++) indexArena[i] = i;

    for(int axis = 0; axis < 3; axis++) this->events[axis].reserve(triCount << 1);
};


void KdTree::Build() {
    this->size = 0;
    AABB bounds;
    for (int i = 0; i < this->triangles.size(); i++) 
        bounds.expand(this->triangles[i].bbox);

    this->Build(0, this->triangles.size(), bounds);
    this->tree.resize(this->size);
}

// Cost: C = Ct + Ci * (SAl/SA * Nl + SAr/SA * Nr)
// Ct = traversal cost
// Ci = intersction cost
// SA = current node surface
// SAl = Left child surface area; SAr = Right child surface area
// Nl = number of triangles at left; Nr = number of triangles at right
KdTree::SplitInfo KdTree::SplitSAH(const AABB& bounds, int l, int r) {
    const float SA = bounds.surfaceArea();
    const int triCount = r - l;

    float splitPos = 0;
    float minCost = std::numeric_limits<float>::max();
    uint32_t bestAxis = 0;
    size_t bestIdx = 0;
    
    auto computeSAH = [&](int axis) {
        this->events[axis].clear();

        for (uint32_t i = 0; i < triCount; i++) {
            uint32_t idx = indexArena[l + i];
            const Triangle& tri = triangles[idx];
            this->events[axis].emplace_back(tri.bbox.min[axis], idx, EventType::START);
            this->events[axis].emplace_back(tri.bbox.max[axis]+1e-6, idx, EventType::END);
        }
        std::sort(this->events[axis].begin(), this->events[axis].end());

        int Nl = 0, Nr = triCount;//, Np = 0;
        for (size_t i = 0; i < events[axis].size(); i++) {
            if (events[axis][i].type == EventType::END) Nr--;
            float pos = events[axis][i].pos;
            if (pos > bounds.min[axis] && pos < bounds.max[axis]) {
                AABB leftBox = bounds;  leftBox.max[axis] = pos;
                AABB rightBox = bounds;  rightBox.min[axis] = pos;

                const float emptyBonus = (Nl == 0 || Nr == 0) ? 0.5 : 0;
                const float cost = Ct + Ci * (1-emptyBonus) * (leftBox.surfaceArea() * Nl + rightBox.surfaceArea() * Nr) /SA;

                if (cost < minCost) {
                    minCost = cost;
                    splitPos = pos;
                    bestAxis = axis;
                    bestIdx = i;
                }
            }
            if (events[axis][i].type == EventType::START) Nl++;
        }
    };

    for (int axis = 0; axis < 3; axis++) computeSAH(axis);

    if (minCost >= Ci * triCount) // SAH wasn't effective enough
        return { -1, -1, 0, 0, true };

    int nl = 0, nr = 0;
    for (size_t i = 0; i < bestIdx; ++i)
        if (this->events[bestAxis][i].type == EventType::START)
            indexArena[r + nl++] = this->events[bestAxis][i].idx;

    for (size_t i = bestIdx+1; i < events[bestAxis].size(); ++i)
        if (this->events[bestAxis][i].type == EventType::END)
            indexArena[r + nl + nr++] = this->events[bestAxis][i].idx;

    return { nl, nr, splitPos, bestAxis };
}

uint32_t KdTree::Build(int l, int r, const AABB& bounds) {
    uint32_t nodeIdx = this->size++;
    Node& node = this->tree[nodeIdx];

    const int triCount = r - l;

    /*printf("[Node %d | %d]:\n", nodeIdx, triCount);
    for (int i = l; i < r; i++)
        printf("f %d %d %d\n", triangles[indexArena[i]].v0 + 1, triangles[indexArena[i]].v1 + 1, triangles[indexArena[i]].v2 + 1);*/

    auto makeLeaf = [&]() {
        node.left = node.right = -1;
        node.triCount = triCount;
        node.triIdx = outBuffer.size();
        outBuffer.insert(outBuffer.end(), indexArena.begin() + l, indexArena.begin() + r);
        return nodeIdx;
    };

    if (triCount <= leafSize) return makeLeaf();

    auto [nl, nr, splitPos, axis, shouldLeaf] = SplitSAH(bounds, l, r);
    
    if(shouldLeaf) return makeLeaf();

    node.splitPos = splitPos;
    node.axis = axis;

    AABB lB = bounds;
    AABB rB = bounds;
    lB.max[axis] = rB.min[axis] = splitPos;

    node.right = Build(r + nl, r + nl + nr, rB);
    node.left = Build(r, r + nl, lB);

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

/*
    int newTris = 0;
    auto split_triangle = [&](const Triangle& tri) {
        enum Side { LEFT = 0, ON = 1, RIGHT = 2 };
        auto classify = [bestAxis, splitPos](const glm::vec4& p) -> Side {
            if (p[bestAxis] > splitPos) return Side::RIGHT;
            else if (p[bestAxis] < splitPos) return Side::LEFT;
            return Side::ON;
        };
        const OBJLoader::Vertex& v0 = this->meshgeo.vertices[tri.v0];
        const OBJLoader::Vertex& v1 = this->meshgeo.vertices[tri.v1];
        const OBJLoader::Vertex& v2 = this->meshgeo.vertices[tri.v2];
        Side sv0 = classify(v0.position);
        Side sv1 = classify(v1.position);
        Side sv2 = classify(v2.position);
        int nL = (sv0 == LEFT) + (sv1 == LEFT) + (sv2 == LEFT);
        int nR = (sv0 == RIGHT) + (sv1 == RIGHT) + (sv2 == RIGHT);
        int nO = 3 - nL - nR;

        auto intersect = [bestAxis, splitPos](const glm::vec3& a, const glm::vec3& b) -> glm::vec3 {
            float da = a[bestAxis] - splitPos;
            float db = b[bestAxis] - splitPos;
            float t = da / (da - db);
            glm::vec3 p = a + t * (b - a);
            p[bestAxis] = splitPos;
            return p;
        };

        auto add_vertex = [this](const glm::vec4& p, uint32_t ref) {
            this->meshgeo.vertices.emplace_back(p, this->meshgeo.vertices[ref].textureCoord, this->meshgeo.vertices[ref].normal);
            return meshgeo.vertices.size() - 1;
        };

        auto add_triangle = [this, tri](const std::array<uint32_t, 3>& idxs) {
            const glm::vec3& pv0 = meshgeo.vertices[idxs[0]].position;
            const glm::vec3& pv1 = meshgeo.vertices[idxs[1]].position;
            const glm::vec3& pv2 = meshgeo.vertices[idxs[2]].position;
            this->triangles.emplace_back(0, idxs[0], idxs[1], idxs[2], tri.m, AABB(glm::min(glm::min(pv0, pv1), pv2), glm::max(glm::max(pv0, pv1), pv2)));
            float area = 0.5f * glm::length(glm::cross(pv0 - pv1, pv2 - pv0));
            this->meshgeo.triangles.emplace_back(glm::uvec4(idxs[0], idxs[1], idxs[2], tri.m), glm::vec4(area,0.0f,0.0f,0.0f));
        };

        struct V {
            uint32_t idx;
            glm::vec4 pos;
            Side side;
        } v[3] = { {tri.v0, v0.position, sv0}, {tri.v1, v1.position, sv1}, {tri.v2, v2.position, sv2} };

        if (nO == 1) {
            int iL, iR, iO;
            for (int i = 0; i < 3; i++) {
                if (v[i].side == LEFT)  iL = i;
                if (v[i].side == RIGHT) iR = i;
                if (v[i].side == ON)    iO = i;
            }

            // One intersection
            glm::vec4 p = glm::vec4(intersect(v[iL].pos, v[iR].pos), 0.0f);
            uint32_t ip = add_vertex(p, v[iO].idx);

            // Left triangle
            add_triangle({ v[iL].idx, v[iO].idx, ip });
            indexArena[r + newTris++] = triangles.size() - 1;

            // Right triangle
            add_triangle({ v[iO].idx, v[iR].idx, ip });
            indexArena[r + newTris++] = triangles.size() - 1;
        }
        else if(nL == 1 && nR == 2) {
            int iL;
            int iR[2];
            for (int i = 0, ri = 0; i < 3; i++)
                if (v[i].side == LEFT)  iL = i;
                else iR[ri++] = i;

            glm::vec4 p0 = glm::vec4(intersect(v[iL].pos, v[iR[0]].pos), 0.0f);
            glm::vec4 p1 = glm::vec4(intersect(v[iL].pos, v[iR[1]].pos), 0.0f);

            uint32_t ip0 = add_vertex(p0, v[iL].idx);
            uint32_t ip1 = add_vertex(p1, v[iL].idx);

            // LEFT => 1 triangle
            add_triangle({ v[iL].idx, ip0, ip1 });
            indexArena[r + newTris++] = triangles.size() - 1;

            // RIGHT => 2 triangles
            add_triangle({ v[iR[0]].idx, v[iR[1]].idx, ip0 });
            indexArena[r + newTris++] = triangles.size() - 1;
            add_triangle({ ip0, v[iR[1]].idx, ip1 });
            indexArena[r + newTris++] = triangles.size() - 1;
        }
        else if (nL == 2 && nR == 1) {
            int iL[2];
            int iR;
            for (int i = 0, li = 0; i < 3; i++)
                if (v[i].side == RIGHT)  iR = i;
                else iL[li++] = i;

            glm::vec4 p0 = glm::vec4(intersect(v[iR].pos, v[iL[0]].pos), 0.0f);
            glm::vec4 p1 = glm::vec4(intersect(v[iR].pos, v[iL[1]].pos), 0.0f);

            uint32_t ip0 = add_vertex(p0, v[iR].idx);
            uint32_t ip1 = add_vertex(p1, v[iR].idx);

            add_triangle({ v[iR].idx, ip0, ip1 });
            indexArena[r + newTris++] = triangles.size() - 1;

            add_triangle({ v[iL[0]].idx, v[iL[1]].idx, ip0 });
            indexArena[r + newTris++] = triangles.size() - 1;
            add_triangle({ ip0, v[iL[1]].idx, ip1 });
            indexArena[r + newTris++] = triangles.size() - 1;
        }
    };

    for (int i = l; i < r; i++) {
        constexpr float EPS = 1e-6f;
        const Triangle& tri = this->triangles[indexArena[i]];
        bool left  = tri.bbox.max[bestAxis] < splitPos + EPS;
        bool right = tri.bbox.min[bestAxis] > splitPos - EPS;
        bool planar = splitPos - tri.bbox.min[bestAxis] < EPS && tri.bbox.max[bestAxis] - splitPos < EPS;
        if(!(left || right || planar))
            split_triangle(tri); // isec
    }

    r += newTris;
    int mid = std::partition(indexArena.begin() + l, indexArena.begin() + r, [&](const uint32_t idx) { return triangles[idx].bbox.max[bestAxis] <= splitPos; }) - indexArena.begin();
    return { mid, r, splitPos, bestAxis };
    */