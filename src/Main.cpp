#define DEBUG
#include "Pathtracer.h"

static Pathtracer::Config refGenConfig(Pathtracer::Scene scene) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::SPP;
    benchmarkInfo.spp = 80U * 1000U;

    Config pathtracerConfig(
        AccelerationStructureType::KD_TREE,
        ComputeBackendType::SPIRV_T,
        scene,
        Resolution::R1024x1024,
        12,
        benchmarkInfo,
        glm::uvec2(64, 64),
        true
    );
    return pathtracerConfig;
}

static Pathtracer::Config debugConfig(Pathtracer::Scene scene, Pathtracer::AccelerationStructureType acctype, Pathtracer::ComputeBackendType computeBackendType) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::SPP;
    benchmarkInfo.spp = 64;

    Config pathtracerConfig(
        acctype,
        computeBackendType,
        scene,
        Resolution::R1024x1024,
        4,
        benchmarkInfo,
        glm::uvec2(64, 64),
        true,
        true
    );
    return pathtracerConfig;
}

static Pathtracer::Config imgrefConfig(Pathtracer::Scene scene) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::IMGREF;

    Config pathtracerConfig(
        AccelerationStructureType::BVH,
        ComputeBackendType::SPIRV_T,
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
        App pathtracer(debugConfig(Scene::CORNELL_BOX, AccelerationStructureType::BVH, ComputeBackendType::CUDA_T));
        pathtracer.run();
    }

    return 0;
}