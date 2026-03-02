#ifdef HAS_CUDA
#ifndef CUDA_BACKEND_H
#define CUDA_BACKEND_H
#include "ComputeBackend.h"
#include "pathtracer_kernel_types.h"
#include <cuda_runtime.h>

#ifdef DEBUG
//#define NVTX_USE_ANSI_STRING
//#define NVTX_NO_LIB
//#include <nvtx3/nvToolsExt.h>
#endif

#define CUDA_CHECK(x) \
    do { cudaError_t err = x; if (err != cudaSuccess) \
        throw std::runtime_error(cudaGetErrorString(err)); } while(0)

namespace Kernel {
    extern "C" void dispatchCUDAPathtracerKernel(
        cudaSurfaceObject_t d_outImage,
        Kernel::vec4* d_accImage,
        Kernel::BVHNode* d_bvhNodes,
        Kernel::KdNode* d_kdNodes,
        unsigned int* d_kdtreeIndices,
        Kernel::Triangle* d_triangles,
        Kernel::Vertex* d_vertices,
        Kernel::Triangle* d_eTriangles,
        Kernel::Material* d_mats,
        Kernel::Statistics* d_statistics,
        Kernel::ComputeTile ct,
        const Kernel::PathtracerState* state,
        unsigned int triangleCount,
        unsigned int lightCount,
        unsigned int lightBounces,
        bool USE_BVH,
        bool USE_STATS
    );
};

namespace Pathtracer {
    
	class CUDA final : public ComputeBackend {
		PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR = nullptr;
        PFN_vkGetSemaphoreWin32HandleKHR vkGetSemaphoreWin32HandleKHR = nullptr;

        cudaStream_t computeStream;
        cudaEvent_t startEvents[PING_PONG_FRAMES], stopEvents[PING_PONG_FRAMES];

        struct CudaImage {
            cudaExternalMemory_t externalMemory;
            cudaMipmappedArray_t mipArray;
            cudaArray_t level0;
            cudaSurfaceObject_t surface;

            VkSemaphore vkSignalSemaphore;
            VkSemaphore vkWaitSemaphore;
            cudaExternalSemaphore_t cudaSignalSemaphore;
            cudaExternalSemaphore_t cudaWaitSemaphore;
        };

        CudaImage cudaImages[PING_PONG_FRAMES];

        enum DeviceBufferIndex {
            BVH_NODES = 0,
            KDTREE_NODES = 0,
            KDTREE_INDICES = 1,
            TRIANGLES = 2,
            VERTICES = 3,
            EMISSIVES = 4,
            MATERIALS = 5
        };

		Kernel::vec4* d_accImage = nullptr;
        std::vector<void*> sceneDeviceBuffers;
        Pathtracer::FrameContext* d_frameContext = nullptr;
        Pathtracer::TreeStatistics* d_treeStats = nullptr;
        //PushConstants::ComputeTile* d_computeTile = nullptr;

        uint32_t triangleCount = 0;
        uint32_t emissiveTriangleCount = 0;

        //int32_t FIRST_FRAMES_WARMUP = PING_PONG_FRAMES; // Warming vulkan semaphore signals up
	public:

