# Pathtracer Benchmark

A benchmark of a custom offiline pathtracer comparing SAH-BVH and Havran SAH-KdTree as well as Vulkan SPIR-V and CUDA API

![Cornell Box render](Renders/cornellbox-mis.png)

## How to Run

Download [CMake](https://cmake.org/download/)

Download [Git](
https://git-scm.com/install/windows)

Run:
> auto-setup

This will build the project using cmake with either Visual Studio, Ninja or MinGW generator. Thus, you must have one of them installed.

For Dev:

- Download [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)

## Milestones

### Pathtracer
🟩 **Base Pathtracer**
- Rendering
- Mathmatically and Physically Accurate
- Handles Benchmark
    - Acc Framebuffer
    - Tree Stats buffer

🟩 **Modularized**
- Acceleration Structure
- API
- Scenes
- Resolution
- Light Bounces
- Tile Size
- Benchmark type
    - SPP
    - IMGREF


🟩 **Vulkan SPIR-V**

🟩 **SAH-BVH**

🟩 **SAH-KdTree**

🟩 **CUDA**

### Benchmarks
🟩 **Samples Per Pixel (SPP) benchmark**
- Throughput time 
- Finish time

🟩 **Image reference benchmark**
- Compare convergence speed
- RMSE; PSNR;

🟩 **Trees Benchmark**
- Build Time
- Memory
- Traversal/s
- Rays/s
- Isec/s

🟩 **Pathtracer Benchmark**
- Primary Rays
- Secondary Rays
- Shadow Rays

🟩 **APIs Benchmark**
- Vulkan (SPIR-V)
- CUDA

🟩 **Benchmark automation** 
- Many benchmarks at single run
- Automatic resume files, infos and outputs

### Optimizations

🟩 NEE

🟩 SAH

🟨 SoA

🟩 Large BVHs

⬜ van Emde Boas layout

⬜ Ray caching

⬜ Wavefront Pathtracing

⬜ Metropolis Light Transport

⬜ ReSTIR

⬜ Neural Radiance Caching

### Supports
🟩 `.obj` and `.mtl` supoort

🟩 **Diffuse material** support

🟩 **Glossy material** support

⬜ **Transmissive material** support