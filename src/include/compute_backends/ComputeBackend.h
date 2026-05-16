#ifndef COMPUTE_BACKEND_H
#define COMPUTE_BACKEND_H

#include "PathtracerSettings.h"
#include "include/vulkan/Buffer.h"
#include "include/acceleration_structures/BVH.h"
#include "include/acceleration_structures/BVH4.h"
#include "include/acceleration_structures/KdTree.h"
#include "include/Helper.h"
#include <vector>
#include <glm/glm.hpp>
#include <optional>
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
            glm::uvec2 tileSize;
            uint32_t lightBounces;
        };

        struct SyncContext {
            std::vector<VkSemaphore>& waitSemaphores;
            std::vector<VkPipelineStageFlags>& waitStages;
            std::vector<VkSemaphore>& signalSemaphores;
            uint32_t currentFrame;
        };

        struct SceneData {
            AccelerationStructureType accelerationStructureType;
            std::span<const OBJLoader::Vertex> vertices;
            std::span<const OBJLoader::Triangle> triangles;
            std::span<const OBJLoader::Triangle> lightTriangles;
            std::span<const BVH::Node> bvhNodes;
            std::span<const BVH4::Node> bvh4Nodes;
            std::span<const KdTree::Node> kdNodes;
            std::span<const uint32_t> indices_kdtree;
            std::span<const OBJLoader::Material> materials;
        };

        virtual void init(const SceneData& sceneData) = 0;
        virtual void dispatch(const DispatchConext& dispatchCtx) = 0;
        virtual void updateFrameContext(const FrameContext* newData, uint64_t size) const = 0;
        //virtual void resize(uint32_t width, uint32_t height) = 0;
        //virtual void cleanup() = 0;
        virtual void sync(const SyncContext& syncCtx) const = 0;

        // Default behavior: dispatch before graphics submit.
        virtual void dispatchBeforeGraphicsSubmit(const DispatchConext& dispatchCtx);
        // Backends that need post-submit execution can override this.
        virtual void dispatchAfterGraphicsSubmit(const DispatchConext&);

        // Fragment shader sampling layout for pathtracer output images.
        virtual VkImageLayout getFragmentSampledImageLayout() const;

        [[nodiscard]]
        virtual double queryDispatchTime(uint32_t frameIdx, float deviceTimestampPeriod) const = 0;

        ComputeBackend(const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig);

        virtual ~ComputeBackend();
        virtual GPUStatistics getBackendStatistics() = 0;
        virtual void getBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) = 0;
        VkImageView getPathtracerImageView(uint32_t index) const;
    
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
