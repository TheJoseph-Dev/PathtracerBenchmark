#ifndef PATHTRACER_CUDA_KERNEL_H
#define PATHTRACER_CUDA_KERNEL_H

#include "cuda_math.h"

// Forward declarations for pointer-only params (full definitions in pathtracer.cu)
struct BVHNode;
struct KdNode;
struct Triangle;
struct Vertex;
struct Material;
struct ComputeTile;
struct PathtracerUBO;
struct Statistics;

namespace Pathtracer {
    extern "C" void dispatchCUDAPathtracerKernel(
        vec4* d_outImage,
        vec4* d_accImage,
        BVHNode* d_bvhNodes,
        KdNode* d_kdNodes,
        unsigned int* d_kdtreeIndices,
        Triangle* d_triangles,
        Vertex* d_vertices,
        Triangle* d_eTriangles,
        Material* d_mats,
        Statistics* d_statistics,
        const ComputeTile* ct,
        const PathtracerUBO* state,
        unsigned int triangleCount,
        unsigned int lightCount,
        unsigned int lightBounces,
        bool USE_BVH,
        bool USE_STATS
    );
};

#endif
