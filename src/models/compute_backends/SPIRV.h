#ifndef SPIRV_H
#define SPIRV_H

#include "ComputeBackend.h"
#include "models/vulkan/Shader.h"

namespace Pathtracer {
	class SPIRV final : public ComputeBackend {
        const Pathtracer::Config pathtracerConfig;
        const uint32_t PING_PONG_FRAMES = 2;
        const uint32_t PATHTRACER_IMG_COUNT = 3;
        std::vector<VkImageView> pathtracerImageViews;
        VkSampler pathtracerImageSampler;
        VkImage accImg;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkDescriptorPool descriptorPool;
        VkDescriptorSetLayout computeDescriptorSetLayout;
        VkPipelineLayout computePipelineLayout;
        VkPipeline computePipeline;
        std::vector<VkDescriptorSet> computeDescriptorSet; // [PING_PONG_FRAMES]

        enum SSBOBinding {
            BVH_NODES = 3,
            KDTREE_NODES = 4,
            KDTREE_INDICES = 5,
            TRIANGLES = 6,
            VERTICES = 7,
            EMISSIVES = 8,
            MATERIALS = 9,

            STATISTICS = 11
        };
        std::vector<Buffer> SSBOs; // pathtracerSSBOs
        std::vector<Buffer> stagingBuffers;
		Buffer UBO; // pathtracerUBO

        static constexpr VkDescriptorSetLayoutBinding computeBindings[] = {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::BVH_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::KDTREE_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::KDTREE_INDICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::TRIANGLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::VERTICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::EMISSIVES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::MATERIALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::STATISTICS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };
        static constexpr uint32_t bindingCount = sizeof(computeBindings) / sizeof(computeBindings[0]);

    public:
        static constexpr uint32_t SSBOsCount = 8;

        SPIRV(const VulkanContext& vkCtx, const PathtracerVulkanResources& vkRsrcs)
            : pathtracerConfig(vkRsrcs.pathtracerConfig), PING_PONG_FRAMES(vkRsrcs.PING_PONG_FRAMES), PATHTRACER_IMG_COUNT(vkRsrcs.PATHTRACER_IMG_COUNT) {
            this->physicalDevice = vkCtx.physicalDevice;
            this->device = vkCtx.device;
            this->commandPool = vkCtx.commandPool;
            this->graphicsQueue = vkCtx.graphicsQueue;
            this->UBO = Buffer(physicalDevice, device, sizeof(Pathtracer::FrameContext), 0, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            this->pathtracerImageSampler = vkRsrcs.pathtracerImageSampler;
            this->pathtracerImageViews = vkRsrcs.pathtracerImageViews;
            this->accImg = vkRsrcs.accImg;
            this->descriptorPool = vkRsrcs.descriptorPool;
            this->SSBOs.reserve(SSBOsCount);
            this->computeDescriptorSet.resize(PING_PONG_FRAMES);
        }

        ~SPIRV() {
            vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
            vkDestroyPipeline(device, computePipeline, nullptr);
            vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
            SSBOs.clear();
            stagingBuffers.clear();
        }

        void updateFrameContext(const FrameContext* newData, VkDeviceSize size) const override {
            void* data;
            vkMapMemory(device, this->UBO.memory, 0, size, 0, &data);
            memcpy(data, newData, (size_t)size);
            vkUnmapMemory(device, this->UBO.memory);
        }
    private:
	    
        VkCommandBuffer beginSingleTimeCommands() const {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = this->commandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(this->device, &allocInfo, &commandBuffer);

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(commandBuffer, &beginInfo);

            return commandBuffer;
        }

        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(this->graphicsQueue);

            vkFreeCommandBuffers(this->device, this->commandPool, 1, &commandBuffer);
        }

        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
            VkCommandBuffer commandBuffer = beginSingleTimeCommands();

            VkBufferCopy copyRegion{};
            copyRegion.size = size;
            vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

            endSingleTimeCommands(commandBuffer);
        }

