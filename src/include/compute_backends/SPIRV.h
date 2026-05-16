#ifndef SPIRV_BACKEND_H
#define SPIRV_BACKEND_H

#include "ComputeBackend.h"
#include "include/vulkan/Shader.h"

namespace Pathtracer {
	class SPIRV final : public ComputeBackend {

        VkDescriptorSetLayout computeDescriptorSetLayout;
        VkPipelineLayout computePipelineLayout;
        VkPipeline computePipeline;
        VkDescriptorSet computeDescriptorSet[PING_PONG_FRAMES];
        VkQueryPool timestampPools[PING_PONG_FRAMES];

        enum SSBOBinding {
            BVH_NODES = 3,
            KDTREE_NODES = 4,
            KDTREE_INDICES = 5,
            TRIANGLE_INDICES = 6,
            TRIANGLE_AREAS = 7,
            VERTEX_POSITIONS = 8,
            VERTEX_UVS = 9,
            VERTEX_NORMALS = 10,
            STATISTICS = 11,
            BVH4_NODES = 12,
            EMISSIVE_INDICES = 13,
            EMISSIVE_AREAS = 14,
            MATERIALS = 15
        };
        std::vector<Buffer> SSBOs; // pathtracerSSBOs
        std::vector<Buffer> stagingBuffers;
		Buffer UBO; // pathtracerUBO
        AccelerationStructureType activeTreeType = AccelerationStructureType::BVH;

        static constexpr VkDescriptorSetLayoutBinding computeBindings[] = {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // accImage
            { SSBOBinding::BVH_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::KDTREE_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::KDTREE_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::TRIANGLE_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::TRIANGLE_AREAS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::VERTEX_POSITIONS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::VERTEX_UVS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::VERTEX_NORMALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::STATISTICS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::BVH4_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::EMISSIVE_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::EMISSIVE_AREAS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::MATERIALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
        };
        static constexpr uint32_t bindingCount = sizeof(computeBindings) / sizeof(computeBindings[0]);

    public:

        static constexpr uint32_t SSBOsCount = 13;

        SPIRV(const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig);
        ~SPIRV() override;

        void updateFrameContext(const FrameContext* newData, uint64_t size) const override;
    private:
	    
        VkCommandBuffer beginSingleTimeCommands() const;
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
        void create2DNearestImageSampler();
        void createImage2D(uint32_t width, uint32_t height);
        void createPathtracerImages();
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        Buffer createStorageBuffer(VkDeviceSize size, const void* initialData, bool keepStaging = false);
        void createSSBO(const VkBuffer& buffer, uint32_t binding);
        void createDescriptorSetLayout();
        void createComputePipeline();
        uint32_t getOptimalLocalSize() const;

        void init(const SceneData& sceneData) override;
        void dispatch(const DispatchConext& dispatchCtx) override;
        void sync(const SyncContext& syncCtx) const override;
        double queryDispatchTime(uint32_t frameIdx, float deviceTimestampPeriod) const override;
        Pathtracer::GPUStatistics getBackendStatistics() override;
        void getBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) override;
	};
};

#endif