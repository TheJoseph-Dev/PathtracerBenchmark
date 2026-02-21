#ifdef HAS_CUDA
#ifndef CUDA_BACKEND_H
#define CUDA_BACKEND_H
#include "ComputeBackend.h"
#include <cuda_runtime.h>

namespace Pathtracer {
	//extern void launchKernel(float4*, float4*, int, int);

	class CUDA final : public ComputeBackend {
	public:
		float4* d_outImage = nullptr;
		float4* d_accImage = nullptr;

		struct CudaContext {

		};



		CUDA(const CudaContext& cuCtx, const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig) : ComputeBackend(vkCtx, pathtracerConfig) {
			
		}

		~CUDA() override {
			//if (d_outImage) cudaFree(d_outImage);
			//if (d_accImage) cudaFree(d_accImage);
		}

		void init(const SceneData& sceneData) override {
				
		};

		void dispatch(const DispatchConext& dispatchCtx) override {
		
		};

		void updateFrameContext(const FrameContext* newData, uint64_t size) const override {
		
		};
		TreeStatistics getBackendStatistics() override { return {}; };
		void getBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) override {};
	};
}

#endif
#endif