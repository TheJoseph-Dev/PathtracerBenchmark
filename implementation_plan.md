# Performance Optimizations Implementation Plan

This plan details the steps to implement the performance optimizations we discussed.

> [!WARNING]
> **Clarification on "SoA to AoSoA"**: The codebase currently uses an **AoS** (Array of Structures) layout for `Vertex` and `Triangle`. To optimize memory coalescing, I will implement an **AoS $\rightarrow$ SoA** (Structure of Arrays) conversion when uploading data to the GPU. This means we will pass `vertexPositions`, `vertexUVs`, and `vertexNormals` as separate independent arrays to the shaders and CUDA kernels.

## Proposed Changes

---

### Component 1: `lastFrameTex` Removal (Vulkan/C++)
Currently, `pathtracer.comp` samples the previous frame's color using `lastFrameTex` and blends it. We can rely entirely on `accImage` to store the accumulation sum and calculate the final color directly from it.

#### [MODIFY] `src/resources/shaders/pathtracer.comp`
- Remove `layout(binding = 2) uniform sampler2D lastFrameTex;`
- Remove the ping-pong sampling logic.
- Change the output logic to divide the accumulated sum in `accImage` by `iSampleFrame + 1u` and write directly to `outImage`.

#### [MODIFY] `src/Renderer.h` & `src/include/compute_backends/SPIRV.cpp`
- Remove all descriptor bindings and descriptor pool allocations associated with `lastFrameTex`.
- Remove the `lastFrameTex` parameter from the compute dispatch and descriptor set updates.

---

### Component 2: AoS $\rightarrow$ SoA Conversion
To avoid modifying the CPU-side `OBJLoader` and the BVH builders (which perform perfectly fine with AoS), the conversion will happen when uploading data to the GPU in the compute backends.

#### [MODIFY] `src/include/compute_backends/CUDA.cpp` & `src/include/compute_backends/SPIRV.cpp`
- Instead of allocating one large device buffer for `vertices` and one for `triangles`, we will allocate 5 separate device buffers: `vertexPositions`, `vertexUVs`, `vertexNormals`, `triangleIndices`, and `triangleAreas` (and similar for `eTriangles`).
- Loop through the CPU-side `SceneData` structs to pack these SoA arrays on the host, then copy them to their respective device buffers.

#### [MODIFY] `src/resources/shaders/pathtracer.comp` & `src/resources/kernel/pathtracer.cu`
- Remove the struct definitions for `Triangle` and `Vertex` from the device buffers.
- Add individual array bindings (e.g., `const vec4* __restrict__ vertexPositions`).
- Update the intersection and sampling functions to read from the separate arrays (e.g., instead of `vertices[idx].position`, we will use `vertexPositions[idx]`).

---

### Component 3: CUDA Specific Optimizations

#### [MODIFY] `src/resources/kernel/pathtracer.cu`
- **Stack Reduction**: Change `#define MAX_TRAVERSAL_DEPTH 64` to `32`. This drastically reduces the local memory footprint of the Kd-Tree traversal stack, minimizing register spilling.
- **Grid-Stride Loop**: Modify the `pathtracerKernel` to use a 1D grid-stride loop over the total number of pixels. This ignores the CPU's tile constraints completely and perfectly load-balances the work across the GPU multiprocessors.
- **Constant Memory**: Define `PathtracerState` and `ComputeTile` in `__constant__` memory to take advantage of warp-level broadcast caching.
- **Math Intrinsics**: I will replace functions like `sqrtf`, `sinf`, and `cosf` with their fast-math equivalents (or rely on `--use_fast_math`).

#### [MODIFY] `src/include/compute_backends/CUDA.cpp`
- **Kernel Launch Update**: In `dispatchCUDAPathtracerKernel`, instead of launching a 2D grid matching the tile size, we will query the GPU's `multiProcessorCount` and launch a fixed 1D grid (e.g., `numSMs * 32` blocks of `256` threads).
- Update the memory transfer logic to map the new `__constant__` memory symbols using `cudaMemcpyToSymbol`.

## Open Questions

1. **CUDA CMake/Build Script**: Since fast math optimization relies on compiler flags, how do you currently build the `pathtracer.cu` file? Is there a script or `CMakeLists.txt` modification you'd like me to apply to add the `-use_fast_math` flag to NVCC?
2. **Ping-Pong Buffer removal**: Does the removal of `lastFrameTex` mean we can completely eliminate the double-buffering (ping-ponging) logic in `Renderer.h` and the external CUDA-Vulkan semaphore synchronization (which was used to sync `outImage` reading/writing), or do you want to keep the double-buffering for safety?

Please review this plan. If you approve, I will begin execution immediately.
