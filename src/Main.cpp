#define DEBUG
#include "Pathtracer.h"
#include "CLI.h"
#include <array>

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

static Pathtracer::Config debugConfig(Pathtracer::Scene scene, Pathtracer::AccelerationStructureType acctype, Pathtracer::ComputeBackendType computeBackendType, glm::uvec2 tileSize = glm::uvec2(256, 256)) {
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
        tileSize,
        false,
        true,
        true
    );
    return pathtracerConfig;
}

static Pathtracer::Config sppConfig(Pathtracer::Scene scene, Pathtracer::AccelerationStructureType acctype, Pathtracer::ComputeBackendType computeBackendType) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::SPP;
    benchmarkInfo.spp = 64;

    Config pathtracerConfig(
        acctype,
        computeBackendType,
        scene,
        Resolution::R1024x1024,
        12,
        benchmarkInfo,
        glm::uvec2(256, 256),
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
        glm::uvec2(256, 256),
        true
    );
    return pathtracerConfig;
}

static Pathtracer::Config warmupConfig() {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::SPP;
    benchmarkInfo.spp = 128;

    Config pathtracerConfig(
        AccelerationStructureType::BVH,
        ComputeBackendType::SPIRV_T,
        Scene::CORNELL_BOX,
        Resolution::R1024x1024,
        8,
        benchmarkInfo,
        glm::uvec2(256, 256),
        false,
        false,
        false
    );
    return pathtracerConfig;
}

static Pathtracer::Config qaConfig(Pathtracer::Scene scene, Pathtracer::AccelerationStructureType acctype, Pathtracer::ComputeBackendType computeBackendType, uint32_t spp = 64, float rmseThreshold = 0.016f) {
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = BenchmarkType::IMGREF;
    benchmarkInfo.spp = spp;
    benchmarkInfo.useSPPLadder = false;
    benchmarkInfo.rmseThreshold = rmseThreshold;

    Config pathtracerConfig(
        acctype,
        computeBackendType,
        scene,
        Resolution::R1024x1024,
        8,
        benchmarkInfo,
        glm::uvec2(256, 256),
        false,
        false,
        false
    );
    return pathtracerConfig;
}

void benchmark() {
    using namespace Pathtracer;
    {
        App pathtracer(warmupConfig());
        pathtracer.run();
    }

    std::array<Scene, 5> scenes = {Scene::CORNELL_BOX, Scene::BUNNY, Scene::DRAGON, Scene::SIBENIK};
    std::array<glm::vec2, 5> tileSizes = { glm::vec2{256, 256}, glm::vec2{256, 256}, glm::vec2{32, 32}, glm::vec2{256, 256} , glm::vec2{256, 256} };
#ifdef HAS_CUDA
    std::array<ComputeBackendType, 2> computeBackends = {ComputeBackendType::SPIRV_T, ComputeBackendType::CUDA_T};
#else
    std::array<ComputeBackendType, 1> computeBackends = {ComputeBackendType::SPIRV_T};
#endif
    std::array<AccelerationStructureType, 3> accStrs = {AccelerationStructureType::BVH, AccelerationStructureType::BVH4, AccelerationStructureType::KD_TREE};
    for (Scene scene : scenes) {
        for (ComputeBackendType computeBackend : computeBackends) {
            for (AccelerationStructureType accStr : accStrs) {
                App pathtracer(sppConfig(scene, accStr, computeBackend));
                pathtracer.run();
            }
        }
    }
}

void test() {
    {
        using namespace Pathtracer;
        App pathtracer(warmupConfig());
        pathtracer.run();
    }

    {
        using namespace Pathtracer;
        App pathtracer(debugConfig(Scene::DRAGON, AccelerationStructureType::BVH, ComputeBackendType::SPIRV_T, glm::uvec2(256, 256)));
        pathtracer.run();
    }

    /*
    {
        using namespace Pathtracer;
        App pathtracer(qaConfig(Scene::CORNELL_BOX, AccelerationStructureType::BVH, ComputeBackendType::SPIRV_T));
        pathtracer.run();
    }
    */
}

void custom() {
    using namespace Pathtracer;
    CLI cli;
    cli.printCustomHeader();

    do {
        Config config = cli.promptCustomConfig();
        const bool runWarmup = cli.promptRunWarmup();
        if (runWarmup) {
            App warmup(warmupConfig());
            warmup.run();
        }

        App pathtracer(config);
        pathtracer.run();

    } while (cli.promptRunAnotherConfiguration());
}

int main() {
    CLI cli;
    switch (cli.promptRunMode()) {
    case CLI::RunMode::Test:
        test();
        break;
    case CLI::RunMode::Benchmark:
        benchmark();
        break;
    case CLI::RunMode::Custom:
        custom();
        break;
    default:
        test();
        break;
    }

    return 0;
}