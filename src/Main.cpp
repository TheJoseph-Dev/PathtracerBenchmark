#include "View.h"

int main() {

    {
        using namespace Pathtracer;
        Benchmark benchmarkInfo = {};
        benchmarkInfo.btype = BenchmarkType::SPP;
        benchmarkInfo.spp = 1024U;
        
        Config pathtracerConfig(
            AccelerationStructure::BVH,
            API::VULKAN,
            Scene::CORNELL_BOX,
            Resolution::R720x480,
            6,
            benchmarkInfo
        );

        View view(pathtracerConfig);
        view.run();
    }

    return 0;
}