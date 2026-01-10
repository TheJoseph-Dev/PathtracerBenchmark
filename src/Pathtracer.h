#include "Vulkan.h"

namespace Pathtracer {

    class App {

        GLFWwindow* window;
        Config config;
        std::optional<Vulkan> vulkan;

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
        void initWindow() {
            glfwInit();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            this->window = glfwCreateWindow(config.GetResolution().x, config.GetResolution().y, "PathtracerBenchmark", nullptr, nullptr);
        }

        void sppBenchmark() {
            vulkan.emplace(config);
            vulkan->init(window);

            uint32_t currentFrame = 0, currentSPP = 0;
            Pathtracer::Benchmark binfo = config.GetBenchmarkInfo();
            auto t0 = std::chrono::high_resolution_clock::now();
            while (!glfwWindowShouldClose(window) && (!binfo.btype || currentSPP < binfo.spp)) {
                currentSPP++;
                vulkan->run(currentFrame);
                glfwPollEvents();
            }
            vulkan->wait();
            auto t1 = std::chrono::high_resolution_clock::now();

            Pathtracer::Statistics stats = vulkan->GetStatistics();
            long long totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            stats.elapsedTotalTime = totalTime / 1000.0f;
            stats.fps = currentSPP / stats.elapsedTotalTime;
            stats.avgKernelTime /= currentSPP;

            std::streambuf* stdoutbuf = std::cout.rdbuf();
            std::ofstream filebuf(RESOURCE("outputs\\stats.txt"), std::ios::app);
            std::cout.rdbuf(filebuf.rdbuf());
            this->SaveStatistics(stats, binfo);
            std::cout.rdbuf(stdoutbuf);
            this->SaveStatistics(stats, binfo);
        }

        void imgrefBenchmark() {
            //const uint32_t sppLadder[] = { 1, 4, 16, 64, 256, 1024, 4096, 16384 };
            uint32_t spp = 1;
            for (int i = 0; i < 8; i++) {
                config.SetSPP(spp);
                spp <<= 2;
                sppBenchmark();
            }
        }

        void SaveStatistics(const Pathtracer::Statistics& stats, const Pathtracer::Benchmark& binfo) const {
            this->config.Print();

            std::cout << "[STATS]\n TRIANGLES: " << stats.sceneTriangles
                << "\n RAYS: " << stats.treeStats.rays
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

            if (binfo.btype == IMGREF)
                std::cout << "\n RMSE: " << stats.rmse
                          << "\n PSNR: " << stats.psnr << "\n\n";
        }
    };
}

/*
[Configuration]
CPU:  Intel Core i5 (11th Gen, 2.40 GHz)
GPU:  NVIDIA GeForce MX350 (2 GB VRAM)
API:  Vulkan
Scene: Cornell Box
Resolution: 720 × 480
Samples per Pixel: 1024
Max Path Length: 6
Acceleration Structure: Binned SAH BVH

[Performance Statistics]
Total Rays Traced:        1.99 x 10^9
Rays per Second:          3.25 x 10^7
Average Nodes per Ray:    5.29
Average Intersections per Ray: 16.30
Total Render Time:        61.10 s

[Acceleration Structure]
Build Time:               < 1 ms
Memory Usage:             5.6 KB
*/