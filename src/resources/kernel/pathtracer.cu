// ============================================================
// pathtracer.cu
// CUDA conversion of Vulkan GLSL path tracer
// Using custom cuda_math.h
// ============================================================

#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include "cuda_math.h"

#define PI 3.14159265359f
#define INV_PI 0.31830988618f
#define INF 1e9f

#define MAX_DIST 100.0f
#define MAX_TRAVERSAL_DEPTH 64
#define MIN_TRACE_DIST 0.0001f
#define NORMAL_OFFSET 0.0001f

struct ComputeTile {
    uint2 tileSize;
    uint2 tileOffset;
};

struct CameraUBO {
    vec4 cameraPos;
    vec4 cameraRot;
};

struct PathtracerUBO {
    float2 iResolution;
    float  iTime;
    int    iFrame;
    CameraUBO camera;
};

struct Vertex {
    vec4 position;
    vec4 uv;
    vec4 normal;
};

struct Triangle {
    uint4 indices;   // xyz = vertex indices, w = material index
    vec4  area;
};

struct BVHNode {
    int left;
    int right;
    unsigned int triIdx;
    unsigned int triCount;
    vec4 bboxMin;
    vec4 bboxMax;
};

struct KdNode {
    int left;
    int right;
    unsigned int triIdx;
    unsigned int triCount;
    float splitPos;
    unsigned int axis;
    int pad0;
    int pad1;
};

struct Material {
    vec4  albedo;
    float specular;
    float roughness;
    float IOR;
    float transmission;

    vec3  emissiveColor;
    float emissivePower;
};

struct Ray {
    vec3 origin;
    vec3 dir;

    Ray(const vec3& origin, const vec3& dir) : origin(origin), dir(dir) {}
};

struct SceneObject {
    vec3 pos;
    vec3 normal;
    vec3 normal2;
    Material mat;
    unsigned int triIdx;
};

struct Scene {
    float d;
    float d2;
    SceneObject closestHit;
};

struct RenderData {
    Scene scene;
    vec4 color;

    RenderData(const Scene& scene, const vec4& color) : scene(scene), color(color) {};
};

struct Statistics {
    unsigned long long rays;
    unsigned long long isecs;
    unsigned long long traversals;
};