		CUDA(const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig) : ComputeBackend(vkCtx, pathtracerConfig) {
			this->vkGetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(vkCtx.device, "vkGetMemoryWin32HandleKHR");
            this->vkGetSemaphoreWin32HandleKHR = (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(vkCtx.device, "vkGetSemaphoreWin32HandleKHR");
            CUDA_CHECK(cudaStreamCreateWithFlags(&this->computeStream, cudaStreamNonBlocking));
		}

		~CUDA() override {
            if (d_accImage) cudaFree(d_accImage);
			if (d_treeStats) cudaFree(d_treeStats);
            if (d_frameContext) cudaFree(d_frameContext);
            for (void* bptr : sceneDeviceBuffers) if (bptr) cudaFree(bptr);
            for (uint32_t i = 0; i < PING_PONG_FRAMES; i++) {
                cudaDestroyExternalMemory(this->cudaImages[i].externalMemory);
                cudaDestroyExternalSemaphore(this->cudaImages[i].cudaSignalSemaphore);
                vkDestroySemaphore(this->vkCtx.device, this->cudaImages[i].vkSignalSemaphore, nullptr);
                cudaDestroyExternalSemaphore(this->cudaImages[i].cudaWaitSemaphore);
                vkDestroySemaphore(this->vkCtx.device, this->cudaImages[i].vkWaitSemaphore, nullptr);
            }
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

            int count;
            cudaGetDeviceCount(&count);

            for (int i = 0; i < count; i++) {
                cudaDeviceProp p;
                cudaGetDeviceProperties(&p, i);
                printf("CUDA Device %d: %s\n", i, p.name);
            }

            // Double Buffer
            for (int i = 0; i < PATHTRACER_IMG_COUNT; i++) {
                this->pathtracerImages[i] = {};
                this->pathtracerImagesMemory[i] = {};
                this->pathtracerImageViews[i] = {};

                VkExternalMemoryImageCreateInfo externalImageInfo{};
                externalImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
                externalImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

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
                dataImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                dataImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                dataImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                dataImageInfo.pNext = &externalImageInfo;

                vkCreateImage(this->vkCtx.device, &dataImageInfo, nullptr, &this->pathtracerImages[i]);

                VkMemoryRequirements memRequirements;
                vkGetImageMemoryRequirements(this->vkCtx.device, this->pathtracerImages[i], &memRequirements);

                VkMemoryDedicatedAllocateInfo dedicatedAllocInfo{};
                dedicatedAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
                dedicatedAllocInfo.image = this->pathtracerImages[i];

                VkExportMemoryAllocateInfo exportAllocInfo{};
                exportAllocInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
                exportAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
                exportAllocInfo.pNext = &dedicatedAllocInfo;

                VkMemoryAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                allocInfo.allocationSize = memRequirements.size;
                allocInfo.memoryTypeIndex = Vulkan::findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkCtx.physicalDevice);
                allocInfo.pNext = &exportAllocInfo;

                if (vkAllocateMemory(this->vkCtx.device, &allocInfo, nullptr, &this->pathtracerImagesMemory[i]) != VK_SUCCESS) {
                    throw std::runtime_error("failed to allocate image memory!");
                }

                vkBindImageMemory(this->vkCtx.device, pathtracerImages[i], pathtracerImagesMemory[i], 0);

                if (i < PING_PONG_FRAMES) {
                    VkMemoryGetWin32HandleInfoKHR memHandleInfo{};
                    memHandleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
                    memHandleInfo.memory = pathtracerImagesMemory[i];
                    memHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

                    HANDLE win32MemoryHandle;
                    vkGetMemoryWin32HandleKHR(vkCtx.device, &memHandleInfo, &win32MemoryHandle);

                    cudaExternalMemoryHandleDesc memDesc{};
                    memDesc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
                    memDesc.handle.win32.handle = win32MemoryHandle;
                    memDesc.size = memRequirements.size;
                    memDesc.flags = cudaExternalMemoryDedicated;

                    cudaExternalMemory_t cudaExtMem;
                    CUDA_CHECK(cudaImportExternalMemory(&cudaExtMem, &memDesc));

                    CloseHandle(win32MemoryHandle);

                    VkExportSemaphoreCreateInfo exportInfo{};
                    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
                    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

                    VkSemaphoreCreateInfo semaphoreInfo{};
                    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                    semaphoreInfo.pNext = &exportInfo;

                    auto createInteropSemaphore = [&](VkSemaphore& vkSemaphore, cudaExternalSemaphore_t& cudaSemaphore) -> void {
                        vkCreateSemaphore(vkCtx.device, &semaphoreInfo, nullptr, &vkSemaphore);
                        VkSemaphoreGetWin32HandleInfoKHR semHandleInfo{};
                        semHandleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
                        semHandleInfo.semaphore = vkSemaphore;
                        semHandleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

                        HANDLE win32SemaphoreHandle;
                        vkGetSemaphoreWin32HandleKHR(vkCtx.device, &semHandleInfo, &win32SemaphoreHandle);

                        cudaExternalSemaphoreHandleDesc semDesc{};
                        semDesc.type = cudaExternalSemaphoreHandleTypeOpaqueWin32;
                        semDesc.handle.win32.handle = win32SemaphoreHandle;

                        CUDA_CHECK(cudaImportExternalSemaphore(&cudaSemaphore, &semDesc));
                        CloseHandle(win32SemaphoreHandle);
                    };

                    VkSemaphore vkSignalSemaphore, vkWaitSemaphore;
                    cudaExternalSemaphore_t cudaSignalSemaphore, cudaWaitSemaphore;
                    createInteropSemaphore(vkSignalSemaphore, cudaSignalSemaphore);
                    createInteropSemaphore(vkWaitSemaphore, cudaWaitSemaphore);

                    cudaExternalMemoryMipmappedArrayDesc arrayDesc{};
                    arrayDesc.offset = 0;
                    arrayDesc.formatDesc = cudaCreateChannelDesc<float4>();
                    arrayDesc.extent = make_cudaExtent(width, height, 1);  // depth = 1
                    arrayDesc.numLevels = 1;
                    arrayDesc.flags = cudaArraySurfaceLoadStore | cudaArrayColorAttachment;

                    cudaMipmappedArray_t mipArray;
                    CUDA_CHECK(cudaExternalMemoryGetMappedMipmappedArray(&mipArray, cudaExtMem, &arrayDesc));

                    cudaArray_t level0;
                    CUDA_CHECK(cudaGetMipmappedArrayLevel(&level0, mipArray, 0));

                    cudaResourceDesc resDesc{};
                    memset(&resDesc, 0, sizeof(resDesc));
                    resDesc.resType = cudaResourceTypeArray;
                    resDesc.res.array.array = level0;

                    cudaSurfaceObject_t surface = 0;
                    CUDA_CHECK(cudaCreateSurfaceObject(&surface, &resDesc));

                    cudaImages[i] = { cudaExtMem, mipArray, level0, surface, vkSignalSemaphore, vkWaitSemaphore, cudaSignalSemaphore, cudaWaitSemaphore };
                }

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

                /*
                VkCommandBuffer cmdbf = beginSingleTimeCommands();
                //Vulkan::transitionImageLayout(this->pathtracerImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, imgLayout, cmdbf);
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.image = pathtracerImages[i];
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = 0;

                vkCmdPipelineBarrier(
                    cmdbf,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    0,
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );

                pathtracerImageLayouts[i] = VK_IMAGE_LAYOUT_GENERAL;
                endSingleTimeCommands(cmdbf);
                */
            }

        }

