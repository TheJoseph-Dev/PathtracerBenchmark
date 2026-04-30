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

void ComputeBackend::dispatchBeforeGraphicsSubmit(const DispatchConext& dispatchCtx) {
    dispatch(dispatchCtx);
}

void ComputeBackend::dispatchAfterGraphicsSubmit(const DispatchConext&) {}

VkImageLayout ComputeBackend::getFragmentSampledImageLayout() const {
    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

VkImageView ComputeBackend::getPathtracerImageView(uint32_t index) const {
    return this->pathtracerImageViews[index];
}

} // namespace Pathtracer