        Buffer createStorageBuffer(VkDeviceSize size, const void* initialData, bool keepStaging = false) {
            Buffer staging = Buffer(this->physicalDevice, this->device, size, initialData, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT); // = createStagingBuffer(size, initialData);

            // Create device-local GPU buffer
            Buffer gpuBuffer = Buffer(this->physicalDevice, this->device, size, nullptr, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            
            // Copy staging => device-local buffer
            copyBuffer(staging.buffer, gpuBuffer.buffer, size);

            if (keepStaging) this->stagingBuffers.push_back(std::move(staging));

            return gpuBuffer; // C++ guarantees it stays alive, so ~Buffer not called here
        }

        VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout* dsl) const {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = this->descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = dsl;

            VkDescriptorSet descriptorSet;
            vkAllocateDescriptorSets(this->device, &allocInfo, &descriptorSet);
            return descriptorSet;
        }

        void createSSBO(const VkBuffer& buffer, uint32_t binding) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstBinding = binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfo;
            for (int i = 0; i < PING_PONG_FRAMES; i++) {
                write.dstSet = computeDescriptorSet[i];
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
        }

        void createDescriptorSetLayout() {
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = bindingCount;
            layoutInfo.pBindings = this->computeBindings;

            vkCreateDescriptorSetLayout(this->device, &layoutInfo, nullptr, &this->computeDescriptorSetLayout);
        }

        void updateUniformBufferDescriptorSet(VkDescriptorSet descriptorSet, uint32_t binding, VkBuffer buffer, VkDeviceSize size) const {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = size;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = descriptorSet;
            descriptorWrite.dstBinding = binding;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(this->device, 1, &descriptorWrite, 0, nullptr);
        }

