#ifndef CUDA_H
#define CUDA_H
#include "ComputeBackend.h"

namespace Pathtracer {
	class CUDA final : public ComputeBackend {
	public:
		CUDA(const CudaContext& cuCtx) {

		}

		void init(const SceneData& sceneData) override {};
		void dispatch(const DispatchConext& dispatchCtx) override {};
		void updateFrameContext(const FrameContext* newData, VkDeviceSize size) const override {};
		TreeStatistics GetBackendStatistics() override { return {}; };
		void GetBackendAccOutImgPixels(std::vector<glm::vec4>& pixels) override {};
	};
}

#endif