#include "ComputeBackend.h"

namespace Pathtracer {

ComputeBackend::ComputeBackend(const VulkanContext& vkCtx, const Pathtracer::Config& pathtracerConfig) : vkCtx(vkCtx), pathtracerConfig(pathtracerConfig) {

}

ComputeBackend::~ComputeBackend() {
    for (uint32_t i = 0; i < PATHTRACER_IMG_COUNT; ++i) {
        vkDestroyImageView(vkCtx.device, pathtracerImageViews[i], nullptr);
        vkDestroyImage(vkCtx.device, pathtracerImages[i], nullptr);
        vkFreeMemory(vkCtx.device, pathtracerImagesMemory[i], nullptr);
    }
    vkDestroySampler(this->vkCtx.device, this->pathtracerImageSampler, nullptr);
}

void ComputeBackend::dispatchBeforeQueueSubmit(const DispatchConext& dispatchCtx) {
    dispatch(dispatchCtx);
}

void ComputeBackend::dispatchAfterQueueSubmit(const DispatchConext&) {}

VkImageView ComputeBackend::getPathtracerImageView(uint32_t index) const {
    return this->pathtracerImageViews[index];
}

} // namespace Pathtracer