        void updateStorageImageDescriptorSet(VkDescriptorSet descriptorSet, uint32_t binding, VkImageView imageView, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL) const {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageView = imageView;
            imageInfo.imageLayout = layout; // usually GENERAL for compute

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        void updateCombinedImageSamplerDescriptorSet(VkDescriptorSet descriptorSet, uint32_t binding, VkSampler sampler, VkImageView imageView, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) const {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = sampler;
            imageInfo.imageView = imageView;
            imageInfo.imageLayout = layout;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        
        void createComputePipeline() {
            // Descriptor set layout for compute (storage image + uniform + sampler)
            VkPipelineLayoutCreateInfo computePipelineLayoutInfo{};
            computePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            computePipelineLayoutInfo.setLayoutCount = 1;
            computePipelineLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;

            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(Pathtracer::PushConstants);
            computePipelineLayoutInfo.pushConstantRangeCount = 1;
            computePipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

            vkCreatePipelineLayout(device, &computePipelineLayoutInfo, nullptr, &computePipelineLayout);

            Pathtracer::SpecializedConstants spec = {
                .useNEE = VK_TRUE,
                .useMIS = VK_TRUE,
                .useBVH = (this->pathtracerConfig.GetAccelerationStructureType() == Pathtracer::AccelerationStructureType::BVH),
                .useStats = this->pathtracerConfig.ShouldGetStatsAS()
            };

            VkSpecializationMapEntry entries[] = {
                { 1, offsetof(Pathtracer::SpecializedConstants, useNEE), sizeof(VkBool32) },
                { 2, offsetof(Pathtracer::SpecializedConstants, useMIS), sizeof(VkBool32) },
                { 3, offsetof(Pathtracer::SpecializedConstants, useBVH), sizeof(VkBool32) },
                { 4, offsetof(Pathtracer::SpecializedConstants, useStats), sizeof(VkBool32) }
            };

            VkSpecializationInfo specInfo{
                .mapEntryCount = 4,
                .pMapEntries = entries,
                .dataSize = sizeof(Pathtracer::SpecializedConstants),
                .pData = &spec
            };

            // Compute pipeline
            Shader pathtracerComputeShader = Shader("shaders\\pathtracer.spv", Shader::Type::COMPUTE, this->device);
            VkComputePipelineCreateInfo computePipelineInfo{};
            computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            computePipelineInfo.stage.stage = pathtracerComputeShader.GetStage();
            computePipelineInfo.stage.module = pathtracerComputeShader.GetShaderModule();
            computePipelineInfo.stage.pName = "main";
            computePipelineInfo.stage.pSpecializationInfo = &specInfo;
            computePipelineInfo.layout = computePipelineLayout;
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &computePipeline);
        }

        void init(const SceneData& sceneData) override {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            
            createDescriptorSetLayout();

            for (int i = 0; i < PING_PONG_FRAMES; i++) this->computeDescriptorSet[i] = allocateDescriptorSet(&computeDescriptorSetLayout);

            for (int i = 0; i < PING_PONG_FRAMES; i++) {

                // Compute descriptor set
                updateUniformBufferDescriptorSet(computeDescriptorSet[i], 1, UBO.buffer, sizeof(Pathtracer::FrameContext));

                // Output storage image for compute
                updateStorageImageDescriptorSet(computeDescriptorSet[i], 0, pathtracerImageViews[i], VK_IMAGE_LAYOUT_GENERAL);

                // Acc image
                updateStorageImageDescriptorSet(computeDescriptorSet[i], 10, pathtracerImageViews[PATHTRACER_IMG_COUNT - 1], VK_IMAGE_LAYOUT_GENERAL);

                // Input sampler for compute (previous frame texture)
                updateCombinedImageSamplerDescriptorSet(computeDescriptorSet[i], 2, pathtracerImageSampler, pathtracerImageViews[1 - i]);
            }

            // Acc image staging buffer
            Buffer stagingAcc = Buffer(this->physicalDevice, this->device, WIDTH * HEIGHT * sizeof(glm::vec4), nullptr, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            this->stagingBuffers.push_back(std::move(stagingAcc));

            std::visit([&](auto const& nodes) {
                SSBOs.emplace_back( createStorageBuffer(nodes.size() * sizeof(nodes[0]), nodes.data() ) );
            }, sceneData.tree);

            if (this->pathtracerConfig.GetAccelerationStructureType() == Pathtracer::AccelerationStructureType::BVH) {
                createSSBO(SSBOs.back().buffer, SSBOBinding::BVH_NODES);
                // Dummy
                SSBOs.emplace_back(createStorageBuffer(4, nullptr));
                createSSBO(SSBOs.back().buffer, SSBOBinding::KDTREE_NODES);

                SSBOs.emplace_back(createStorageBuffer(4, nullptr));
                createSSBO(SSBOs.back().buffer, SSBOBinding::KDTREE_INDICES);
            }
            else {
                createSSBO(SSBOs.back().buffer, SSBOBinding::KDTREE_NODES);
                // Dummy
                SSBOs.emplace_back(createStorageBuffer(4, nullptr));
                createSSBO(SSBOs.back().buffer, SSBOBinding::BVH_NODES);

                SSBOs.emplace_back(createStorageBuffer(sceneData.indices_kdtree.size() * sizeof(sceneData.indices_kdtree[0]), sceneData.indices_kdtree.data()));
                createSSBO(SSBOs.back().buffer, SSBOBinding::KDTREE_INDICES);
            }

            SSBOs.emplace_back(createStorageBuffer(sceneData.triangles.size() * sizeof(sceneData.triangles[0]), sceneData.triangles.data()));
            createSSBO(SSBOs.back().buffer, SSBOBinding::TRIANGLES); // Triangles

            SSBOs.emplace_back(createStorageBuffer(sceneData.vertices.size() * sizeof(sceneData.vertices[0]), sceneData.vertices.data()));
            createSSBO(SSBOs.back().buffer, SSBOBinding::VERTICES); // Vertices

            SSBOs.emplace_back(createStorageBuffer(sceneData.lightTriangles.size() * sizeof(sceneData.lightTriangles[0]), sceneData.lightTriangles.data()));
            createSSBO(SSBOs.back().buffer, SSBOBinding::EMISSIVES); // Emissives/Light

            SSBOs.emplace_back(createStorageBuffer(sceneData.materials.size() * sizeof(sceneData.materials[0]), sceneData.materials.data()));
            createSSBO(SSBOs.back().buffer, SSBOBinding::MATERIALS); // Materials

            SSBOs.emplace_back(createStorageBuffer(sizeof(Pathtracer::TreeStatistics), nullptr, true));
            createSSBO(SSBOs.back().buffer, SSBOBinding::STATISTICS); // Statistics
            
            createComputePipeline();
        }

        void dispatch(const DispatchConext& dispatchCtx) override {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;

            // Bind compute pipeline and dispatch
            // Bind compute pipeline and descriptors
            vkCmdBindPipeline(dispatchCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
            vkCmdBindDescriptorSets(dispatchCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet[dispatchCtx.currentFrame % PING_PONG_FRAMES], 0, nullptr);
            
            Pathtracer::PushConstants pathtracerPC{};
            pathtracerPC.ct.tileSize = dispatchCtx.tileSize;
            pathtracerPC.lightBounces = dispatchCtx.lightBounces;

            // Dispatch compute
            // Tiling to avoid TDR
            const uint32_t TILE_Y = this->pathtracerConfig.GetTileSize().y;
            const uint32_t TILE_X = this->pathtracerConfig.GetTileSize().x;
            for (uint32_t tileY = 0; tileY < HEIGHT; tileY += TILE_Y) {
                for (uint32_t tileX = 0; tileX < WIDTH; tileX += TILE_X) {
                    pathtracerPC.ct.tileOffset = { tileX, tileY };
                    vkCmdPushConstants(dispatchCtx.commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Pathtracer::PushConstants), &pathtracerPC);
                    vkCmdDispatch(dispatchCtx.commandBuffer, (TILE_X + Pathtracer::local_size_x - 1) / Pathtracer::local_size_x, (TILE_Y + Pathtracer::local_size_y - 1) / Pathtracer::local_size_y, 1);
                }
            }

        }

        Pathtracer::TreeStatistics GetBackendStatistics() override {
            const uint32_t STAGING_STATS = 1;
            
            const Buffer& statsSSBO = this->SSBOs.back();
            const Buffer& stagingStats = this->stagingBuffers[STAGING_STATS];

            VkBufferCopy statsCopy{};
            statsCopy.srcOffset = 0;
            statsCopy.dstOffset = 0;
            statsCopy.size = sizeof(Pathtracer::TreeStatistics);

            VkCommandBuffer cmdbf = beginSingleTimeCommands();
            vkCmdCopyBuffer(cmdbf, statsSSBO.buffer, stagingStats.buffer, 1, &statsCopy);

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmdbf;

            endSingleTimeCommands(cmdbf);
            vkQueueWaitIdle(this->graphicsQueue);

            void* mappedStats = nullptr;
            vkMapMemory(this->device, stagingStats.memory, 0, sizeof(Pathtracer::TreeStatistics), 0, &mappedStats);
            Pathtracer::TreeStatistics gpuTreeStats = *reinterpret_cast<Pathtracer::TreeStatistics*>(mappedStats);
            vkUnmapMemory(this->device, stagingStats.memory);

            return gpuTreeStats;
        }

        void GetBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) override {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            const uint32_t STAGING_ACC = 0;
            VkBufferImageCopy imgCopy{};
            imgCopy.bufferOffset = 0;
            imgCopy.bufferRowLength = 0; // tightly packed
            imgCopy.bufferImageHeight = 0;
            imgCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgCopy.imageSubresource.mipLevel = 0;
            imgCopy.imageSubresource.baseArrayLayer = 0;
            imgCopy.imageSubresource.layerCount = 1;
            imgCopy.imageOffset = { 0, 0, 0 };
            imgCopy.imageExtent = { WIDTH, HEIGHT, 1 };

            VkCommandBuffer cmdbf = beginSingleTimeCommands();
            
            vkCmdCopyImageToBuffer(cmdbf, accImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, this->stagingBuffers[STAGING_ACC].buffer, 1, &imgCopy);

            endSingleTimeCommands(cmdbf);
            vkQueueWaitIdle(this->graphicsQueue);

            void* mappedPixels = nullptr;
            vkMapMemory(this->device, this->stagingBuffers[STAGING_ACC].memory, 0, VK_WHOLE_SIZE, 0, &mappedPixels);
            pixels.resize(WIDTH * HEIGHT);
            memcpy(pixels.data(), mappedPixels, WIDTH * HEIGHT * sizeof(glm::vec4));
            vkUnmapMemory(this->device, this->stagingBuffers[STAGING_ACC].memory);
        }
    };
};

#endif