        void createPathtracerImages() {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            createImage2D(WIDTH, HEIGHT);
            create2DNearestImageSampler();
        }

        void createDeviceMemory(void** dst, const void* src, size_t size) {
            if (!size) {
                *dst = nullptr;
                return;
            }
            CUDA_CHECK(cudaMalloc(dst, size));
            if (src) CUDA_CHECK(cudaMemcpy(*dst, src, size, cudaMemcpyHostToDevice));
            //else CUDA_CHECK(cudaMemset(*dst, 0, size));
        }

		void init(const SceneData& sceneData) override {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            createPathtracerImages();

            sceneDeviceBuffers.reserve(10);

            sceneDeviceBuffers.push_back(nullptr);
            std::visit([&](auto const& nodes) {
                createDeviceMemory(&sceneDeviceBuffers.back(), (void*)nodes.data(), nodes.size() * sizeof(nodes[0]));
             }, sceneData.tree);

            sceneDeviceBuffers.push_back(nullptr);
            if (this->pathtracerConfig.GetAccelerationStructureType() == Pathtracer::AccelerationStructureType::KD_TREE)
                createDeviceMemory(&sceneDeviceBuffers.back(), (void*)sceneData.indices_kdtree.data(), sceneData.indices_kdtree.size() * sizeof(sceneData.indices_kdtree[0]));

            sceneDeviceBuffers.push_back(nullptr);
            createDeviceMemory(&sceneDeviceBuffers.back(), (void*)sceneData.triangles.data(), sceneData.triangles.size() * sizeof(sceneData.triangles[0]));
            this->triangleCount = sceneData.triangles.size();

            sceneDeviceBuffers.push_back(nullptr);
            createDeviceMemory(&sceneDeviceBuffers.back(), (void*)sceneData.vertices.data(), sceneData.vertices.size() * sizeof(sceneData.vertices[0]));
            
            sceneDeviceBuffers.push_back(nullptr);
            createDeviceMemory(&sceneDeviceBuffers.back(), (void*)sceneData.lightTriangles.data(), sceneData.lightTriangles.size() * sizeof(sceneData.lightTriangles[0]));
            this->emissiveTriangleCount = sceneData.lightTriangles.size();
            
            sceneDeviceBuffers.push_back(nullptr);
            createDeviceMemory(&sceneDeviceBuffers.back(), (void*)sceneData.materials.data(), sceneData.materials.size() * sizeof(sceneData.materials[0]));

            cudaMalloc(&d_frameContext, sizeof(Pathtracer::FrameContext));
            cudaMalloc(&d_treeStats, sizeof(Pathtracer::TreeStatistics));
            cudaMemset(d_treeStats, 0, sizeof(Pathtracer::TreeStatistics));
            cudaMalloc(&d_accImage, WIDTH*HEIGHT*sizeof(Kernel::vec4));

            for (uint32_t i = 0; i < PING_PONG_FRAMES; i++) {
                cudaEventCreate(&startEvents[i]);
                cudaEventCreate(&stopEvents[i]);

                cudaExternalSemaphoreSignalParams signalParams{};
                CUDA_CHECK(cudaSignalExternalSemaphoresAsync(
                    &this->cudaImages[i].cudaWaitSemaphore, &signalParams, 1, this->computeStream));
            }

            CUDA_CHECK(cudaStreamSynchronize(this->computeStream));
		};

