#ifndef COMPUTE_BACKEND_H
#define COMPUTE_BACKEND_H

#include "PathtracerSettings.h"
#include "models/vulkan/Buffer.h"
#include "models/acceleration_structures/BVH.h"
#include "models/acceleration_structures/KdTree.h"
#include "models/Helper.h"
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
            VkDescriptorPool descriptorPool;
        };

        struct DispatchConext {
            VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
            uint32_t currentFrame;
            uint32_t textureIndex;
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
        virtual void updateFrameContext(const FrameContext* newData, uint64_t size) const = 0;
        //virtual void resize(uint32_t width, uint32_t height) = 0;
        //virtual void cleanup() = 0;
        ComputeBackend(const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig) : vkCtx(vkCtx), pathtracerConfig(pathtracerConfig) {
        
        }

        virtual ~ComputeBackend() {
            for (uint32_t i = 0; i < PATHTRACER_IMG_COUNT; ++i) {
                vkDestroyImageView(vkCtx.device, pathtracerImageViews[i], nullptr);
                vkDestroyImage(vkCtx.device, pathtracerImages[i], nullptr);
                vkFreeMemory(vkCtx.device, pathtracerImagesMemory[i], nullptr);
            }
        };
        virtual TreeStatistics getBackendStatistics() = 0;
        virtual void getBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) = 0;
        inline VkImageView getPathtracerImageView(uint32_t index) const { return this->pathtracerImageViews[index]; }
    
    protected:
        VulkanContext vkCtx;
        const Pathtracer::Config& pathtracerConfig;

        static constexpr uint32_t PATHTRACER_IMG_COUNT = PING_PONG_FRAMES + 1; // + 1 comes from acc out image
        VkImage pathtracerImages[PATHTRACER_IMG_COUNT];
        VkDeviceMemory pathtracerImagesMemory[PATHTRACER_IMG_COUNT];
        VkImageView pathtracerImageViews[PATHTRACER_IMG_COUNT];
        VkImageLayout pathtracerImageLayouts[PATHTRACER_IMG_COUNT];
        VkSampler pathtracerImageSampler;
    };

}

#endif