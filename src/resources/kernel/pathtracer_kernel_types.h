#ifndef PATHTRACER_KERNEL_TYPES
#define PATHTRACER_KERNEL_TYPES

#include "cuda_math.h"

namespace Kernel {
    struct alignas(16) ComputeTile {
        uint2 tileSize;
        uint2 tileOffset;
    };

    struct alignas(16) Camera {
        vec4 cameraPos;
        vec4 cameraRot;
    };

    struct alignas(16) PathtracerState {
        float2 iResolution;
        float  iTime;
        int    iFrame;
        Camera camera;
    };



    struct alignas(16) BVHNode {
        int left;
        int right;
        unsigned int triIdx;
        unsigned int triCount;
        vec4 bboxMin;
        vec4 bboxMax;
    };

    struct alignas(16) BVH4Node {
        int child[4];
        unsigned int triIdx;
        unsigned int triCount;
        unsigned int pad0;
        unsigned int pad1;
        vec4 bboxMin[4];
        vec4 bboxMax[4];
    };

    struct alignas(16) KdNode {
        int left;
        int right;
        unsigned int triIdx;
        unsigned int triCount;
        float splitPos;
        unsigned int axis;
        int pad0;
        int pad1;
    };

    struct alignas(16) Material {
        vec4  albedo;
        float specular;
        float roughness;
        float IOR;
        float transmission;

        vec3  emissiveColor;
        float emissivePower;
    };

    /*
    struct alignas(16) Params {
        PathtracerUBO state;
        ComputeTile ct;
        unsigned int triangleCount;
        unsigned int lightCount;
        unsigned int lightBounces;
        int accelerationStructureType;
        bool USE_STATS;
    };
    */

    struct Ray {
        vec3 origin;
        vec3 dir;

        __device__ Ray(const vec3& origin, const vec3& dir) : origin(origin), dir(dir) {}
    };

    struct Hit {
        float t;
        float u;
        float v;
        unsigned int triIdx;
    };

    struct RenderData {
        //Hit hit;
        vec4 color;

        __device__ RenderData(/*const Hit& hit, */const vec4& color) : /*hit(hit),*/ color(color) {};
    };

    struct Statistics {
        unsigned long long rays;
        unsigned long long isecs;
        unsigned long long traversals;
        unsigned long long primaryRays;
        unsigned long long secondaryRays;
        unsigned long long shadowRays;
    };
}

#endif