		void dispatch(const DispatchConext& dispatchCtx) override {
            cudaExternalSemaphoreWaitParams waitParams{};
            cudaError_t err = cudaWaitExternalSemaphoresAsync(&this->cudaImages[dispatchCtx.currentFrame].cudaSignalSemaphore, &waitParams, 1, this->computeStream);
            if (err != cudaSuccess)
                printf("CUDA Wait error: %s\n", cudaGetErrorString(err));

            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            bool USE_BVH = pathtracerConfig.GetAccelerationStructureType() == AccelerationStructureType::BVH;
            bool USE_STATS = pathtracerConfig.ShouldGetStatsAS();

            Kernel::ComputeTile ct = { .tileSize = uint2(dispatchCtx.tileSize.x, dispatchCtx.tileSize.y) };

            cudaEventRecord(this->startEvents[dispatchCtx.currentFrame], this->computeStream);

            const uint32_t TILE_Y = this->pathtracerConfig.GetTileSize().y;
            const uint32_t TILE_X = this->pathtracerConfig.GetTileSize().x;
            for (uint32_t tileY = 0; tileY < HEIGHT; tileY += TILE_Y) {
                for (uint32_t tileX = 0; tileX < WIDTH; tileX += TILE_X) {
                    ct.tileOffset = { tileX, tileY };
                    //nvtxRangePush("CUDA Pathtracer Tile");
                    
                    Kernel::dispatchCUDAPathtracerKernel(
                        this->cudaImages[dispatchCtx.currentFrame].surface,
                        (Kernel::vec4*)this->d_accImage,
                        USE_BVH ? (Kernel::BVHNode*)this->sceneDeviceBuffers[DeviceBufferIndex::BVH_NODES] : nullptr,
                        !USE_BVH ? (Kernel::KdNode*)this->sceneDeviceBuffers[DeviceBufferIndex::KDTREE_NODES] : nullptr,
                        (uint32_t*)this->sceneDeviceBuffers[DeviceBufferIndex::KDTREE_INDICES],
                        (Kernel::Triangle*)this->sceneDeviceBuffers[DeviceBufferIndex::TRIANGLES],
                        (Kernel::Vertex*)this->sceneDeviceBuffers[DeviceBufferIndex::VERTICES],
                        (Kernel::Triangle*)this->sceneDeviceBuffers[DeviceBufferIndex::EMISSIVES],
                        (Kernel::Material*)this->sceneDeviceBuffers[DeviceBufferIndex::MATERIALS],
                        (Kernel::Statistics*)this->d_treeStats,
                        ct,
                        (Kernel::PathtracerState*)this->d_frameContext,
                        triangleCount,
                        emissiveTriangleCount,
                        dispatchCtx.lightBounces,
                        USE_BVH,
                        USE_STATS
                    );
                    //nvtxRangePop();
                    #ifdef DEBUG
                    //cudaStreamSynchronize(this->computeStream);
                    /*cudaError_t err = cudaGetLastError();
                    if (err != cudaSuccess)
                        printf("Kernel launch error: %s (%d, %d)\n", cudaGetErrorString(err), tileX, tileY);*/
                    #endif
                }
            }

            if (USE_STATS) { // Required to measure the accurate kernel time
                cudaDeviceSynchronize();
                cudaEventSynchronize(this->stopEvents[dispatchCtx.currentFrame]);
            }

            cudaEventRecord(this->stopEvents[dispatchCtx.currentFrame], this->computeStream);
            
            /*
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

            barrier.srcAccessMask = 0; // external
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            barrier.dstQueueFamilyIndex = 0;

            barrier.image = pathtracerImages[dispatchCtx.currentFrame];

            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(
                dispatchCtx.commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
            */
            cudaExternalSemaphoreSignalParams signalParams{};
            cudaError_t e = cudaSignalExternalSemaphoresAsync(&this->cudaImages[dispatchCtx.currentFrame].cudaWaitSemaphore, &signalParams, 1, this->computeStream);
            if (e != cudaSuccess)
                printf("CUDA Signal error: %s\n", cudaGetErrorString(e));
		};

