# Pathtracer Benchmark

A benchmark of a custom offiline pathtracer comparing SAH-BVH and PBR SAH-KdTree as well as Vulkan SPIR-V and CUDA API

![Cornell Box render](renders/cornellbox-mis.png)

## How to Run

Download [CMake](https://cmake.org/download/)

Download [Git](
https://git-scm.com/install/windows)

Download a CMake generator:
- [Ninja](https://ninja-build.org/)
- [Visual Studio (required for CUDA)](https://visualstudio.microsoft.com/downloads/)

Run:
> auto-setup

This will build the project using cmake with either Visual Studio, Ninja or MinGW generator. Thus, you must have one of them installed.

Then:
> run

For Dev:

- Download [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)

- Download [CUDA SDK](https://developer.nvidia.com/cuda-12-8-0-download-archive?target_os=Windows&target_arch=x86_64&target_version=11&target_type=exe_local)

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

🟩 SoA

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