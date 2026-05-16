# Pathtracer Optimizations Task List

- `[x]` **1. Remove lastFrameTex Logic**
  - `[x]` Modify `pathtracer.comp` to output directly from `accImage`
  - `[x]` Remove `lastFrameTex` bindings in `Renderer.h`
  - `[x]` Update descriptor sets in `SPIRV.cpp`
- `[x]` **2. AoS to SoA Conversion**
  - `[x]` Update `ComputeBackend::SceneData` to reflect SoA
  - `[x]` Update `CUDA.cpp` device buffer allocations and data copy
  - `[x]` Update `SPIRV.cpp` device buffer allocations and data copy
  - `[x]` Update `pathtracer.comp` and `pathtracer.cu` kernel parameters
  - `[x]` Refactor kernel logic to access separated arrays
- `[x]` **3. CUDA Specific Optimizations**
  - `[x]` Change `MAX_TRAVERSAL_DEPTH` to `32` in `pathtracer.cu` (Verified 32 was already set)
  - `[x]` Implement Grid-Stride Loop in `pathtracerKernel`
  - `[x]` Update kernel launch parameters in `CUDA.cpp`
  - `[x]` Move `PathtracerState` to `__constant__` memory
  - `[x]` Add `-use_fast_math` NVCC flag (via build script) (Verified it was already there)