__device__ unsigned int pcg_hash(unsigned int& seed) {
    seed = seed * 747796405u + 2891336453u;
    unsigned int word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ float RandomFloat01(unsigned int& state) {
    return (float)pcg_hash(state) / 4294967296.0f;
}

__device__ inline vec3 reflect(const vec3& I, const vec3& N) {
    return I - N * (2.0f * dot(N, I));
}

__device__ inline float clampf(float x, float a, float b) {
    return fminf(fmaxf(x, a), b);
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
    BVHNode* bvhNodes,
    KdNode* kdNodes,
    unsigned int* kdtreeIndices,
    Triangle* triangles,
    Vertex* vertices,
    Triangle* eTriangles,
    Material* mats,
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
    BVHNode* bvhNodes,
    Triangle* triangles,
    Vertex* vertices,
    Material* mats,
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
    KdNode* kdNodes,
    unsigned int* kdtreeIndices,
    Triangle* triangles,
    Vertex* vertices,
    Material* mats,
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
    Triangle* eTriangles,
    Vertex* vertices,
    Material* mats,
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

__device__ Scene world(
    const Ray& ray,
    BVHNode* bvhNodes,
    KdNode* kdNodes,
    unsigned int* kdtreeIndices,
    Triangle* triangles,
    Vertex* vertices,
    Triangle* eTriangles,
    Material* mats,
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

    vec3 H = normalize(V + L);
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
    vec3 H = normalize(V + L);
    float NdotH = fmaxf(dot(N, H), 1e-6f);
    float VdotH = fmaxf(dot(V, H), 1e-6f);

    float a = roughness * roughness;
    float D = (a * a) /  fmaxf(1e-5f, PI * powf(NdotH * NdotH * (a*a - 1.0f) + 1.0f, 2.0f));

    return D * NdotH / fmaxf(1e-5f, 4.0f * VdotH);
}

__device__ float MISWeight(float pdfA, float pdfB) {
    float a2 = pdfA * pdfA;
    float b2 = pdfB * pdfB;
    return a2 / fmaxf(1e-6f, (a2 + b2));
}

__device__ bool RussianRoulette( vec3& throughput, unsigned int& seed) {
    float p = clampf( fmaxf(throughput.x, fmaxf(throughput.y, throughput.z)), 0.05f, 1.0f);

    if (RandomFloat01(seed) > p) return true;

    throughput = throughput * (1.0f / p);
    return false;
}

__device__ RenderData worldRender(
    const vec2& uv,
    Ray ray,
    BVHNode* bvhNodes,
    KdNode* kdNodes,
    unsigned int* kdtreeIndices,
    Triangle* triangles,
    Vertex* vertices,
    Triangle* eTriangles,
    Material* mats,
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

        if (scene.d >= MAX_DIST) break;

        vec3 hitPoint = ray.origin + ray.dir * scene.d;
        vec3 N = scene.closestHit.normal;
        Material mat = scene.closestHit.mat;

        // Emission
        if (mat.emissivePower > 0.0f) {
            vec3 Le = mat.emissiveColor *  mat.emissivePower;
            radiance = radiance + throughput * Le;
            break;
        }

        ray.origin = hitPoint + N * NORMAL_OFFSET;
        vec3 V = normalize(ray.dir * -1.0f);

        float glossyWeight = clampf(mat.specular, 0.0f, 1.0f);
        float diffuseWeight = 1.0f - glossyWeight;

        // Sample BSDF
        vec3 wi;
        vec3 f;
        float pdfBSDF;

        float rnd = RandomFloat01(seed);

        if (rnd < glossyWeight)
        {
            // Specular
            wi = reflect(-V, N);
            f = vec3(mat.albedo.x, mat.albedo.y, mat.albedo.z);
            pdfBSDF = 1.0f;
        }
        else
        {
            // Diffuse
            vec3 t, b;
            buildTBN(N, t, b);

            vec3 local = CosineSampleHemisphere( RandomFloat01(seed), RandomFloat01(seed));

            wi = normalize( t * local.x + b * local.y + N * local.z);

            float cosTheta = fmaxf(dot(N, wi), 0.0f);

            f = vec3(mat.albedo.x, mat.albedo.y, mat.albedo.z) * INV_PI;
            pdfBSDF = cosTheta * INV_PI;
        }

        if (pdfBSDF <= 1e-6f) break;

        float cosTheta = fmaxf(dot(N, wi), 0.0f);

        throughput = throughput * f * (cosTheta / pdfBSDF);

        ray.dir = wi;
        if (i > 4 && RussianRoulette(throughput, seed)) break;
    }

    RenderData rd(scene, vec4(radiance, 1.0f));
    return rd;
}

__global__ void pathtracerKernel(
    vec4* outImage,
    vec4* accImage,

    BVHNode* bvhNodes,
    KdNode* kdNodes,
    unsigned int* kdtreeIndices,
    Triangle* triangles,
    Vertex* vertices,
    Triangle* eTriangles,
    Material* mats,

    Statistics* statistics,

    ComputeTile ct,
    PathtracerUBO state,
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

    if (px >= (unsigned int)state.iResolution.x || py >= (unsigned int)state.iResolution.y)
        return;

    unsigned int width = (unsigned int)state.iResolution.x;

    unsigned int idx = py * width + px;

    // RNG seed
    unsigned int seed = idx + state.iFrame * 16777619u;

    // Ray
    vec3 ro = vec3(state.camera.cameraPos.x, state.camera.cameraPos.y, state.camera.cameraPos.z);
    vec3 rd = vec3(0,0,1);
    Ray ray(ro, normalize(rd));

    unsigned long long statsRays = 0;
    unsigned long long statsIsecs = 0;
    unsigned long long statsTraversals = 0;

    RenderData render = worldRender(
        vec2(0.0f),
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

    accImage[idx] = accImage[idx] + render.color;

    if (USE_STATS) {
        atomicAdd(&statistics->rays, statsRays);
        atomicAdd(&statistics->isecs, statsIsecs);
        atomicAdd(&statistics->traversals, statsTraversals);
    }

    outImage[idx] = render.color;
}

/*
extern "C"
void dispatchCUDAPathtracerKernel(
    void* cudaImagePtr,
    int width,
    int height,
    unsigned int frame
)
{
    vec4* d_out = reinterpret_cast<vec4*>(cudaImagePtr);

    dim3 block(16, 16);
    dim3 grid(
        (width + block.x - 1) / block.x,
        (height + block.y - 1) / block.y
    );

    pathtracerKernel<<<grid, block>>>(
        d_out,
        d_out,          // no accumulation for now
        nullptr,        // bvh
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {},
        {},
        0,
        0,
        4,
        true,
        false
    );

    cudaDeviceSynchronize();
}
*/