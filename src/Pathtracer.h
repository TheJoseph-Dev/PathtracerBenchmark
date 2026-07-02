#ifndef PATHTRACER_H
#define PATHTRACER_H
#include "Renderer.h"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Pathtracer {

    class App {

        GLFWwindow* window;
        Config config;
        std::optional<Renderer> vulkanRenderer;

    public:
        App(const Pathtracer::Config& config): config(config) {
            initWindow();
        }

        ~App() {
            glfwDestroyWindow(window);
            glfwTerminate();
        }

        void run() {
            Pathtracer::Benchmark binfo = config.GetBenchmarkInfo();
            if (binfo.btype == NONE || binfo.btype == SPP) sppBenchmark();
            else imgrefBenchmark();
        }

    private:
        static std::string ToMarkdownPath(std::string path) {
            for (char& ch : path)
                if (ch == '\\') ch = '/';
            return path;
        }

        static std::string GetBenchmarkTypeName(Pathtracer::BenchmarkType type) {
            switch (type) {
            case Pathtracer::NONE: return "NONE";
            case Pathtracer::SPP: return "SPP";
            case Pathtracer::IMGREF: return "IMGREF";
            default: return "UNKNOWN";
            }
        }

        static std::string GetAccelerationStructureName(Pathtracer::AccelerationStructureType type) {
            switch (type) {
            case Pathtracer::BVH: return "Binned SAH-BVH";
            case Pathtracer::BVH4: return "Binned SAH-BVH4";
            case Pathtracer::KD_TREE: return "Havran SAH-KdTree";
            default: return "Unknown";
            }
        }

        static std::string GetComputeBackendName(Pathtracer::ComputeBackendType type) {
            switch (type) {
            case Pathtracer::SPIRV_T: return "SPIR-V";
            case Pathtracer::CUDA_T: return "CUDA";
            default: return "Unknown";
            }
        }

        static std::string GetCurrentDateTime() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t timeNow = std::chrono::system_clock::to_time_t(now);

            std::tm localTime{};
            localtime_s(&localTime, &timeNow);

            std::ostringstream out;
            out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
            return out.str();
        }

        static std::string GetCPUName() {
            const char* cpu = std::getenv("PROCESSOR_IDENTIFIER");
            if (cpu && *cpu) return std::string(cpu);

            const char* arch = std::getenv("PROCESSOR_ARCHITECTURE");
            return (arch && *arch) ? std::string("Windows CPU (") + arch + ")" : "Unknown CPU";
        }

        static std::string CsvEscape(const std::string& value) {
            std::string escaped;
            escaped.reserve(value.size());
            for (char ch : value) {
                if (ch == '"') escaped += "\"\"";
                else escaped += ch;
            }
            return "\"" + escaped + "\"";
        }

        void SaveMarkdownBenchmark(const Pathtracer::Statistics& stats, const Pathtracer::Benchmark& binfo) const {
            std::ofstream md(RESOURCE("outputs\\benchmarks\\benchmark-" + std::format("{:%Y%m%d%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())) + ".md"), std::ios::trunc);
            if (!md.is_open()) return;

            const glm::uvec2 resolution = this->config.GetResolution();
            const glm::uvec2 tileSize = this->config.GetTileSize();

            md << "# Pathtracer Benchmark\n\n";

            md << "## Configuration\n\n";
            md << "- **Date/Time:** " << GetCurrentDateTime() << "\n";
            md << "- **CPU:** " << GetCPUName() << "\n";
            md << "- **GPU:** " << this->vulkanRenderer->GetGPUName() << "\n";
            md << "- **Benchmark Type:** " << GetBenchmarkTypeName(binfo.btype) << "\n";
            md << "- **Scene:** " << this->config.GetScene() << "\n";
            md << "- **Compute Backend:** " << GetComputeBackendName(this->config.GetComputeBackendType()) << "\n";
            md << "- **Acceleration Structure:** " << GetAccelerationStructureName(this->config.GetAccelerationStructureType()) << "\n";
            md << "- **Resolution:** " << resolution.x << "x" << resolution.y << "\n";
            md << "- **Tile Size:** " << tileSize.x << "x" << tileSize.y << "\n";
            md << "- **SPP:** " << binfo.spp << "\n";
            md << "- **Max Light Bounces:** " << this->config.GetLightBounces() << "\n";

            md << "\n## Statistics\n\n";
            md << "- **Triangles:** " << stats.sceneTriangles << "\n";
            md << "- **Rays:** " << stats.treeStats.rays << "\n";
            md << "- **Primary Rays:** " << stats.treeStats.primaryRays << "\n";
            md << "- **Secondary Rays:** " << stats.treeStats.secondaryRays << "\n";
            md << "- **Shadow Rays:** " << stats.treeStats.shadowRays << "\n";
            md << "- **Traversals:** " << stats.treeStats.traversals << "\n";
            md << "- **Intersections:** " << stats.treeStats.isecs << "\n";
            md << "- **Rays/s:** " << (double)stats.treeStats.rays / stats.elapsedTotalTime << "\n";
            md << "- **Nodes/Ray:** " << (double)stats.treeStats.traversals / stats.treeStats.rays << "\n";
            md << "- **Intersections/Ray:** " << (double)stats.treeStats.isecs / stats.treeStats.rays << "\n";
            md << "- **Total Elapsed Time (s):** " << stats.elapsedTotalTime << "\n";
            md << "- **FPS (spp/s):** " << stats.fps << "\n";
            md << "- **Avg. Kernel Time (ms):** " << stats.avgKernelTime << "\n";
            md << "- **Acc. Structure Build Time (s):** " << stats.accStructBuildTime << "\n";
            md << "- **Acc. Structure Memory (bytes):** " << stats.accStructMemoryUsage << "\n";
            md << "- **Acc. Structure Height:** " << stats.accStructHeight << "\n";

            if (binfo.btype == Pathtracer::IMGREF) {
                md << "- **RMSE:** " << stats.rmse << "\n";
                md << "- **PSNR:** " << stats.psnr << "\n";
            }

            md << "\n## Output Files\n\n";
            if (this->config.ShouldSaveImage()) {
                const std::string outputBase = (binfo.btype != Pathtracer::IMGREF)
                    ? "output"
                    : this->config.GetScene() + "\\output" + this->config.InlineString();

                const std::string outputExr = ToMarkdownPath(outputBase + ".exr");
                const std::string outputPpm = ToMarkdownPath(outputBase + ".ppm");

                md << "- **Output EXR:** [" << outputExr << "](" << outputExr << ")\n";
                md << "- **Output PPM:** [" << outputPpm << "](" << outputPpm << ")\n";
                md << "- **Output PNG:** ![output.png](output.png)\n";
            }
            else md << "- Output images were not saved for this run.\n";

            if (binfo.btype == Pathtracer::IMGREF) {
                const std::string refExr = ToMarkdownPath(this->config.GetScene() + "\\ref.exr");
                md << "- **Reference EXR:** [" << refExr << "](" << refExr << ")\n";
            }
        }

        void SaveCsvBenchmark(const Pathtracer::Statistics& stats, const Pathtracer::Benchmark& binfo) const {
            std::ofstream csv(RESOURCE("outputs\\benchmark.csv"), std::ios::app);
            if (!csv.is_open()) return;
            csv.seekp(0, std::ios::end);
            if (csv.tellp() == 0)
                csv << "DateTime,CPU,GPU,BenchmarkType,Scene,ComputeBackend,AccelerationStructure,Resolution,TileSize,SPP,LightBounces,"
                      "Triangles,Rays,PrimaryRays,SecondaryRays,ShadowRays,Traversals,Intersections,RaysPerSecond,NodesPerRay,IntersectionsPerRay,TotalElapsedSeconds,FPS,"
                       "AvgKernelMs,AccStructBuildSeconds,AccStructMemoryBytes,AuxBytes,AccStructHeight,RMSE,PSNR,QARmseThreshold,QAResult\n";

            const glm::uvec2 resolution = this->config.GetResolution();
            const glm::uvec2 tileSize = this->config.GetTileSize();
            const double raysPerSecond = stats.elapsedTotalTime > 0.0f
                ? static_cast<double>(stats.treeStats.rays) / stats.elapsedTotalTime
                : 0.0;
            const double nodesPerRay = stats.treeStats.rays > 0
                ? static_cast<double>(stats.treeStats.traversals) / stats.treeStats.rays
                : 0.0;
            const double intersectionsPerRay = stats.treeStats.rays > 0
                ? static_cast<double>(stats.treeStats.isecs) / stats.treeStats.rays
                : 0.0;

            const bool isImgRef = binfo.btype == Pathtracer::IMGREF;
            const bool qaApplicable = isImgRef && !binfo.useSPPLadder && binfo.rmseThreshold >= 0.0f;
            const std::string rmseText = isImgRef ? std::to_string(stats.rmse) : "";
            const std::string psnrText = isImgRef ? std::to_string(stats.psnr) : "";
            const std::string qaThresholdText = qaApplicable ? std::to_string(binfo.rmseThreshold) : "";
            const std::string qaResultText = qaApplicable ? (stats.rmse <= binfo.rmseThreshold ? "PASS" : "FAIL") : "";

            csv << CsvEscape(GetCurrentDateTime()) << ','
                << CsvEscape(GetCPUName()) << ','
                << CsvEscape(this->vulkanRenderer->GetGPUName()) << ','
                << CsvEscape(GetBenchmarkTypeName(binfo.btype)) << ','
                << CsvEscape(this->config.GetScene()) << ','
                << CsvEscape(GetComputeBackendName(this->config.GetComputeBackendType())) << ','
                << CsvEscape(GetAccelerationStructureName(this->config.GetAccelerationStructureType())) << ','
                << CsvEscape(std::to_string(resolution.x) + "x" + std::to_string(resolution.y)) << ','
                << CsvEscape(std::to_string(tileSize.x) + "x" + std::to_string(tileSize.y)) << ','
                << binfo.spp << ','
                << this->config.GetLightBounces() << ','
                << stats.sceneTriangles << ','
                << stats.treeStats.rays << ','
                << stats.treeStats.primaryRays << ','
                << stats.treeStats.secondaryRays << ','
                << stats.treeStats.shadowRays << ','
                << stats.treeStats.traversals << ','
                << stats.treeStats.isecs << ','
                << raysPerSecond << ','
                << nodesPerRay << ','
                << intersectionsPerRay << ','
                << stats.elapsedTotalTime << ','
                << stats.fps << ','
                << stats.avgKernelTime << ','
                << stats.accStructBuildTime << ','
                << stats.accStructMemoryUsage << ','
                << stats.accStructAuxBytes << ','
                << stats.accStructHeight << ','
                << rmseText << ','
                << psnrText << ','
                << qaThresholdText << ','
                << (qaResultText.empty() ? "" : CsvEscape(qaResultText)) << '\n';
        }

        void initWindow() {
            glfwInit();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            this->window = glfwCreateWindow(config.GetResolution().x, config.GetResolution().y, "PathtracerBenchmark", nullptr, nullptr);
        }

        void sppBenchmark() {
            vulkanRenderer.emplace(config);
            vulkanRenderer->init(window);

            uint32_t currentFrame = 0;
            uint32_t frameCounter = 0;
            Pathtracer::Benchmark binfo = config.GetBenchmarkInfo();
            const uint32_t sppStride = Pathtracer::pixel_sample_stride ? Pathtracer::pixel_sample_stride : 1u;
            auto t0 = std::chrono::high_resolution_clock::now();
            while (!glfwWindowShouldClose(window) && (!binfo.btype || (frameCounter / sppStride) < binfo.spp)) {
                vulkanRenderer->run(currentFrame);
                glfwPollEvents();
                frameCounter++;
            }
            vulkanRenderer->wait();
            auto t1 = std::chrono::high_resolution_clock::now();

            uint32_t currentSPP = frameCounter / sppStride;
            if (currentSPP == 0)
                currentSPP = 1;
            Pathtracer::Statistics stats = vulkanRenderer->GetStatistics();
            long long totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            stats.elapsedTotalTime = totalTime / 1000.0f;
            stats.fps = currentSPP / stats.elapsedTotalTime;
            stats.avgKernelTime /= currentSPP;

            if (!this->config.ShouldSaveStatistics()) {
                if (binfo.btype == IMGREF && !binfo.useSPPLadder)
                    this->SaveStatistics(stats, binfo);
                return;
            }
            std::streambuf* stdoutbuf = std::cout.rdbuf();
            std::ofstream filebuf(RESOURCE("outputs\\stats.txt"), std::ios::app);
            std::cout.rdbuf(filebuf.rdbuf());

            this->SaveStatistics(stats, binfo);
            std::cout.rdbuf(stdoutbuf);
            this->SaveStatistics(stats, binfo);
            this->SaveMarkdownBenchmark(stats, binfo);
            this->SaveCsvBenchmark(stats, binfo);
        }

        void imgrefBenchmark() {
            Pathtracer::Benchmark binfo = config.GetBenchmarkInfo();

            if (!binfo.useSPPLadder) {
                if (binfo.spp == 0) config.SetSPP(1);
                sppBenchmark();
                return;
            }

            //const uint32_t sppLadder[] = { 1, 4, 16, 64, 256, 1024, 4096 };
            uint32_t spp = 1;
            for (int i = 0; i < 7; i++) {
                config.SetSPP(spp);
                spp <<= 2;
                sppBenchmark();
            }
        }

        void SaveStatistics(const Pathtracer::Statistics& stats, const Pathtracer::Benchmark& binfo) const {
            this->config.Print(this->GetCPUName(), this->vulkanRenderer->GetGPUName(), this->GetCurrentDateTime());

            std::cout << "[STATS]\n TRIANGLES: " << stats.sceneTriangles
                << "\n RAYS: " << stats.treeStats.rays
                << "\n PRIMARY RAYS: " << stats.treeStats.primaryRays
                << "\n SECONDARY RAYS: " << stats.treeStats.secondaryRays
                << "\n SHADOW RAYS: " << stats.treeStats.shadowRays
                << "\n TRAVERSALS: " << stats.treeStats.traversals
                << "\n ISECS: " << stats.treeStats.isecs << "\n"
                << "\n RAYS/s: " << (double)stats.treeStats.rays / stats.elapsedTotalTime
                << "\n NODES/RAY: " << (double)stats.treeStats.traversals / stats.treeStats.rays
                << "\n ISECS/RAY: " << (double)stats.treeStats.isecs / stats.treeStats.rays << "\n";

            std::cout << "\n Total Elapsed Time: " << stats.elapsedTotalTime << " s\n"
                << "\n FPS: " << (stats.fps) << " spp/s\n"
                << "\n Avg. Kernel Time: " << stats.avgKernelTime << " ms\n"
                << "\n Acc. Structure Build Time: " << (stats.accStructBuildTime < 1e-3 ? "< 1 ms" : std::to_string(stats.accStructBuildTime)) << " s\n"
                << "\n Acc. Structure Memory: " << stats.accStructMemoryUsage << " bytes (Nodes only)\n\n";

            if (binfo.btype == IMGREF) {
                std::cout << "\n RMSE: " << stats.rmse
                          << "\n PSNR: " << stats.psnr << "\n\n";

                if (!binfo.useSPPLadder && binfo.rmseThreshold >= 0.0f) {
                    const bool qaPass = stats.rmse <= binfo.rmseThreshold;
                    std::cout << " QA RMSE Threshold: " << binfo.rmseThreshold
                              << "\n QA Result: " << (qaPass ? "PASS" : "FAIL") << "\n\n";
                }
            }
        }
    };
}

#endif