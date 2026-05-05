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
        Kernel::BVH4Node* d_bvh4Nodes,
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
        int accelerationStructureType,
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
        Pathtracer::GPUStatistics* d_gpuStats = nullptr;
        //PushConstants::ComputeTile* d_computeTile = nullptr;

        uint32_t triangleCount = 0;
        uint32_t emissiveTriangleCount = 0;

        //int32_t FIRST_FRAMES_WARMUP = PING_PONG_FRAMES; // Warming vulkan semaphore signals up
	public:

		CUDA(const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig);

		~CUDA() override;

    private:
        VkCommandBuffer beginSingleTimeCommands() const;
        void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
        void create2DNearestImageSampler();
        void createImage2D(uint32_t width, uint32_t height);
        void createPathtracerImages();
        void createDeviceMemory(void** dst, const void* src, size_t size);
		void init(const SceneData& sceneData) override;
        void dispatch(const DispatchConext& dispatchCtx) override;
        void dispatchBeforeGraphicsSubmit(const DispatchConext&) override;
        void dispatchAfterGraphicsSubmit(const DispatchConext& dispatchCtx) override;
        VkImageLayout getFragmentSampledImageLayout() const override;
		void updateFrameContext(const FrameContext* newData, uint64_t size) const override;
        void sync(const SyncContext& syncCtx) const override;
        double queryDispatchTime(uint32_t frameIdx, float deviceTimestampPeriod) const override;
		GPUStatistics getBackendStatistics() override;
		void getBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) override;
	};
}

#endif
#endif
