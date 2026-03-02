// ============================================================
// pathtracer.cu
// CUDA conversion of Vulkan GLSL path tracer
// ============================================================
#pragma unroll
#include <stdio.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include "cuda_math.h"
#include "pathtracer_kernel_types.h"

using namespace Kernel;

#define PI 3.14159265359f
#define INV_PI 0.31830988618f
#define INF 1e9f

#define MAX_DIST 100.0f
#define MAX_TRAVERSAL_DEPTH 64
#define MIN_TRACE_DIST 0.0001f
#define NORMAL_OFFSET 0.0001f

__device__ unsigned int pcg_hash(unsigned int& seed) {
    seed = seed * 747796405u + 2891336453u;
    unsigned int word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ float RandomFloat01(unsigned int& state) {
    return (float)pcg_hash(state) / 4294967296.0f;
}

// Hash function for seed generation
__device__ unsigned int hash(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}

__device__ inline vec3 reflect(const vec3& I, const vec3& N) {
    return I - N * (2.0f * dot(N, I));
}

__device__ inline float clampf(float x, float a, float b) {
    return fminf(fmaxf(x, a), b);
}

__device__ void rotate2D(float& a, float& b, float angle) {
    float c = cosf(angle), s = sinf(angle);
    float na = a * c + b * s;
    float nb = -a * s + b * c;
    a = na;
    b = nb;
}

__device__ bool miss(float t) {
    return (t <= MIN_TRACE_DIST || t > MAX_DIST);
}

__device__ vec3 CosineSampleHemisphere(float u1, float u2) {
    float r = sqrtf(fmaxf(1e-6f, u1));
    float theta = 2.0f * PI * u2;

    return vec3(
        r * cosf(theta),
        r * sinf(theta),
        sqrtf(fmaxf(1e-6f, 1.0f - u1))
    );
}

__device__ void buildTBN(const vec3& n, vec3& t, vec3& b) {
    vec3 up = (fabsf(n.z) < 0.999f) ? vec3(0,0,1) : vec3(1,0,0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

__device__ vec3 triIntersect(const vec3& ro, const vec3& rd, const vec3& v0, const vec3& v1, const vec3& v2) {
    vec3 v1v0 = v1 - v0;
    vec3 v2v0 = v2 - v0;
    vec3 rov0 = ro - v0;

    vec3 n = cross(v1v0, v2v0);
    vec3 q = cross(rov0, rd);

    float d = 1.0f / dot(rd, n);

    float u = d * dot(-q, v2v0);
    float v = d * dot( q, v1v0);
    float t = d * dot(-n, rov0);

    if (u < 0.0f || v < 0.0f || (u + v) > 1.0f)
        t = -1.0f;

    return vec3(t, u, v);
}

struct IsecInfo {
    float tmin;
    float tmax;
};

__device__ IsecInfo RayAABBIsec(
    const Ray& ray,
    const vec3& bbMin,
    const vec3& bbMax,
    const vec3& invDir
)
{
    vec3 t0s = (bbMin - ray.origin) * invDir;
    vec3 t1s = (bbMax - ray.origin) * invDir;

    vec3 tsmaller(
        fminf(t0s.x, t1s.x),
        fminf(t0s.y, t1s.y),
        fminf(t0s.z, t1s.z)
    );

    vec3 tbigger(
        fmaxf(t0s.x, t1s.x),
        fmaxf(t0s.y, t1s.y),
        fmaxf(t0s.z, t1s.z)
    );

    float tmin = fmaxf(fmaxf(tsmaller.x, tsmaller.y), fmaxf(tsmaller.z, 0.0f));

    float tmax = fminf(fminf(tbigger.x, tbigger.y), tbigger.z);

    IsecInfo info;
    info.tmin = tmin;
    info.tmax = tmax;
    return info;
}


__device__ Scene world(
    const Ray& ray,
    const BVHNode* __restrict__ bvhNodes,
    const KdNode* __restrict__ kdNodes,
    const unsigned int* __restrict__ kdtreeIndices,
    const Triangle* __restrict__ triangles,
    const Vertex* __restrict__ vertices,
    const Triangle* __restrict__ eTriangles,
    const Material* __restrict__ mats,
    unsigned int triangleCount,
    unsigned int lightCount,
    bool USE_BVH,
    unsigned long long& statsRays,
    unsigned long long& statsIsecs,
    unsigned long long& statsTraversals
);


__device__ void TraversalBVH(
    const Ray& ray,
    Scene& scene,
    const BVHNode* __restrict__ bvhNodes,
    const Triangle* __restrict__ triangles,
    const Vertex* __restrict__ vertices,
    const Material* __restrict__ mats,
    unsigned long long& statsRays,
    unsigned long long& statsIsecs,
    unsigned long long& statsTraversals
)
{
    statsRays++;

    vec3 invDir(1.0f / ray.dir.x, 1.0f / ray.dir.y, 1.0f / ray.dir.z );

    int stack[MAX_TRAVERSAL_DEPTH];
    int stkptr = 0;
    stack[stkptr++] = 0;

    while (stkptr > 0 && stkptr + 2 < MAX_TRAVERSAL_DEPTH) {
        statsTraversals++;

        int nodeIdx = stack[--stkptr];
        BVHNode n = bvhNodes[nodeIdx];

        bool isLeaf = (n.left == -1);

        if (isLeaf)
        {
            for (unsigned int i = 0; i < n.triCount; ++i)
            {
                statsIsecs++;

                Triangle tri = triangles[n.triIdx + i];

                vec3 v0 = vec3(
                    vertices[tri.indices.x].position.x,
                    vertices[tri.indices.x].position.y,
                    vertices[tri.indices.x].position.z
                );
                vec3 v1 = vec3(
                    vertices[tri.indices.y].position.x,
                    vertices[tri.indices.y].position.y,
                    vertices[tri.indices.y].position.z
                );
                vec3 v2 = vec3(
                    vertices[tri.indices.z].position.x,
                    vertices[tri.indices.z].position.y,
                    vertices[tri.indices.z].position.z
                );

                float t = triIntersect(ray.origin, ray.dir, v0, v1, v2).x;

                if (miss(t)) continue;
                if (t > scene.d) continue;

                vec3 Ng = normalize(cross(v1 - v0, v2 - v0));

                scene.d = t;
                scene.d2 = t;

                scene.closestHit.pos = ray.origin + ray.dir * t;
                scene.closestHit.normal = Ng;
                scene.closestHit.normal2 = Ng;
                scene.closestHit.triIdx = n.triIdx + i;
                scene.closestHit.mat = mats[tri.indices.w];
            }
        }
        else
        {
            int left = n.left;
            int right = n.right;

            bool hitL = false;
            bool hitR = false;

            IsecInfo lIsec, rIsec;

            if (left >= 0)
            {
                lIsec = RayAABBIsec(
                    ray,
                    vec3(bvhNodes[left].bboxMin.x,
                         bvhNodes[left].bboxMin.y,
                         bvhNodes[left].bboxMin.z),
                    vec3(bvhNodes[left].bboxMax.x,
                         bvhNodes[left].bboxMax.y,
                         bvhNodes[left].bboxMax.z),
                    invDir
                );

                hitL = !(lIsec.tmax < lIsec.tmin || lIsec.tmin > scene.d);
            }

            if (right >= 0)
            {
                rIsec = RayAABBIsec(
                    ray,
                    vec3(bvhNodes[right].bboxMin.x,
                         bvhNodes[right].bboxMin.y,
                         bvhNodes[right].bboxMin.z),
                    vec3(bvhNodes[right].bboxMax.x,
                         bvhNodes[right].bboxMax.y,
                         bvhNodes[right].bboxMax.z),
                    invDir
                );

                hitR = !(rIsec.tmax < rIsec.tmin || rIsec.tmin > scene.d);
            }

            if (hitL && hitR)
            {
                if (lIsec.tmin < rIsec.tmin)
                {
                    stack[stkptr++] = right;
                    stack[stkptr++] = left;
                }
                else
                {
                    stack[stkptr++] = left;
                    stack[stkptr++] = right;
                }
            }
            else if (hitL) stack[stkptr++] = left;
            else if (hitR) stack[stkptr++] = right;
        }
    }
}


struct SpaceContext {
    unsigned int node;
    float tmin;
    float tmax;
};

__device__ void TraversalKdTree(
    const Ray& ray,
    Scene& scene,
    const KdNode* __restrict__ kdNodes,
    const unsigned int* __restrict__ kdtreeIndices,
    const Triangle* __restrict__ triangles,
    const Vertex* __restrict__ vertices,
    const Material* __restrict__ mats,
    unsigned long long& statsRays,
    unsigned long long& statsIsecs,
    unsigned long long& statsTraversals
)
{
    statsRays++;
    vec3 invDir(1.0f / ray.dir.x, 1.0f / ray.dir.y, 1.0f / ray.dir.z);

    SpaceContext stack[MAX_TRAVERSAL_DEPTH];
    int stkptr = 0;

    stack[stkptr++] = {0, -INF, INF};

    while (stkptr > 0 && stkptr + 2 < MAX_TRAVERSAL_DEPTH) {
        statsTraversals++;

        SpaceContext sc = stack[--stkptr];
        KdNode n = kdNodes[sc.node];

        if (sc.tmin >= scene.d || sc.tmin > sc.tmax) continue;

        bool isLeaf = (n.left == -1);

        if (isLeaf)
            for (unsigned int i = 0; i < n.triCount; ++i) {
                statsIsecs++;

                Triangle tri = triangles[kdtreeIndices[n.triIdx + i]];

                vec3 v0 = vec3(
                    vertices[tri.indices.x].position.x,
                    vertices[tri.indices.x].position.y,
                    vertices[tri.indices.x].position.z
                );
                vec3 v1 = vec3(
                    vertices[tri.indices.y].position.x,
                    vertices[tri.indices.y].position.y,
                    vertices[tri.indices.y].position.z
                );
                vec3 v2 = vec3(
                    vertices[tri.indices.z].position.x,
                    vertices[tri.indices.z].position.y,
                    vertices[tri.indices.z].position.z
                );

                float t = triIntersect(ray.origin, ray.dir, v0, v1, v2).x;

                if (miss(t)) continue;
                if (t > scene.d) continue;

                vec3 Ng = normalize(cross(v1 - v0, v2 - v0));

                scene.d = t;
                scene.closestHit.pos = ray.origin + ray.dir * t;
                scene.closestHit.normal = Ng;
                scene.closestHit.normal2 = Ng;
                scene.closestHit.triIdx = n.triIdx + i;
                scene.closestHit.mat = mats[tri.indices.w];
            }
        else
        {
            float tsplit = (n.splitPos - ray.origin[n.axis]) * invDir[n.axis];

            bool below = ray.origin[n.axis] < n.splitPos || (ray.origin[n.axis] == n.splitPos && ray.dir[n.axis] <= 0.0f);

            unsigned int near = below ? n.left : n.right;
            unsigned int far  = below ? n.right : n.left;

            if (tsplit >= sc.tmax || tsplit <= 0.0f) stack[stkptr++] = {near, sc.tmin, sc.tmax};
            else if (tsplit <= sc.tmin) stack[stkptr++] = {far, sc.tmin, sc.tmax};
            else {
                stack[stkptr++] = {far,  tsplit, sc.tmax};
                stack[stkptr++] = {near, sc.tmin, tsplit};
            }
        }
    }
}

__device__ void EmissiveTraversalBF(
    const Ray& ray,
    Scene& scene,
    const Triangle* __restrict__ eTriangles,
    const Vertex* __restrict__ vertices,
    const Material* __restrict__ mats,
    unsigned int lightCount
)
{
    for (unsigned int tIdx = 0; tIdx < lightCount; ++tIdx) {
        Triangle tri = eTriangles[tIdx];

        vec3 v0 = vec3(vertices[tri.indices.x].position.x,
                       vertices[tri.indices.x].position.y,
                       vertices[tri.indices.x].position.z);
        vec3 v1 = vec3(vertices[tri.indices.y].position.x,
                       vertices[tri.indices.y].position.y,
                       vertices[tri.indices.y].position.z);
        vec3 v2 = vec3(vertices[tri.indices.z].position.x,
                       vertices[tri.indices.z].position.y,
                       vertices[tri.indices.z].position.z);

        float t = triIntersect(ray.origin, ray.dir, v0, v1, v2).x;

        if (miss(t)) continue;

        if (t < scene.d) {
            vec3 Ng = normalize(cross(v1 - v0, v2 - v0));

            scene.d = t;
            scene.closestHit.pos = ray.origin + ray.dir * t;
            scene.closestHit.normal = Ng;
            scene.closestHit.mat = mats[tri.indices.w];
            scene.closestHit.triIdx = tIdx;
        }
    }
}

__device__ vec4 skyColor(vec2 uv) {
    
    float red = 0.1;
    float green = 0.25;
    float blue = 0.5;
    
    vec4 color = vec4(red, green, blue, 1.0) * 1.0; 
    //color = vec4(0.5);
    return vec4(0.0);
}

__device__ Scene world(
    const Ray& ray,
    const BVHNode* __restrict__ bvhNodes,
    const KdNode* __restrict__ kdNodes,
    const unsigned int* __restrict__ kdtreeIndices,
    const Triangle* __restrict__ triangles,
    const Vertex* __restrict__ vertices,
    const Triangle* __restrict__ eTriangles,
    const Material* __restrict__ mats,
    unsigned int triangleCount,
    unsigned int lightCount,
    bool USE_BVH,
    unsigned long long& statsRays,
    unsigned long long& statsIsecs,
    unsigned long long& statsTraversals
)
{
    Scene scene;
    scene.d = MAX_DIST;
    scene.d2 = MAX_DIST;

    if (USE_BVH) TraversalBVH(ray, scene, bvhNodes, triangles, vertices, mats, statsRays, statsIsecs, statsTraversals);
    else TraversalKdTree(ray, scene, kdNodes, kdtreeIndices, triangles, vertices, mats, statsRays, statsIsecs, statsTraversals);

    EmissiveTraversalBF(ray, scene, eTriangles, vertices, mats, lightCount);

    return scene;
}

__device__ vec3 fresnelSchlick(float cosT, const vec3& F0) {
    return F0 + (vec3(1.0f) - F0) * powf(1.0f - cosT, 5.0f);
}

__device__ float smithG1GGX(float NdotX, float a) {
    float a2 = a * a;
    float denom = NdotX + sqrtf(fmaxf(1e-5f, a2 + (1.0f - a2) * NdotX * NdotX));

    return (2.0f * NdotX) / fmaxf(1e-5f, denom);
}

__device__ float smithGGX(float NdotV, float NdotL, float a) {
    return smithG1GGX(NdotV, a) * smithG1GGX(NdotL, a);
}

__device__ vec3 ggxBRDF(
    const vec3& N,
    const vec3& V,
    const vec3& L,
    float roughness,
    const vec3& albedo
) {
    float NdotL = fmaxf(dot(N, L), 1e-6f);
    float NdotV = fmaxf(dot(N, V), 1e-6f);
    if (NdotL < 1e-5f || NdotV < 1e-5f)
        return vec3(0.0f);

    vec3 Htmp = V + L;
    if (dot(Htmp, Htmp) < 1e-8f) return vec3(0.0f);
    vec3 H = normalize(Htmp);

    float NdotH = fmaxf(dot(N, H), 1e-6f);
    float VdotH = fmaxf(dot(V, H), 1e-6f);

    float a = roughness * roughness;
    float a2 = a * a;

    float D = a2 /
        fmaxf(1e-6f,
        PI * powf(NdotH * NdotH *
                  (a2 - 1.0f) + 1.0f, 2.0f));

    float G = smithGGX(NdotV, NdotL, a);

    vec3 F = fresnelSchlick(VdotH, albedo);

    return (F * D * G) / fmaxf(1e-5f, 4.0f * NdotL * NdotV);
}

__device__ float ggxPdf(
    const vec3& N,
    const vec3& V,
    const vec3& L,
    float roughness
) {
    vec3 Htmp = V + L;
    if (dot(Htmp, Htmp) < 1e-9f) return 0.0f;
    vec3 H = normalize(Htmp);

    float NdotH = fmaxf(dot(N, H), 1e-6f);
    float VdotH = fmaxf(dot(V, H), 1e-6f);

    if (NdotH <= 1e-5f || VdotH <= 1e-5f) return 0.0f;

    float a = roughness * roughness;
    float D = (a * a) / fmaxf(1e-5f, PI * powf(NdotH * NdotH * (a*a - 1.0f) + 1.0f, 2.0f));

    return D * NdotH / fmaxf(1e-5f, 4.0f * VdotH);
}

__device__ float MISWeight(float pdfA, float pdfB) {
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    return a2 / fmaxf(1e-6f, (a2 + b2));
}

__device__ bool RussianRoulette(vec3& throughput, unsigned int& seed) {
    float p = clampf(fmaxf(throughput.x, fmaxf(throughput.y, throughput.z)), 0.05f, 1.0f);

    if (RandomFloat01(seed) > p) return true;

    throughput = throughput * (1.0f / p);
    return false;
}

struct LightSample {
    vec3 wi;
    vec3 Le;
    float pdf;
};

__device__ float computeLightPdf(
    const vec3& curPos,
    const vec3& prevPos,
    unsigned int triIdx,
    const Triangle* __restrict__ eTriangles,
    const Vertex* __restrict__ vertices,
    unsigned int lightCount
) {
    Triangle tri = eTriangles[triIdx];

    vec3 v0(vertices[tri.indices.x].position.x, vertices[tri.indices.x].position.y, vertices[tri.indices.x].position.z);
    vec3 v1(vertices[tri.indices.y].position.x, vertices[tri.indices.y].position.y, vertices[tri.indices.y].position.z);
    vec3 v2(vertices[tri.indices.z].position.x, vertices[tri.indices.z].position.y, vertices[tri.indices.z].position.z);

    vec3 lightNormal = normalize(cross(v1 - v0, v2 - v0));

    float area = tri.area.x;

    vec3 toLight = curPos - prevPos;
    float dist2 = dot(toLight, toLight);
    if (dist2 < 1e-9f) return 0.0f;
    vec3 wi = normalize(toLight);

    float cosL = fmaxf(dot(lightNormal, -wi), 0.0f);
    if (cosL <= 0.0f) return 0.0f;

    float denom = fmaxf(area * cosL * float(lightCount), 1e-6f);
    return dist2 / denom;
}

__device__ LightSample sampleNEE(
    const vec3& hitPos,
    const vec3& N,
    unsigned int& seed,
    const Triangle* __restrict__ eTriangles,
    const Vertex* __restrict__ vertices,
    const Material* __restrict__ mats,
    unsigned int lightCount,
    const BVHNode* __restrict__ bvhNodes,
    const KdNode* __restrict__ kdNodes,
    const unsigned int* __restrict__ kdtreeIndices,
    const Triangle* __restrict__ triangles,
    unsigned int triangleCount,
    bool USE_BVH,
    unsigned long long& statsRays,
    unsigned long long& statsIsecs,
    unsigned long long& statsTraversals
) {
    LightSample ls;
    ls.pdf = -1e-5f;
    ls.Le  = vec3(0.0f);
    ls.wi  = vec3(0.0f);

    if (lightCount == 0) return ls;

    // Uniform triangle sampling
    unsigned int lidx = min((unsigned int)(RandomFloat01(seed) * float(lightCount)), lightCount - 1u);
    Triangle tri = eTriangles[lidx];

    vec3 v0(vertices[tri.indices.x].position.x, vertices[tri.indices.x].position.y, vertices[tri.indices.x].position.z);
    vec3 v1(vertices[tri.indices.y].position.x, vertices[tri.indices.y].position.y, vertices[tri.indices.y].position.z);
    vec3 v2(vertices[tri.indices.z].position.x, vertices[tri.indices.z].position.y, vertices[tri.indices.z].position.z);

    float u = RandomFloat01(seed);
    float v = RandomFloat01(seed);
    if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
    vec3 lightPos = v0 + (v1 - v0) * u + (v2 - v0) * v;

    vec3 lightNormal = normalize(cross(v1 - v0, v2 - v0));

    vec3 toLight = lightPos - hitPos;
    float dist2 = fmaxf(dot(toLight, toLight), 1e-5f);
    float dist  = sqrtf(dist2);
    vec3 wi = toLight * (1.0f / fmaxf(1e-5f, dist));

    float cosS = dot(N, wi);
    float cosL = dot(lightNormal, -wi);
    if (cosS <= 0.0f || cosL <= 0.0f) return ls;

    // Shadow ray
    Ray shadow(hitPos + N * NORMAL_OFFSET, wi);
    Scene sh = world(shadow, bvhNodes, kdNodes, kdtreeIndices, triangles, vertices, eTriangles, mats,
                     triangleCount, lightCount, USE_BVH, statsRays, statsIsecs, statsTraversals);
    if (sh.d < dist - 2.0f * NORMAL_OFFSET) return ls;

    ls.wi  = wi;
    ls.pdf = computeLightPdf(lightPos, hitPos, lidx, eTriangles, vertices, lightCount);

    Material lmat = mats[tri.indices.w];
    ls.Le = lmat.emissiveColor * lmat.emissivePower;

    return ls;
}

__device__ vec3 diffuseSample(const vec3& normals, unsigned int& seed) {
    vec3 t, b;
    buildTBN(normals, t, b);
    float u1 = RandomFloat01(seed);
    float u2 = RandomFloat01(seed);
    vec3 localDir = CosineSampleHemisphere(u1, u2);
    return normalize(t * localDir.x + b * localDir.y + normals * localDir.z);
}

__device__ vec3 sampleGGX(const vec3& N, const vec3& V, float roughness, float& pdf, unsigned int& seed) {
    roughness = fmaxf(roughness, 0.03f);
    float a = roughness * roughness;
    float u1 = RandomFloat01(seed);
    float u2 = RandomFloat01(seed);

    float phi = 2.0f * PI * u1;
    float cosTheta = sqrtf(fmaxf(1e-6f, (1.0f - u2) / (1.0f + (a*a - 1.0f) * u2)));
    float sinTheta = sqrtf(fmaxf(1e-6f, 1.0f - cosTheta * cosTheta));

    vec3 H_local(cosf(phi) * sinTheta, sinf(phi) * sinTheta, cosTheta);

    vec3 T, B;
    buildTBN(N, T, B);
    vec3 bH = T * H_local.x + B * H_local.y + N * H_local.z;
    if (dot(bH, bH) < 1e-9f) { pdf = 1e-6f; return vec3(0.0f); }
    vec3 H = normalize(bH);

    vec3 L = reflect(-V, H);
    if (dot(N, L) <= 0.0f) {
        pdf = 1e-6f;
        return vec3(0.0f);
    }

    float NdotH = fmaxf(dot(N, H), 1e-6f);
    float VdotH = fmaxf(dot(V, H), 1e-6f);
    float D = (a*a) / fmaxf(1e-5f, PI * powf(NdotH*NdotH * (a*a - 1.0f) + 1.0f, 2.0f));

    pdf = D * NdotH / (4.0f * VdotH);
    return normalize(L);
}

__device__ RenderData worldRender(
    const vec2& uv,
    Ray ray,
    const BVHNode* __restrict__ bvhNodes,
    const KdNode* __restrict__ kdNodes,
    const unsigned int* __restrict__ kdtreeIndices,
    const Triangle* __restrict__ triangles,
    const Vertex* __restrict__ vertices,
    const Triangle* __restrict__ eTriangles,
    const Material* __restrict__ mats,
    unsigned int triangleCount,
    unsigned int lightCount,
    bool USE_BVH,
    unsigned int lightBounces,
    unsigned int& seed,
    unsigned long long& statsRays,
    unsigned long long& statsIsecs,
    unsigned long long& statsTraversals
)
{
    Scene scene;
    vec3 radiance(0.0f);
    vec3 throughput(1.0f);

    float prevPdfBSDF = 1.0f;

    for (unsigned int i = 0; i < lightBounces; i++) {
        scene = world(
            ray,
            bvhNodes, kdNodes,
            kdtreeIndices,
            triangles, vertices,
            eTriangles, mats,
            triangleCount, lightCount,
            USE_BVH,
            statsRays, statsIsecs,
            statsTraversals
        );

        if (scene.d >= MAX_DIST) {
            // sky is black
            break;
        }

        vec3 hitPoint = ray.origin + ray.dir * scene.d;
        vec3 N = scene.closestHit.normal;
        Material mat = scene.closestHit.mat;

        // Emission — apply MIS weight when not the primary hit
        if (mat.emissivePower > 0.0f) {
            vec3 Le = mat.emissiveColor * mat.emissivePower;
            if (i > 0) {
                float pdfLight = computeLightPdf(hitPoint, ray.origin, scene.closestHit.triIdx, eTriangles, vertices, lightCount);
                float w = MISWeight(prevPdfBSDF, pdfLight);
                radiance = radiance + throughput * Le * w;
            } else radiance = radiance + throughput * Le;
            break;
        }

        ray.origin = hitPoint + N * NORMAL_OFFSET;
        vec3 V = normalize(ray.dir * -1.0f);

        float glossyWeight = clampf(mat.specular, 0.0f, 1.0f);
        float diffuseWeight = 1.0f - glossyWeight;

        vec3 albedo(mat.albedo.x, mat.albedo.y, mat.albedo.z);

        // Next Event Estimation (NEE)
        LightSample ls = sampleNEE(
            hitPoint, N, seed,
            eTriangles, vertices, mats, lightCount,
            bvhNodes, kdNodes, kdtreeIndices, triangles, triangleCount,
            USE_BVH, statsRays, statsIsecs, statsTraversals
        );
        bool didNEE = ls.pdf > 0.0f;

        if (didNEE) {
            float cosS = fmaxf(dot(N, ls.wi), 0.0f);
            float pdfBSDF = glossyWeight * ggxPdf(N, V, ls.wi, mat.roughness) + diffuseWeight * cosS * INV_PI;
            pdfBSDF = fmaxf(pdfBSDF, 1e-6f);

            float w = MISWeight(ls.pdf, pdfBSDF);

            vec3 f = ggxBRDF(N, V, ls.wi, mat.roughness, albedo) * glossyWeight
                   + albedo * INV_PI * diffuseWeight;
            radiance = radiance + throughput * f * ls.Le * cosS * w * (1.0f / ls.pdf);
        }

        // BSDF sample
        vec3 wi;
        float pdfBSDF;
        vec3 f;

        float rnd = RandomFloat01(seed);
        if (rnd < glossyWeight) {
            // GGX specular
            float pdfGGX;
            wi = sampleGGX(N, V, mat.roughness, pdfGGX, seed);

            if (pdfGGX <= 1e-6f || dot(N, wi) <= 0.0f) break;

            float cosTheta = fmaxf(dot(N, wi), 0.0f);
            f = ggxBRDF(N, V, wi, mat.roughness, albedo);

            pdfBSDF = glossyWeight * pdfGGX + diffuseWeight * cosTheta * INV_PI;
        } else {
            // Diffuse
            wi = diffuseSample(N, seed);
            float cosTheta = fmaxf(dot(N, wi), 0.0f);

            f = albedo * INV_PI;

            pdfBSDF = glossyWeight * ggxPdf(N, V, wi, mat.roughness) + diffuseWeight * cosTheta * INV_PI;
        }

        if (pdfBSDF <= 1e-6f) break;
        pdfBSDF = fmaxf(pdfBSDF, 1e-6f);
        prevPdfBSDF = pdfBSDF;

        float cosTheta = fmaxf(dot(N, wi), 0.0f);
        throughput = throughput * f * (cosTheta / pdfBSDF);

        ray.dir = wi;

        if (i > 4 && RussianRoulette(throughput, seed)) break;
    }

    // Guard against NaN / Inf
    if (isnan(radiance.x) || isnan(radiance.y) || isnan(radiance.z) ||
        isinf(radiance.x) || isinf(radiance.y) || isinf(radiance.z))
        radiance = vec3(0.0f);

    RenderData rd(scene, vec4(radiance, 1.0f));
    return rd;
}

__global__ void pathtracerKernel(
    cudaSurfaceObject_t outImage,
    vec4* __restrict__ accImage,

    const BVHNode* __restrict__ bvhNodes,
    const KdNode* __restrict__ kdNodes,
    const unsigned int* __restrict__ kdtreeIndices,
    const Triangle* __restrict__ triangles,
    const Vertex* __restrict__ vertices,
    const Triangle* __restrict__ eTriangles,
    const Material* __restrict__ mats,

    Statistics* __restrict__ statistics,

    ComputeTile ct,
    const PathtracerState* __restrict__ state,
    unsigned int triangleCount,
    unsigned int lightCount,
    unsigned int lightBounces,
    bool USE_BVH,
    bool USE_STATS
)
{
    unsigned int gx = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned int gy = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned int px = ct.tileOffset.x + gx;
    unsigned int py = ct.tileOffset.y + gy;

    if (px >= (unsigned int)state->iResolution.x || py >= (unsigned int)state->iResolution.y)
        return;
    

    unsigned int width  = (unsigned int)state->iResolution.x;
    unsigned int height = (unsigned int)state->iResolution.y;
    unsigned int idx    = py * width + px;

    unsigned int seed = hash(px + py * width + (unsigned int)state->iFrame * 16777619u) | 1u;

    float aspect = state->iResolution.x / state->iResolution.y;

    vec2 uv;
    uv.x = (float(px) / float(width))  * 2.0f - 1.0f;
    uv.y = (float(py) / float(height)) * 2.0f - 1.0f;
    uv.x *= aspect;

    // Subpixel jitter for anti-aliasing
    float jx = RandomFloat01(seed) - 0.5f;
    float jy = RandomFloat01(seed) - 0.5f;

    vec2 jitteredUV;
    jitteredUV.x = (float(px) + jx) / float(width);
    jitteredUV.y = (float(py) + jy) / float(height);
    jitteredUV.x = jitteredUV.x * 2.0f - 1.0f;
    jitteredUV.y = jitteredUV.y * 2.0f - 1.0f;
    jitteredUV.y = -jitteredUV.y;
    jitteredUV.x *= aspect;
    jitteredUV.x = -jitteredUV.x;

    vec3 ro(state->camera.cameraPos.x, state->camera.cameraPos.y, state->camera.cameraPos.z);
    vec3 cameraRot(state->camera.cameraRot.x, state->camera.cameraRot.y, state->camera.cameraRot.z);

    vec3 rd(jitteredUV.x, jitteredUV.y, 1.0f);
    rd = normalize(rd);

    // Camera rotation
    rotate2D(rd.y, rd.z, cameraRot.x);
    rotate2D(rd.x, rd.z, cameraRot.y);
    rotate2D(rd.x, rd.y, cameraRot.z);

    Ray ray(ro, rd);

    unsigned long long statsRays        = 0;
    unsigned long long statsIsecs       = 0;
    unsigned long long statsTraversals  = 0;
    
    RenderData render = worldRender(
        uv,
        ray,
        bvhNodes, kdNodes,
        kdtreeIndices,
        triangles, vertices,
        eTriangles, mats,
        triangleCount,
        lightCount,
        USE_BVH,
        lightBounces,
        seed,
        statsRays,
        statsIsecs,
        statsTraversals
    );
    
    
    // Frame accumulation
    vec4 lastSum = accImage[idx];
    if (state->iFrame == 0) lastSum = vec4(0.0f);
    vec4 newSum = lastSum + vec4(render.color.x, render.color.y, render.color.z, 0.0f);
    accImage[idx] = newSum;
    
    if (USE_STATS) {
        atomicAdd(&statistics->rays,        statsRays);
        atomicAdd(&statistics->isecs,       statsIsecs);
        atomicAdd(&statistics->traversals,  statsTraversals);
    }

    // Temporal mix: average of all accumulated samples
    float t = 1.0f / float(state->iFrame + 1);
    vec3 finalColor(newSum.x * t, newSum.y * t, newSum.z * t);
    float4 out = make_float4(finalColor.x, finalColor.y, finalColor.z, 1.0f);

    surf2Dwrite(out, outImage, px * sizeof(float4), py);
}


extern "C"
void dispatchCUDAPathtracerKernel(
    cudaSurfaceObject_t d_outImage,
    vec4* __restrict__ d_accImage,
    const BVHNode* __restrict__ d_bvhNodes,
    const KdNode* __restrict__ d_kdNodes,
    const unsigned int* __restrict__ d_kdtreeIndices,
    const Triangle* __restrict__ d_triangles,
    const Vertex* __restrict__ d_vertices,
    const Triangle* __restrict__ d_eTriangles,
    const Material* __restrict__ d_mats,
    Statistics* __restrict__ d_statistics,
    ComputeTile ct,
    const PathtracerState* __restrict__ state,
    unsigned int triangleCount,
    unsigned int lightCount,
    unsigned int lightBounces,
    bool USE_BVH,
    bool USE_STATS
)
{
    dim3 block(128, 1);
    dim3 grid(
        ((unsigned int)ct.tileSize.x + block.x - 1) / block.x,
        ((unsigned int)ct.tileSize.y + block.y - 1) / block.y
    );
    
    pathtracerKernel<<<grid, block>>>(
        d_outImage,
        d_accImage,
        d_bvhNodes,
        d_kdNodes,
        d_kdtreeIndices,
        d_triangles,
        d_vertices,
        d_eTriangles,
        d_mats,
        d_statistics,
        ct,
        state,
        triangleCount,
        lightCount,
        lightBounces,
        USE_BVH,
        USE_STATS
    );
    
}