		void updateFrameContext(const FrameContext* newData, uint64_t size) const override {
            cudaMemcpy(this->d_frameContext, newData, size, cudaMemcpyHostToDevice);
		};

        void sync(const SyncContext& syncCtx) const override {
            syncCtx.waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            syncCtx.waitSemaphores.push_back(this->cudaImages[syncCtx.currentFrame].vkWaitSemaphore);
            syncCtx.signalSemaphores.push_back(this->cudaImages[syncCtx.currentFrame].vkSignalSemaphore);
        }

        double queryDispatchTime(uint32_t frameIdx, float deviceTimestampPeriod) const override {
            cudaDeviceSynchronize();
            cudaEventSynchronize(stopEvents[frameIdx]);
            float ms;
            cudaEventElapsedTime(&ms, startEvents[frameIdx], stopEvents[frameIdx]);
            return ms;
        }

		TreeStatistics getBackendStatistics() override { 
            TreeStatistics ts;
            cudaDeviceSynchronize();
            cudaMemcpy(&ts, this->d_treeStats, sizeof(Pathtracer::TreeStatistics), cudaMemcpyDeviceToHost);
            return ts;
        };
		void getBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) override {
            const uint32_t WIDTH = this->pathtracerConfig.GetResolution().x, HEIGHT = this->pathtracerConfig.GetResolution().y;
            pixels.resize(WIDTH * HEIGHT);
            cudaDeviceSynchronize();
            cudaMemcpy(pixels.data(), d_accImage, WIDTH * HEIGHT * sizeof(glm::vec4), cudaMemcpyDeviceToHost);
        };
	};
}

#endif
#endif