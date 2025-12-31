# Pathtracer Benchmark

A benchmark of a custom offiline pathtracer comparing SAH-BVH and Havran SAH-KdTree as well as Vulkan SPIR-V and CUDA API


## Milestones

### Pathtracer
🟨 **Base Pathtracer**
- Rendering
- Mathmatically and Physically Accurate
- Handles Benchmark
    - Acc Framebuffer
    - Tree Stats buffer

🟨 **Modularized**
- Acceleration Structure
- API
- Scenes
- Resolution
- Light Bounces
- Tile Size
- Benchmark type
    - SPP
    - Error functions (MSE, RMSE,...)


🟩 **Vulkan SPIR-V**

🟩 **SAH-BVH**

⬜ **SAH-KdTree**

⬜ **CUDA**

### Benchmarks
⬜ **Samples Per Pixel (SPP) benchmark**
- Throughput time 
- Finish time

⬜ **Trees Benchmark**
- Build Time
- Memory
- Traversal/s
- Rays/s
- Isec/s

⬜ **Image reference benchmark**
- Compare convergence speed
- RMSE; MSE; PSNR; SSIM

⬜ **APIs Benchmark**
- Vulkan (SPIR-V)
- CUDA

⬜ **Benchmark automation** 
- Many benchmarks at single run
- Automatic resume files, infos and outputs

### Supports
🟩 `.obj` and `.mtl` supoort

🟩 **Diffuse material** support

🟩 **Glossy material** support

⬜ **Transmissive material** support