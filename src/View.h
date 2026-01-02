#include "Vulkan.h"

namespace Pathtracer {

    class View {

        GLFWwindow* window;
        Config pathtracerConfig;
        Vulkan vulkan;

    public:
        View(const Pathtracer::Config& pathtracerConfig): pathtracerConfig(pathtracerConfig), vulkan(Vulkan(pathtracerConfig)) {
            initWindow();
            vulkan.init(window);
        }

        ~View() {
            vulkan.shutdown();
        }

        void run() {
            mainLoop();
        }

    private:
        void initWindow() {
            glfwInit();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            this->window = glfwCreateWindow(pathtracerConfig.GetResolution().x, pathtracerConfig.GetResolution().y, "PathtracerBenchmark", nullptr, nullptr);
        }

        void mainLoop() {
            Pathtracer::Benchmark binfo = pathtracerConfig.GetBenchmarkInfo();
            uint32_t currentFrame = 0;
            while (!glfwWindowShouldClose(window) && (!binfo.btype || binfo.spp--)) {
                vulkan.run(currentFrame);
                glfwPollEvents();
            }
            Pathtracer::Statistics stats = vulkan.GetStatistics();
        }
    };
}