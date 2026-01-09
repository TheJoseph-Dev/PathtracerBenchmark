#include "View.h"

int main() {

    {
        using namespace Pathtracer;
        Benchmark benchmarkInfo = {};
        benchmarkInfo.btype = BenchmarkType::SPP;
        benchmarkInfo.spp = 10U * 1000U;
        
        Config pathtracerConfig(
            AccelerationStructure::BVH,
            API::VULKAN,
            Scene::CORNELL_BOX,
            Resolution::R1024x1024,
            12,
            benchmarkInfo,
            glm::uvec2(64, 64),
            true
        );

        View view(pathtracerConfig);
        view.run();
    }

    return 0;
}