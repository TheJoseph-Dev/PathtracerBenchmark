#include "CLI.h"

#include <cctype>
#include <iostream>
#include <limits>
#include <string>

namespace {
    template <typename T>
    T promptNumeric(const std::string& label, T minValue, T maxValue) {
        T value{};
        while (true) {
            std::cout << label << " [" << minValue << "-" << maxValue << "]: ";
            if (std::cin >> value && value >= minValue && value <= maxValue)
                return value;

            std::cout << "Invalid value. Try again.\n";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }
}

CLI::RunMode CLI::promptRunMode() const {
    std::cout << "\nRun mode:\n"
              << "  1) test\n"
              << "  2) benchmark\n"
              << "  3) custom\n";

    return static_cast<RunMode>(promptNumeric<int>("Choose mode", 1, 3));
}

void CLI::printCustomHeader() const {
    std::cout << "\n=== Custom Pathtracer Runner ===\n";
}

bool CLI::promptRunWarmup() const {
    return promptYesNo("Run warmup first?", true);
}

Pathtracer::Config CLI::promptCustomConfig() const {
    using namespace Pathtracer;

    std::cout << "\nBenchmark type:\n"
              << "  1) SPP\n"
              << "  2) IMGREF (SPP ladder)\n"
              << "  3) QA (IMGREF single SPP with threshold)\n";
    int benchmarkChoice = promptNumeric<int>("Choose benchmark type", 1, 3);

    std::cout << "\nScene:\n"
              << "  1) CORNELL_BOX\n"
              << "  2) BUNNY\n"
              << "  3) DRAGON\n"
              << "  4) SIBENIK\n";
    int sceneChoice = promptNumeric<int>("Choose scene", 1, 4);

    std::cout << "\nAcceleration Structure:\n"
              << "  1) BVH\n"
              << "  2) BVH4\n"
              << "  3) KD_TREE\n";
    int accChoice = promptNumeric<int>("Choose acceleration structure", 1, 3);

    int backendChoice = 1;
#ifdef HAS_CUDA
    std::cout << "\nCompute Backend:\n"
              << "  1) SPIRV\n"
              << "  2) CUDA\n";
    backendChoice = promptNumeric<int>("Choose compute backend", 1, 2);
#else
    std::cout << "\nCompute Backend: SPIRV only (CUDA unavailable in this build)\n";
#endif

    std::cout << "\nResolution:\n"
              << "  1) 480x320\n"
              << "  2) 512x512\n"
              << "  3) 720x480\n"
              << "  4) 1080x720\n"
              << "  5) 1024x1024\n"
              << "  6) 1440x810\n"
              << "  7) 1920x1080\n";
    int resolutionChoice = promptNumeric<int>("Choose resolution", 1, 7);

    uint32_t lightBounces = promptNumeric<uint32_t>("Light bounces", 1u, 32u);
    uint32_t tileEdge = promptNumeric<uint32_t>("Tile size (x and y)", 8u, 512u);

    const bool isSPP = benchmarkChoice == 1;
    const bool isImgRefLadder = benchmarkChoice == 2;
    const bool isQA = benchmarkChoice == 3;

    uint32_t spp = isImgRefLadder ? 1u : promptNumeric<uint32_t>("SPP", 1u, 200000u);
    float rmseThreshold = isQA ? promptNumeric<float>("QA RMSE threshold", 0.0f, 1.0f) : -1.0f;

    bool saveImage = promptYesNo("Save output image?", isSPP || isImgRefLadder);
    bool getStatsAS = promptYesNo("Collect tree stats (GPU atomics)?", true);
    bool saveStatistics = promptYesNo("Save benchmark statistics to files?", true);

    BenchmarkType btype = isSPP ? BenchmarkType::SPP : BenchmarkType::IMGREF;
    return customConfig(
        sceneFromChoice(sceneChoice),
        accStructFromChoice(accChoice),
        backendFromChoice(backendChoice),
        resolutionFromChoice(resolutionChoice),
        btype,
        spp,
        lightBounces,
        glm::uvec2(tileEdge, tileEdge),
        saveImage,
        getStatsAS,
        saveStatistics,
        isImgRefLadder,
        rmseThreshold
    );
}

bool CLI::promptRunAnotherConfiguration() const {
    return promptYesNo("Run another configuration?", false);
}

bool CLI::promptYesNo(const std::string& label, bool defaultYes) {
    while (true) {
        std::cout << label << (defaultYes ? " [Y/n]: " : " [y/N]: ");

        std::string input;
        std::getline(std::cin >> std::ws, input);

        if (input.empty())
            return defaultYes;

        char c = static_cast<char>(std::tolower(static_cast<unsigned char>(input[0])));
        if (c == 'y') return true;
        if (c == 'n') return false;

        std::cout << "Please answer with y or n.\n";
    }
}

Pathtracer::Scene CLI::sceneFromChoice(int choice) {
    using namespace Pathtracer;
    switch (choice) {
    case 1: return Scene::CORNELL_BOX;
    case 2: return Scene::BUNNY;
    case 3: return Scene::DRAGON;
    case 4: return Scene::SIBENIK;
    default: return Scene::CORNELL_BOX;
    }
}

Pathtracer::AccelerationStructureType CLI::accStructFromChoice(int choice) {
    using namespace Pathtracer;
    switch (choice) {
    case 1: return AccelerationStructureType::BVH;
    case 2: return AccelerationStructureType::BVH4;
    case 3: return AccelerationStructureType::KD_TREE;
    default: return AccelerationStructureType::BVH;
    }
}

Pathtracer::ComputeBackendType CLI::backendFromChoice(int choice) {
    using namespace Pathtracer;
#ifdef HAS_CUDA
    return choice == 2 ? ComputeBackendType::CUDA_T : ComputeBackendType::SPIRV_T;
#else
    (void)choice;
    return ComputeBackendType::SPIRV_T;
#endif
}

Pathtracer::Resolution CLI::resolutionFromChoice(int choice) {
    using namespace Pathtracer;
    switch (choice) {
    case 1: return Resolution::R480x320;
    case 2: return Resolution::R512x512;
    case 3: return Resolution::R720x480;
    case 4: return Resolution::R1080x720;
    case 5: return Resolution::R1024x1024;
    case 6: return Resolution::R1440x810;
    case 7: return Resolution::R1920x1080;
    default: return Resolution::R1024x1024;
    }
}

Pathtracer::Config CLI::customConfig(
    Pathtracer::Scene scene,
    Pathtracer::AccelerationStructureType accStruct,
    Pathtracer::ComputeBackendType backend,
    Pathtracer::Resolution resolution,
    Pathtracer::BenchmarkType benchmarkType,
    uint32_t spp,
    uint32_t lightBounces,
    glm::uvec2 tileSize,
    bool saveImage,
    bool getStatsAS,
    bool saveStatistics,
    bool useSPPLadder,
    float rmseThreshold)
{
    using namespace Pathtracer;
    Benchmark benchmarkInfo = {};
    benchmarkInfo.btype = benchmarkType;
    benchmarkInfo.spp = spp;
    benchmarkInfo.useSPPLadder = useSPPLadder;
    benchmarkInfo.rmseThreshold = rmseThreshold;

    return Config(
        accStruct,
        backend,
        scene,
        resolution,
        lightBounces,
        benchmarkInfo,
        tileSize,
        saveImage,
        getStatsAS,
        saveStatistics
    );
}
