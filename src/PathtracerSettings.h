#ifndef PATHTRACER_SETTINGS_H
#define PATHTRACER_SETTINGS_H

#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <string>
#include <stdexcept>
#include <iostream>
#include <utility>

namespace Pathtracer {

    constexpr uint32_t local_size_x = 64;
    constexpr uint32_t local_size_y = 1;
    //constexpr uint32_t stats_sample_interval = 16; // Keep in sync with shaders/kernels
    constexpr uint32_t pixel_sample_stride = 2; // Keep in sync with shaders/kernels

    struct PushConstants {
        struct ComputeTile {
            glm::uvec2 tileSize;
            glm::uvec2 tileOffset;
        };

        ComputeTile ct;
        uint32_t lightBounces;
    };

    struct SpecializedConstants {
        VkBool32 useNEE = VK_TRUE;
        VkBool32 useMIS = VK_TRUE;
        uint32_t accelerationStructureType = 0;
        VkBool32 useStats = VK_TRUE;
    };

    /*
        NONE: Pathtracer runs indefinitely
        SPP: Pathtracer runs for a fixed number of frames/spp (samples per pixel). This yields runtime and tree build and traversal statistics
        IMGREF: Pathtracer compares rendered output against a fixed scene reference image.
                By default it runs the SPP ladder (1,4,16,64,...) but it can also run a single SPP pass for QA.
    */
    enum BenchmarkType {
        NONE = 0,
        SPP = 1, /*Samples Per Pixel*/
        IMGREF = 2 /*Image Reference*/
    };


    struct Benchmark {
        BenchmarkType btype = BenchmarkType::NONE;
        uint32_t spp = 0;
        bool useSPPLadder = true;
        float rmseThreshold = -1.0f;
    };

    enum AccelerationStructureType {
        BVH = 0,
        BVH4 = 1,
        KD_TREE = 2
    };

    enum ComputeBackendType {
        SPIRV_T = 0,
        CUDA_T = 1
    };

    enum Scene {
        CORNELL_BOX = 0,
        SIBENIK = 1,
        BUNNY = 2,
        DRAGON = 3,
        SPONZA = 4
    };

    enum Resolution {
        R480x320 = 0,
        R512x512 = 1,
        R720x480 = 2,
        R1080x720 = 3,
        R1024x1024 = 4,
        R1440x810 = 5,
        R1920x1080 = 6
    };

    class Config {
        const AccelerationStructureType accelerationStructureType;
        const ComputeBackendType cbType;
        const Scene scene;
        const Resolution resolution;
        const uint32_t lightBounces;
        Benchmark benchmarkInfo;
        const glm::uvec2 tileSize;
        const bool saveOutputImage;
        const bool getStatsAS; // Enables or not the atomics
        const bool saveStatistics;
        const std::string customScenePath;
        const std::string customSceneLabel;

        static std::string BuildSceneLabel(const std::string& path) {
            if (path.empty()) return "";
            const size_t slash = path.find_last_of("\\/");
            const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
            size_t end = path.find_last_of('.');
            if (end == std::string::npos || end < start) end = path.size();
            return path.substr(start, end - start);
        }

    public:

        Config(AccelerationStructureType as, ComputeBackendType cbType, Scene scene, Resolution resolution, uint32_t lightBounces, Benchmark benchmarkInfo, glm::uvec2 tileSize = glm::uvec2(8, 8), bool saveOutputImage = false, bool getStatsAS = true, bool saveStatistics = true, std::string customScenePath = {})
            : accelerationStructureType(as), cbType(cbType), scene(scene), resolution(resolution), lightBounces(lightBounces), benchmarkInfo(benchmarkInfo), saveOutputImage(saveOutputImage), tileSize(tileSize), getStatsAS(getStatsAS), saveStatistics(saveStatistics), customScenePath(std::move(customScenePath)), customSceneLabel(BuildSceneLabel(this->customScenePath))
        {
        }

        AccelerationStructureType GetAccelerationStructureType() const {
            return accelerationStructureType;
        }

        ComputeBackendType GetComputeBackendType() const {
            return cbType;
        }

        /*
        Scene GetScene() const {
            return scene;
        }
        */

        std::string GetScene() const {
            if (!customSceneLabel.empty())
                return customSceneLabel;
            switch (this->scene) {
            case Scene::CORNELL_BOX: return "cornell_box";
            case Scene::SIBENIK: return "sibenik2";
            case Scene::BUNNY: return "bunny-cbx";
            case Scene::DRAGON: return "dragon-cbx";
            case Scene::SPONZA: return "sponza2";
            default: throw std::runtime_error("Unknown scene");
            }
        }

