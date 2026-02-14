#define DEBUG
#include "Pathtracer.h"

Pathtracer::Config refGenConfig(Pathtracer::Scene scene) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::SPP;
    benchmarkInfo.spp = 80U * 1000U;

    Config pathtracerConfig(
        AccelerationStructureType::KD_TREE,
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

Pathtracer::Config debugConfig(Pathtracer::Scene scene, Pathtracer::AccelerationStructureType acctype) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::SPP;
    benchmarkInfo.spp = 100;

    Config pathtracerConfig(
        acctype,
        API::VULKAN,
        scene,
        Resolution::R1024x1024,
        4,
        benchmarkInfo,
        glm::uvec2(64, 64),
        false,
        true
    );
    return pathtracerConfig;
}

Pathtracer::Config imgrefConfig(Pathtracer::Scene scene) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::IMGREF;

    Config pathtracerConfig(
        AccelerationStructureType::BVH,
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
        App pathtracer(debugConfig(Scene::CORNELL_BOX, AccelerationStructureType::KD_TREE));
        pathtracer.run();
    }

    return 0;
}