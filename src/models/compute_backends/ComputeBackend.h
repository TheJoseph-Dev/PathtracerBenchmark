#ifndef COMPUTE_BACKEND_H
#define COMPUTE_BACKEND_H

#include "PathtracerSettings.h"
#include "models/vulkan/Buffer.h"
#include "models/acceleration_structures/BVH.h"
#include "models/acceleration_structures/KdTree.h"
#include <vector>
#include <glm/glm.hpp>
#include <optional>
#include <variant>
#include <span>

namespace Pathtracer {
    class ComputeBackend {
        public:
        struct VulkanContext {
            VkPhysicalDevice physicalDevice;
            VkDevice device;
            VkCommandPool commandPool;
            VkQueue graphicsQueue;
        };

        struct PathtracerVulkanResources {
            std::vector<VkImageView> pathtracerImageViews;
            VkSampler pathtracerImageSampler;
            VkImage accImg;
            VkDescriptorPool descriptorPool;
            Pathtracer::Config pathtracerConfig;
            uint32_t PING_PONG_FRAMES;
            uint32_t PATHTRACER_IMG_COUNT;
        };

        struct CudaContext {

        };

        struct DispatchConext {
            VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
            uint32_t currentFrame;
            glm::vec2 tileSize;
            uint32_t lightBounces;
        };

        struct SceneData {
            std::span<const OBJLoader::Vertex> vertices;
            std::span<const OBJLoader::Triangle> triangles;
            std::span<const OBJLoader::Triangle> lightTriangles;
            std::variant<std::vector<BVH::Node>, std::vector<KdTree::Node>> tree;
            std::span<const uint32_t> indices_kdtree;
            std::span<const OBJLoader::Material> materials;
        };

        virtual void init(const SceneData& sceneData) = 0;
        virtual void dispatch(const DispatchConext& dispatchCtx) = 0;
        virtual void updateFrameContext(const FrameContext* newData, VkDeviceSize size) const = 0;
        //virtual void resize(uint32_t width, uint32_t height) = 0;
        //virtual void cleanup() = 0;
        virtual ~ComputeBackend() = default;
        virtual TreeStatistics GetBackendStatistics() = 0;
        virtual void GetBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) = 0;
    };
}

#endif