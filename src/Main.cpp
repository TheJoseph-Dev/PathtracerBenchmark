//#define DEBUG
#include "Pathtracer.h"

//using namespace Pathtracer;

Pathtracer::Config refGenConfig(Pathtracer::Scene scene) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::SPP;
    benchmarkInfo.spp = 80U * 1000U;

    Config pathtracerConfig(
        AccelerationStructure::BVH,
        API::VULKAN,
        scene,
        Resolution::R1024x1024,
        12,
        benchmarkInfo,
        glm::uvec2(64, 64),
        true
    );
    return pathtracerConfig;
}

int main() {

    {   
        using namespace Pathtracer;
        Benchmark benchmarkInfo = {};
        benchmarkInfo.btype = BenchmarkType::IMGREF;
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

        App pathtracer(refGenConfig(Scene::CORNELL_BOX));
        pathtracer.run();
    }

    return 0;
}