#ifndef CLI_H
#define CLI_H

#include "PathtracerSettings.h"

#include <string>

class CLI {
public:
    enum class RunMode {
        Test = 1,
        Benchmark = 2,
        Custom = 3
    };

    RunMode promptRunMode() const;
    void printCustomHeader() const;
    bool promptRunWarmup() const;
    Pathtracer::Config promptCustomConfig() const;
    bool promptRunAnotherConfiguration() const;

private:
    static bool promptYesNo(const std::string& label, bool defaultYes);

    static Pathtracer::Scene sceneFromChoice(int choice);
    static Pathtracer::AccelerationStructureType accStructFromChoice(int choice);
    static Pathtracer::ComputeBackendType backendFromChoice(int choice);
    static Pathtracer::Resolution resolutionFromChoice(int choice);

    static Pathtracer::Config customConfig(
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
        float rmseThreshold,
        const std::string& customScenePath);
};

#endif
