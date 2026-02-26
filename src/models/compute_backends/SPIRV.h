#ifndef SPIRV_BACKEND_H
#define SPIRV_BACKEND_H

#include "ComputeBackend.h"
#include "models/vulkan/Shader.h"

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

        SPIRV(const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig) : ComputeBackend(vkCtx, pathtracerConfig) {
            //this->computeDescriptorSet.resize(PING_PONG_FRAMES);
            this->UBO = Buffer(vkCtx.physicalDevice, vkCtx.device, sizeof(Pathtracer::FrameContext), 0, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            this->SSBOs.reserve(SSBOsCount);
        }

        ~SPIRV() override {
            vkDestroyDescriptorSetLayout(vkCtx.device, computeDescriptorSetLayout, nullptr);
            vkDestroyPipeline(vkCtx.device, computePipeline, nullptr);
            vkDestroyPipelineLayout(vkCtx.device, computePipelineLayout, nullptr);
            for (uint32_t i = 0; i < PING_PONG_FRAMES; i++) vkDestroyQueryPool(vkCtx.device, timestampPools[i], nullptr);
            SSBOs.clear();
            stagingBuffers.clear();
        }

        void updateFrameContext(const FrameContext* newData, uint64_t size) const override {
            void* data;
            vkMapMemory(vkCtx.device, this->UBO.memory, 0, size, 0, &data);
            memcpy(data, newData, (size_t)size);
            vkUnmapMemory(vkCtx.device, this->UBO.memory);
        }
    private:
	    
        VkCommandBuffer beginSingleTimeCommands() const {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = this->vkCtx.commandPool;
            allocInfo.commandBufferCount = 1;

            VkCommandBuffer commandBuffer;
            vkAllocateCommandBuffers(this->vkCtx.device, &allocInfo, &commandBuffer);

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

            vkQueueSubmit(this->vkCtx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(this->vkCtx.graphicsQueue);

            vkFreeCommandBuffers(this->vkCtx.device, this->vkCtx.commandPool, 1, &commandBuffer);
        }

        void create2DNearestImageSampler() {
            VkSamplerCreateInfo dataSamplerInfo{};
            dataSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            dataSamplerInfo.magFilter = VK_FILTER_NEAREST;
            dataSamplerInfo.minFilter = VK_FILTER_NEAREST;
            dataSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            dataSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            dataSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            dataSamplerInfo.anisotropyEnable = VK_FALSE;
            dataSamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            dataSamplerInfo.unnormalizedCoordinates = VK_FALSE;

            vkCreateSampler(this->vkCtx.device, &dataSamplerInfo, nullptr, &this->pathtracerImageSampler);
        }

        void createImage2D(uint32_t width, uint32_t height) {

            // Double Buffer
            for (int i = 0; i < PATHTRACER_IMG_COUNT; i++) {
                this->pathtracerImages[i] = {};
                this->pathtracerImagesMemory[i] = {};
                this->pathtracerImageViews[i] = {};

                VkImageCreateInfo dataImageInfo{};
                dataImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                dataImageInfo.imageType = VK_IMAGE_TYPE_2D;
                dataImageInfo.extent.width = width;
                dataImageInfo.extent.height = height;
                dataImageInfo.extent.depth = 1;
                dataImageInfo.mipLevels = 1;
                dataImageInfo.arrayLayers = 1;
                dataImageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                dataImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                dataImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                dataImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                dataImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                dataImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

                vkCreateImage(this->vkCtx.device, &dataImageInfo, nullptr, &this->pathtracerImages[i]);

                VkMemoryRequirements memRequirements;
                vkGetImageMemoryRequirements(this->vkCtx.device, this->pathtracerImages[i], &memRequirements);

                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = Vulkan::findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkCtx.physicalDevice);

                if (vkAllocateMemory(this->vkCtx.device, &allocInfo, nullptr, &this->pathtracerImagesMemory[i]) != VK_SUCCESS) {
                    throw std::runtime_error("failed to allocate image memory!");
                }

                vkBindImageMemory(this->vkCtx.device, pathtracerImages[i], pathtracerImagesMemory[i], 0);

                VkImageViewCreateInfo dataTextureInfo{};
                dataTextureInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                dataTextureInfo.image = this->pathtracerImages[i];
                dataTextureInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                dataTextureInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                dataTextureInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                dataTextureInfo.subresourceRange.baseMipLevel = 0;
                dataTextureInfo.subresourceRange.levelCount = 1;
                dataTextureInfo.subresourceRange.baseArrayLayer = 0;
                dataTextureInfo.subresourceRange.layerCount = 1;

                vkCreateImageView(this->vkCtx.device, &dataTextureInfo, nullptr, &pathtracerImageViews[i]);

                VkImageLayout imgLayout = i != PATHTRACER_IMG_COUNT - 1 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                VkCommandBuffer cmdbf = beginSingleTimeCommands();
                Vulkan::transitionImageLayout(this->pathtracerImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, imgLayout, cmdbf);
                endSingleTimeCommands(cmdbf);
                pathtracerImageLayouts[i] = imgLayout;
            }

        }

        void createPathtracerImages() {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            createImage2D(WIDTH, HEIGHT);
            create2DNearestImageSampler();
        }

        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
            VkCommandBuffer commandBuffer = beginSingleTimeCommands();

            VkBufferCopy copyRegion{};
            copyRegion.size = size;
            vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

            endSingleTimeCommands(commandBuffer);
        }

        Buffer createStorageBuffer(VkDeviceSize size, const void* initialData, bool keepStaging = false) {
            Buffer staging = Buffer(this->vkCtx.physicalDevice, this->vkCtx.device, size, initialData, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT); // = createStagingBuffer(size, initialData);

            // Create device-local GPU buffer
            Buffer gpuBuffer = Buffer(this->vkCtx.physicalDevice, this->vkCtx.device, size, nullptr, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            
            // Copy staging => device-local buffer
            copyBuffer(staging.buffer, gpuBuffer.buffer, size);

            if (keepStaging) this->stagingBuffers.push_back(std::move(staging));

            return gpuBuffer; // C++ guarantees it stays alive, so ~Buffer not called here
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
                vkUpdateDescriptorSets(vkCtx.device, 1, &write, 0, nullptr);
            }
        }

        void createDescriptorSetLayout() {
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = bindingCount;
            layoutInfo.pBindings = this->computeBindings;

            vkCreateDescriptorSetLayout(this->vkCtx.device, &layoutInfo, nullptr, &this->computeDescriptorSetLayout);
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

            vkCreatePipelineLayout(vkCtx.device, &computePipelineLayoutInfo, nullptr, &computePipelineLayout);

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
            Shader pathtracerComputeShader = Shader("shaders\\pathtracer.spv", Shader::Type::COMPUTE, this->vkCtx.device);
            VkComputePipelineCreateInfo computePipelineInfo{};
            computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            computePipelineInfo.stage.stage = pathtracerComputeShader.GetStage();
            computePipelineInfo.stage.module = pathtracerComputeShader.GetShaderModule();
            computePipelineInfo.stage.pName = "main";
            computePipelineInfo.stage.pSpecializationInfo = &specInfo;
            computePipelineInfo.layout = computePipelineLayout;
            vkCreateComputePipelines(vkCtx.device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &computePipeline);
        }

        void init(const SceneData& sceneData) override {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            
            createPathtracerImages();
            createDescriptorSetLayout();

            for (int i = 0; i < PING_PONG_FRAMES; i++) this->computeDescriptorSet[i] = Vulkan::allocateDescriptorSet(vkCtx.device, vkCtx.descriptorPool, &computeDescriptorSetLayout);

            for (int i = 0; i < PING_PONG_FRAMES; i++) {

                // Compute descriptor set
                Vulkan::updateUniformBufferDescriptorSet(vkCtx.device, computeDescriptorSet[i], 1, UBO.buffer, sizeof(Pathtracer::FrameContext));

                // Output storage image for compute
                Vulkan::updateStorageImageDescriptorSet(vkCtx.device, computeDescriptorSet[i], 0, pathtracerImageViews[i], VK_IMAGE_LAYOUT_GENERAL);

                // Acc image
                Vulkan::updateStorageImageDescriptorSet(vkCtx.device, computeDescriptorSet[i], 10, pathtracerImageViews[PATHTRACER_IMG_COUNT - 1], VK_IMAGE_LAYOUT_GENERAL);

                // Input sampler for compute (previous frame texture)
                Vulkan::updateCombinedImageSamplerDescriptorSet(vkCtx.device, computeDescriptorSet[i], 2, pathtracerImageSampler, pathtracerImageViews[1 - i]);
            }

            // Acc image staging buffer
            Buffer stagingAcc = Buffer(this->vkCtx.physicalDevice, this->vkCtx.device, WIDTH * HEIGHT * sizeof(glm::vec4), nullptr, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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

            VkQueryPoolCreateInfo qpci{};
            qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qpci.queryCount = 2;

            for (int i = 0; i < PING_PONG_FRAMES; i++)
                vkCreateQueryPool(vkCtx.device, &qpci, nullptr, &timestampPools[i]);
        }

        void dispatch(const DispatchConext& dispatchCtx) override {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            // Transition SHADER_READ_ONLY_OPTIMAL -> GENERAL for compute
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = pathtracerImages[dispatchCtx.textureIndex];
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            // Track old layout per frame
            //printf("=== (COMPUTE) BEFORE BARRIER LAYOUT: %d ===\n", pathtracerImageLayouts[textureIndex]);
            barrier.oldLayout = pathtracerImageLayouts[dispatchCtx.textureIndex];
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcAccessMask = (barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            //printf("[FRAME %d] Pre-compute barrier: oldLayout=%d newLayout=%d\n", currentFrame, barrier.oldLayout, barrier.newLayout);

            vkCmdPipelineBarrier(
                dispatchCtx.commandBuffer,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            pathtracerImageLayouts[dispatchCtx.textureIndex] = VK_IMAGE_LAYOUT_GENERAL;

            // Bind compute pipeline and dispatch
            // Bind compute pipeline and descriptors
            vkCmdBindPipeline(dispatchCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
            vkCmdBindDescriptorSets(dispatchCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet[dispatchCtx.currentFrame % PING_PONG_FRAMES], 0, nullptr);
            
            vkCmdResetQueryPool(dispatchCtx.commandBuffer, timestampPools[dispatchCtx.currentFrame % PING_PONG_FRAMES], 0, 2);
            vkCmdWriteTimestamp(dispatchCtx.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPools[dispatchCtx.currentFrame % PING_PONG_FRAMES], 0);

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

            vkCmdWriteTimestamp(dispatchCtx.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, timestampPools[dispatchCtx.currentFrame % PING_PONG_FRAMES], 1);

            // Transition GENERAL -> SHADER_READ_ONLY_OPTIMAL for graphics
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                dispatchCtx.commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
            pathtracerImageLayouts[dispatchCtx.textureIndex] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        void sync(const SyncContext& syncCtx) const override {}

        double queryDispatchTime(uint32_t frameIdx, float deviceTimestampPeriod) const override {
            uint64_t timestamps[2];
            vkGetQueryPoolResults(vkCtx.device, timestampPools[frameIdx], 0, 2, sizeof(timestamps), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            uint64_t delta = timestamps[1] - timestamps[0];
            double gpuTimeNs = double(delta) * deviceTimestampPeriod;
            return gpuTimeNs / 1e6;
        }

        Pathtracer::TreeStatistics getBackendStatistics() override {
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
            vkQueueWaitIdle(this->vkCtx.graphicsQueue);

            void* mappedStats = nullptr;
            vkMapMemory(this->vkCtx.device, stagingStats.memory, 0, sizeof(Pathtracer::TreeStatistics), 0, &mappedStats);
            Pathtracer::TreeStatistics gpuTreeStats = *reinterpret_cast<Pathtracer::TreeStatistics*>(mappedStats);
            vkUnmapMemory(this->vkCtx.device, stagingStats.memory);

            return gpuTreeStats;
        }

        void getBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) override {
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
            VkImage accImg = this->pathtracerImages[PATHTRACER_IMG_COUNT - 1];
            
            Vulkan::transitionImageLayout(accImg, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmdbf);

            vkCmdCopyImageToBuffer(cmdbf, accImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, this->stagingBuffers[STAGING_ACC].buffer, 1, &imgCopy);

            endSingleTimeCommands(cmdbf);
            vkQueueWaitIdle(this->vkCtx.graphicsQueue);

            void* mappedPixels = nullptr;
            vkMapMemory(this->vkCtx.device, this->stagingBuffers[STAGING_ACC].memory, 0, VK_WHOLE_SIZE, 0, &mappedPixels);
            pixels.resize(WIDTH * HEIGHT);
            memcpy(pixels.data(), mappedPixels, WIDTH * HEIGHT * sizeof(glm::vec4));
            vkUnmapMemory(this->vkCtx.device, this->stagingBuffers[STAGING_ACC].memory);
        }

        private:
            
    };
};

#endif