        bool HasCustomScene() const {
            return !customScenePath.empty();
        }

        const std::string& GetCustomScenePath() const {
            return customScenePath;
        }

        glm::uvec2 GetResolution() const {
            switch (this->resolution) {
            case Resolution::R480x320: return glm::uvec2(480, 320);
            case Resolution::R512x512: return glm::uvec2(512, 512);
            case Resolution::R720x480: return glm::uvec2(720, 480);
            case Resolution::R1080x720: return glm::uvec2(1080, 720);
            case Resolution::R1024x1024: return glm::uvec2(1024, 1024);
            case Resolution::R1440x810: return glm::uvec2(1440, 810);
            case Resolution::R1920x1080: return glm::uvec2(1920, 1080);
            default: throw std::runtime_error("Unknown resolution");
            }
        }

        uint32_t GetLightBounces() const {
            return this->lightBounces;
        }

        Benchmark GetBenchmarkInfo() const {
            return this->benchmarkInfo;
        }

        glm::uvec2 GetTileSize() const {
            return this->tileSize;
        }

        bool ShouldSaveImage() const {
            return this->saveOutputImage;
        }

        bool ShouldSaveStatistics() const {
            return this->saveStatistics;
        }

        bool ShouldGetStatsAS() const {
            return this->getStatsAS;
        }

        void SetSPP(uint32_t spp) {
            this->benchmarkInfo.spp = spp;
        }

        void Print(const std::string& cpuName = "Unknown CPU", const std::string& gpuName = "Unknown GPU", const std::string& dateTime = "") const {
            std::string accStructName;
            switch (this->accelerationStructureType) {
            case AccelerationStructureType::BVH:
                accStructName = "Binned SAH-BVH";
                break;
            case AccelerationStructureType::BVH4:
                accStructName = "Binned SAH-BVH4";
                break;
            case AccelerationStructureType::KD_TREE:
                accStructName = "Havran SAH-KdTree";
                break;
            default:
                accStructName = "Unknown";
                break;
            }

            std::cout << "[Config]";

            if (!dateTime.empty())
                std::cout << "\n Date/Time: " << dateTime;

            std::cout
                << "\n CPU: " << cpuName
                << "\n GPU: " << gpuName
                << "\n Scene: " << this->GetScene()
                << "\n Compute Backend: " << (!this->cbType ? "SPIR-V" : "CUDA")
                << "\n Acc. Structure: " << accStructName
                << "\n Resolution: " << this->GetResolution().x << "x" << this->GetResolution().y
                << "\n Tile Size: " << this->GetTileSize().x << "x" << this->GetTileSize().y
                << "\n SPP: " << this->benchmarkInfo.spp
                << "\n Max Light Bounces: " << this->lightBounces << "\n";
        }

        std::string InlineString() const {
            std::string accStructTag;
            switch (this->accelerationStructureType) {
            case AccelerationStructureType::BVH:
                accStructTag = "-bvh";
                break;
            case AccelerationStructureType::BVH4:
                accStructTag = "-bvh4";
                break;
            case AccelerationStructureType::KD_TREE:
                accStructTag = "-kdtree";
                break;
            default:
                accStructTag = "-unknown";
                break;
            }

            return "-spp" + std::to_string(this->benchmarkInfo.spp)
                + "-r" + std::to_string(GetResolution().x) + "x" + std::to_string(GetResolution().y)
                + "-lb" + std::to_string(this->lightBounces)
                + (!this->cbType ? "-spirv" : "-cuda")
                + accStructTag;
        }
    };

    /*
    struct GPUGPUStatistics {
        struct uint64gpu_t { uint32_t lo, hi; };
        uint64gpu_t rays;
        uint64gpu_t isecs;
        uint64gpu_t traversals;
    };
    */

    struct GPUStatistics {
        uint64_t rays;
        uint64_t isecs;
        uint64_t traversals;
        uint64_t primaryRays;
        uint64_t secondaryRays;
        uint64_t shadowRays;
    };


    struct Statistics {
        GPUStatistics treeStats;
        float elapsedTotalTime = 0.0f;
        float fps = 0.0f; // time/spp
        float avgKernelTime = 0.0f;

        float accStructBuildTime = 0.0f;
        uint32_t accStructMemoryUsage = 0;

        uint32_t sceneTriangles = 0;

        float rmse = 0.0f;
        float psnr = 0.0f;
    };

    struct alignas(16) Camera {
        glm::vec4 cameraPos;
        glm::vec4 cameraRot;
    };

    struct alignas(16) FrameContext {
        glm::vec2 iResolution;
        float iTime;
        int iFrame;
        //int accumulate;
        //vec3 pad0;
        Camera camera;
    };

};

#endif