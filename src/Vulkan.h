#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <set>
#include <fstream>
#include <direct.h>
#include <optional>
#include <chrono>
#include <glm/glm.hpp>
#include "models/OBJLoader.h"
#include "models/BVH.h"

namespace Pathtracer {
    //constexpr uint32_t WIDTH = 720;//1440; //640; //1080;
    //constexpr uint32_t HEIGHT = 480;//810; //480; //720;

    //constexpr uint32_t TILE_X = 64;
    //constexpr uint32_t TILE_Y = 64;

    struct PushConstants {
        struct ComputeTile {
            glm::uvec2 tileSize;
            glm::uvec2 tileOffset;
        };

        ComputeTile ct;
        uint32_t lightBounces;
    };

    
    /*
        NONE: Pathtracer runs indefinitely
        SPP: Pathtracer runs for a fixed number of frames/spp (samples per pixel). This yields runtime and tree build and traversal statistics
        IMGREF: Pathtracer runs with ladder of spp (1,4,16,64,256,...) for 2 images using different techniques and store the RMSE or relRMSE
    */
    enum BenchmarkType {
        NONE = 0,
        SPP = 1, /*Samples Per Pixel*/
        IMGREF = 2 /*Image Reference*/
    };


    struct Benchmark {
        BenchmarkType btype;
        uint32_t spp;
    };

    enum AccelerationStructure {
        /*Binned SAH*/BVH = 0,
        /*Havran SAH*/KD_TREE = 1
    };

    enum API {
        VULKAN = 0, /*SPIR-V*/
        CUDA = 1
    };

    enum Scene {
        CORNELL_BOX = 0,
        SIBENIK = 1,
        /*Stanford*/BUNNY = 2,
        DRAGON = 3
    };

    enum Resolution {
        R480x320 = 0,
        R720x480 = 1,
        R1080x720 = 2,
        R1024x1024 = 3,
        R1440x810 = 4,
        R1920x1080 = 5
    };

    class Config {
        const AccelerationStructure accelerationStructure;
        const API api;
        const Scene scene;
        const Resolution resolution;
        const uint32_t lightBounces;
        const Benchmark benchmarkInfo;
        const glm::uvec2 tileSize;
        const bool saveOutputImage;
    
    public:

        Config(AccelerationStructure as, API api, Scene scene, Resolution resolution, uint32_t lightBounces, Benchmark benchmarkInfo, glm::uvec2 tileSize = glm::uvec2(8, 8), bool saveOutputImage = false)
            : accelerationStructure(as), api(api), scene(scene), resolution(resolution), lightBounces(lightBounces), benchmarkInfo(benchmarkInfo), saveOutputImage(saveOutputImage), tileSize(tileSize)
        {}

        AccelerationStructure GetAccelerationStructure() const {
            return accelerationStructure;
        }

        API GetAPI() const {
            return api;
        }

        /*
        Scene GetScene() const {
            return scene;
        }
        */

        std::string GetScene() const {
            switch (this->scene) {
                case Scene::CORNELL_BOX: return "cornell_box";
                case Scene::SIBENIK: return "sibenik2";
                case Scene::BUNNY: return "bunny-plane";
                case Scene::DRAGON: return "dragon-plane";
                default: throw std::runtime_error("Unknown resolution");
            }
        }

        glm::uvec2 GetResolution() const {
            switch (this->resolution) {
                case Resolution::R480x320: return glm::uvec2(480, 320);
                case Resolution::R720x480: return glm::uvec2(720, 480);
                case Resolution::R1080x720: return glm::uvec2(1080, 720);
                case Resolution::R1024x1024: return glm::uvec2(1024, 1024);
                case Resolution::R1440x810: return glm::uvec2(1440, 810);
                case Resolution::R1920x1080: return glm::uvec2(1920, 1080);
                default: throw std::runtime_error("Unknown resolution");
            }
        }

        uint32_t GetLightBounces() const {
            return this->lightBounces;
        }

        Benchmark GetBenchmarkInfo() const {
            return this->benchmarkInfo;
        }

        glm::uvec2 GetTileSize() const {
            return this->tileSize;
        }

        bool ShouldSaveImage() const {
            return this->saveOutputImage;
        }


        void Print() const {
            std::cout << "[Config]"
                << "\n CPU: 11th Gen Intel Core i5 - 2.40GHz"
                << "\n GPU: NVIDIA GeForce MX350 - 2Gb VRAM"
                << "\n Scene: " << this->GetScene()
                << "\n API: " << (!this->api ? "Vulkan" : "CUDA")
                << "\n Acc. Structure: " << (!this->accelerationStructure ? "Binned SAH-BVH" : "Havran SAH-KdTree")
                << "\n Resolution: " << this->GetResolution().x << "x" << this->GetResolution().y
                << "\n SPP: " << this->benchmarkInfo.spp
                << "\n Max Light Bounces: " << this->lightBounces << "\n";
        }
    };

    struct GPUTreeStatistics {
        struct uint64gpu_t { uint32_t lo, hi; };
        uint64gpu_t rays;
        uint64gpu_t isecs;
        uint64gpu_t traversals;
    };

    struct TreeStatistics {
        uint64_t rays;
        uint64_t isecs;
        uint64_t traversals;
    };
    
    struct Statistics {
        TreeStatistics treeStats;
        float elapsedTotalTime = 0.0f;
        float avgFrameTime = 0.0f;

        float accStructBuildTime = 0.0f;
        uint32_t accStructMemoryUsage = 0;

        float rmse = 0.0f;
        float psnr = 0.0f;
    };
}

std::string RESOURCE_PATH_PREFIX = std::string("..\\..\\..\\src\\resources\\");
#define RESOURCE(filepath) "..\\..\\..\\src\\resources\\" filepath

#define DEBUG
#ifdef DEBUG
constexpr bool enableValidationLayers = true;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
#else
constexpr bool enableValidationLayers = false;
#endif

struct Vertex {
    glm::vec2 pos;
    glm::vec2 uv;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static void getAttributeDescriptions(VkVertexInputAttributeDescription attributeDescriptions[2]) {
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, uv);
    }
};

// RAII Implementation
struct Buffer {
    VkDevice device{ VK_NULL_HANDLE };
    VkBuffer buffer{ VK_NULL_HANDLE };
    VkDeviceMemory memory{ VK_NULL_HANDLE };
    VkDeviceSize size{ 0 };

    ~Buffer() {
        if (buffer)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }

    Buffer() {};

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept {
        *this = std::move(other);
    }

    Buffer& operator=(Buffer&& other) noexcept {
        device = other.device;
        buffer = other.buffer;
        memory = other.memory;
        size = other.size;
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        return *this;
    }
};

class Vulkan {

    // How many frames you let the CPU start rendering before the GPU is done
    // MAX_FRAMES_IN_FLIGHT: How many frames the CPU can work on at once
    // Swap Chain: How many images are available to render into
    // They are not directly tied, but you need to consider things like: MAX_FRAMES_IN_FLIGHT = min(MAX_FRAMES_IN_FLIGHT, SC.size())
    static constexpr uint32_t SWAPCHAIN_IMAGE_COUNT = 2;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2; //SWAPCHAIN_IMAGE_COUNT - 1;
    static constexpr uint32_t PING_PONG_FRAMES = 2;
    static constexpr uint32_t nDescriptorSets = 2; // Frag and Compute
    static constexpr uint32_t PATHTRACER_IMG_COUNT = PING_PONG_FRAMES + 1; // + 1 comes from acc out image

    VkInstance instance; // Connection between Vulkan and the main program
    VkPhysicalDevice physicalDevice;
    VkDevice device; // After selecting a Physical Device, create a logical device to interface with it
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
        //VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME - Not available
    };

    uint32_t graphicsQueueFamilyIndex = 0, presentQueueFamilyIndex = 0, computeQueueFamilyIndex = 0;
    VkQueue graphicsQueue; // Handle to interact with device graphics queue;
    VkSurfaceKHR surface; // Handle to interact with window
    VkQueue presentQueue; // Handle to interact with window surface queue;

    VkSurfaceFormatKHR surfaceFormat;
    VkExtent2D extent;
    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkImageLayout swapchainImageLayouts[SWAPCHAIN_IMAGE_COUNT];

    VkViewport viewport;
    VkRect2D scissor; // Cut viewport filter >:/

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSetLayout computeDescriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet fragDescriptorSet[PING_PONG_FRAMES];
    VkDescriptorSet computeDescriptorSet[PING_PONG_FRAMES];

    VkImage pathtracerImages[PATHTRACER_IMG_COUNT];
    VkDeviceMemory pathtracerImagesMemory[PATHTRACER_IMG_COUNT];
    VkImageView pathtracerImageViews[PATHTRACER_IMG_COUNT];
    VkImageLayout pathtracerImageLayouts[PATHTRACER_IMG_COUNT];
    VkSampler pathtracerImageSampler;
    VkSampler fragImageSampler;
    int textureIndex = 0;

    Buffer vertexBuffer;
    VkPipelineLayout pipelineLayout;
    VkPipelineLayout computePipelineLayout;
    VkPipeline graphicsPipeline;
    VkPipeline computePipeline;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];

    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT]; // Ensures the submit waits until the acquired image is ready (from vkAcquireNextImageKHR)
    VkSemaphore renderFinishedSemaphores[SWAPCHAIN_IMAGE_COUNT]; // Ensures the present (vkQueuePresentKHR) waits until rendering to that image is done.

    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
    std::vector<VkFence> imagesInFlight;

    Pathtracer::Config pathtracerConfig;
    Pathtracer::Statistics pathtracerStatistics;
    const uint32_t WIDTH, HEIGHT;
public:
    Vulkan(const Pathtracer::Config& pathtracerConfig) :
        pathtracerConfig(pathtracerConfig), WIDTH(pathtracerConfig.GetResolution().x), HEIGHT(pathtracerConfig.GetResolution().y)
    {
    }

    void init(GLFWwindow* window) {
        createInstance();
        createWindowSurface(window);
        createDevice();
        createSwapchain(window);
        createGraphicsPipeline();
    }

private:
    void createInstance() {
        std::cout << "\n Vulkan Header Version: " << VK_HEADER_VERSION << std::endl;
        std::cout << " Vulkan API Version: " << VK_API_VERSION_VARIANT(VK_HEADER_VERSION_COMPLETE)
            << "." << VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE)
            << "." << VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE)
            << std::endl;

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "PathtracerBenchmark";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;

        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        instanceInfo.enabledExtensionCount = glfwExtensionCount;
        instanceInfo.ppEnabledExtensionNames = glfwExtensions;
        instanceInfo.enabledLayerCount = 0;

#ifdef DEBUG
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        bool layersAvailable = true;
        for (const char* layerName : validationLayers) {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers)
                if (strcmp(layerName, layerProperties.layerName) == 0) { layerFound = true; break; }
            if (!layerFound) std::cerr << "Requested validation layer: " << layerName << " is not availible\n";
            layersAvailable &= layerFound;
        }

        if (layersAvailable) {
            instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            instanceInfo.ppEnabledLayerNames = validationLayers.data();
        }
#endif

        if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
            std::cerr << "VkInstance Creation Error\n";
            return;
        }

#ifdef DEBUG
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageTypes,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData) -> VkBool32 {
                std::cerr << "[VULKAN DEBUG] " << pCallbackData->pMessage << std::endl;
                return VK_FALSE;
            };
        debugCreateInfo.pUserData = nullptr;

        // Load the extension function
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            VkDebugUtilsMessengerEXT debugMessenger;
            if (func(instance, &debugCreateInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
                std::cerr << "Failed to create Vulkan debug messenger!" << std::endl;
            }
        }
#endif
    }

    void createWindowSurface(GLFWwindow* window) {
        if (glfwCreateWindowSurface(this->instance, window, nullptr, &this->surface) != VK_SUCCESS) {
            std::cerr << "VkSurfaceKHR Creation Error\n";
            return;
        }
    }

    void createDevice() {
        this->physicalDevice = VK_NULL_HANDLE;

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (!deviceCount) { std::cerr << "Failed to find GPUs with Vulkan support\n"; return; }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        physicalDevice = devices[0];

        std::cout << "\n Available Devices:\n";
        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures2 deviceFeatures2;
        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
        dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;

        for (const auto& device : devices) {
            vkGetPhysicalDeviceProperties(device, &deviceProperties);

            deviceFeatures2 = {};
            deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            deviceFeatures2.pNext = &dynamicRenderingFeatures;
            vkGetPhysicalDeviceFeatures2(device, &deviceFeatures2);
            std::cout << "\n - " << deviceProperties.deviceName;

            if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                std::cout << " (Discrete)";
                physicalDevice = device;
            }

            if (dynamicRenderingFeatures.dynamicRendering) {
                std::cout << " (Core Dyn. Rendering)";
            }

            uint32_t extensionCount;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

            std::vector<VkExtensionProperties> availableExtensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

            std::set<std::string> requiredExtensions(this->deviceExtensions.begin(), this->deviceExtensions.end());
            for (const auto& extension : availableExtensions) requiredExtensions.erase(extension.extensionName);

            if (requiredExtensions.empty()) std::cout << " (Extensions Available)";
            else std::cerr << "ERROR: EXTENSIONS REQUIRED NOT AVAILABLE - " << *requiredExtensions.begin();

            uint32_t apiVersion = deviceProperties.apiVersion;
            uint32_t major = VK_VERSION_MAJOR(apiVersion);
            uint32_t minor = VK_VERSION_MINOR(apiVersion);
            uint32_t patch = VK_VERSION_PATCH(apiVersion);

            std::cout << " (" << major << "." << minor << "." << patch << ")";

            std::cout << std::endl;
        }
        std::cout << std::endl;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);


        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
        for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsQueueFamilyIndex = i;
                std::cout << " Queue family " << i << " supports graphics operations\n";
            };
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                computeQueueFamilyIndex = i;
                std::cout << " Queue family " << i << " supports compute operations\n";
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, this->surface, &presentSupport);
            if (presentSupport) {
                presentQueueFamilyIndex = i;
                std::cout << " Queue family " << i << " supports window surface\n";
            }
        }


        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> queueFamilyIndicies = { graphicsQueueFamilyIndex, presentQueueFamilyIndex, computeQueueFamilyIndex };
        for (uint32_t queueFamilyIndex : queueFamilyIndicies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
            queueCreateInfo.queueCount = 1;
            float queuePriority = 1.0f;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.features = {};
        deviceFeatures2.pNext = &dynamicRenderingFeatures;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = &dynamicRenderingFeatures;
        deviceInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceInfo.queueCreateInfoCount = queueCreateInfos.size();

        deviceInfo.pEnabledFeatures = nullptr;
        deviceInfo.pNext = &deviceFeatures2;
        deviceInfo.enabledExtensionCount = this->deviceExtensions.size();
        deviceInfo.ppEnabledExtensionNames = this->deviceExtensions.data();

        if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &this->device) != VK_SUCCESS) {
            std::cerr << "VkDevice Creation Error\n";
            return;
        }
    }

    void createSwapchain(GLFWwindow* window) {
        vkGetDeviceQueue(this->device, graphicsQueueFamilyIndex, 0, &this->graphicsQueue); // 0 because we created only 1 queue of this family
        vkGetDeviceQueue(this->device, presentQueueFamilyIndex, 0, &this->presentQueue);

        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, this->surface, &capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, this->surface, &formatCount, nullptr);

        if (formatCount) {
            formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, this->surface, &formatCount, formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->surface, &presentModeCount, nullptr);

        if (presentModeCount) {
            presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->surface, &presentModeCount, presentModes.data());
        }

        if (!formatCount || !presentModeCount) {
            std::cerr << "Chosen physical device swap chain doesn't support current window surface or is not adequate\n";
            return;
        }

        VkPresentModeKHR presentMode = presentModes[0];
        this->surfaceFormat = formats[0];
        this->extent = capabilities.currentExtent;

        for (const auto& sFormat : formats)
            if (sFormat.format == VK_FORMAT_B8G8R8A8_SRGB && sFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                this->surfaceFormat = sFormat;
                break;
            }

        for (const auto& pMode : presentModes)
            if (pMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = pMode;
                break;
            }

        if (capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max()) {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            this->extent = actualExtent;
        }

        uint32_t imageCount = SWAPCHAIN_IMAGE_COUNT; // How many images to have in the swap chain
        //(capabilities.maxImageCount > 0 && capabilities.minImageCount + 1 > capabilities.maxImageCount) ?
        //capabilities.maxImageCount : capabilities.minImageCount + 1; 

        VkSwapchainCreateInfoKHR swapChainInfo{};
        swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainInfo.surface = this->surface;
        swapChainInfo.minImageCount = imageCount;
        swapChainInfo.imageFormat = this->surfaceFormat.format;
        swapChainInfo.imageColorSpace = this->surfaceFormat.colorSpace;
        swapChainInfo.imageExtent = this->extent;
        swapChainInfo.imageArrayLayers = 1;
        swapChainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainInfo.preTransform = capabilities.currentTransform;
        swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainInfo.presentMode = presentMode;
        swapChainInfo.clipped = VK_TRUE;
        swapChainInfo.oldSwapchain = VK_NULL_HANDLE;

        if (graphicsQueueFamilyIndex != presentQueueFamilyIndex) {
            swapChainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            uint32_t queueFamilyIndices[] = { graphicsQueueFamilyIndex, presentQueueFamilyIndex };
            swapChainInfo.queueFamilyIndexCount = sizeof(queueFamilyIndices) / sizeof(uint32_t);
            swapChainInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapChainInfo.queueFamilyIndexCount = 0;
            swapChainInfo.pQueueFamilyIndices = nullptr;
        }

        if (vkCreateSwapchainKHR(this->device, &swapChainInfo, nullptr, &this->swapChain) != VK_SUCCESS) {
            std::cerr << "VkSwapchainKHR Creation Error\n";
            return;
        }

        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            VkImageViewCreateInfo imageViewInfo{};
            imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewInfo.image = swapChainImages[i];
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewInfo.format = swapChainInfo.imageFormat;
            imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewInfo.subresourceRange.baseMipLevel = 0;
            imageViewInfo.subresourceRange.levelCount = 1;
            imageViewInfo.subresourceRange.baseArrayLayer = 0;
            imageViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(this->device, &imageViewInfo, nullptr, &this->swapChainImageViews[i]) != VK_SUCCESS) {
                std::cerr << "VkImageView Creation Error\n";
                return;
            }

            this->swapchainImageLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    enum ShaderType {
        VERTEX,
        FRAGMENT,
        COMPUTE
    };

    std::optional<VkShaderModule> createShaderModule(const std::string& path, ShaderType type) {
        std::ifstream shaderFile(RESOURCE_PATH_PREFIX + path, std::ios::ate | std::ios::binary);
        if (!shaderFile.is_open()) { std::cerr << "Failed to read shader file\n"; return std::nullopt; }

        size_t shaderFileSize = static_cast<size_t>(shaderFile.tellg());
        std::vector<char> shaderBuffer(shaderFileSize);

        shaderFile.seekg(0);
        shaderFile.read(shaderBuffer.data(), shaderFileSize);
        shaderFile.close();

        VkShaderModule shaderModule;
        VkShaderModuleCreateInfo shaderModuleInfo{};
        shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderModuleInfo.codeSize = shaderBuffer.size();
        shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(shaderBuffer.data());
        if (vkCreateShaderModule(this->device, &shaderModuleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            std::cerr << "Failed to create VkShaderModule\n";
            return std::nullopt;
        }

        return shaderModule;
    }

    VkCommandBuffer beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = this->commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(this->device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(this->graphicsQueue);

        vkFreeCommandBuffers(this->device, this->commandPool, 1, &commandBuffer);
    }

    struct CameraUBO {
        glm::vec4 cameraPos;
        glm::vec4 cameraRot;
    };

    struct PathtracerUBO {
        glm::vec2 iResolution;
        float iTime;
        int iFrame;
        //int accumulate;
        //vec3 pad0;
        CameraUBO camera;
    };

    Pathtracer::PushConstants pathtracerPC;

    void createDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings, VkDescriptorSetLayout* dsl) {
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        vkCreateDescriptorSetLayout(this->device, &layoutInfo, nullptr, dsl);
    }

    void createDescriptorPool(const std::vector<VkDescriptorPoolSize>& poolSizes, uint32_t maxSets) {
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = maxSets;

        vkCreateDescriptorPool(this->device, &poolInfo, nullptr, &this->descriptorPool);
    }

    VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout* dsl) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = this->descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = dsl;

        VkDescriptorSet descriptorSet;
        vkAllocateDescriptorSets(this->device, &allocInfo, &descriptorSet);
        return descriptorSet;
    }

    void updateUniformBufferDescriptorSet(VkDescriptorSet descriptorSet, uint32_t binding, VkBuffer buffer, VkDeviceSize size) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = size;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = binding;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(this->device, 1, &descriptorWrite, 0, nullptr);
    }

    void updateStorageImageDescriptorSet(VkDescriptorSet descriptorSet, uint32_t binding, VkImageView imageView, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = imageView;
        imageInfo.imageLayout = layout; // usually GENERAL for compute

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void updateCombinedImageSamplerDescriptorSet(VkDescriptorSet descriptorSet, uint32_t binding, VkSampler sampler, VkImageView imageView, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = sampler;
        imageInfo.imageView = imageView;
        imageInfo.imageLayout = layout;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        vkBindBufferMemory(this->device, buffer, bufferMemory, 0);
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                return i;

        throw std::runtime_error("failed to find suitable memory type!");
    }

    Buffer createUniformBuffer(VkDeviceSize size, const void* initialData) {
        Buffer ubo{};
        ubo.device = this->device;
        ubo.size = size;

        createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ubo.buffer, ubo.memory);

        // Upload initial data
        if (initialData) {
            void* data;
            vkMapMemory(device, ubo.memory, 0, size, 0, &data);
            memcpy(data, initialData, (size_t)size);
            vkUnmapMemory(device, ubo.memory);
        }

        return ubo;
    }

    void updateUniformBuffer(const Buffer& ubo, const void* newData, VkDeviceSize size) {
        void* data;
        vkMapMemory(device, ubo.memory, 0, size, 0, &data);
        memcpy(data, newData, (size_t)size);
        vkUnmapMemory(device, ubo.memory);
    }

    const uint32_t SSBOsCount = 6;
    enum SSBOBinding {
        BVH_NODES = 3,
        TRIANGLES = 4,
        VERTICES = 5,
        EMISSIVES = 6,
        MATERIALS = 7,

        STATISTICS = 9
    };
    std::vector<Buffer> pathtracerSSBOs;
    std::vector<Buffer> stagingBuffers;

    Buffer createStagingBuffer(VkDeviceSize size, const void* initialData) {
        Buffer staging{};
        staging.device = this->device;
        staging.size = size;
        createBuffer(
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging.buffer,
            staging.memory
        );

        // Map & copy initial data
        if (initialData) {
            void* data;
            vkMapMemory(device, staging.memory, 0, size, 0, &data);
            memcpy(data, initialData, size);
            vkUnmapMemory(device, staging.memory);
        }
        else {
            void* data;
            vkMapMemory(device, staging.memory, 0, size, 0, &data);
            memset(data, 0, size);
            vkUnmapMemory(device, staging.memory);
        }

        return staging;
    }

    Buffer createStorageBuffer(VkDeviceSize size, const void* initialData, bool keepStaging = false) {
        Buffer staging = createStagingBuffer(size, initialData);

        // Create device-local GPU buffer
        Buffer gpuBuffer{};
        gpuBuffer.device = this->device;
        gpuBuffer.size = size;
        createBuffer(
            size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            gpuBuffer.buffer,
            gpuBuffer.memory
        );

        // Copy staging => device-local buffer
        copyBuffer(staging.buffer, gpuBuffer.buffer, size);

        if (keepStaging) this->stagingBuffers.push_back(std::move(staging));
        
        return gpuBuffer; // C++ guarantees it stays alive, so ~Buffer not called here
    }

    void createSSBO(const VkBuffer& buffer, uint32_t binding) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        for (int i = 0; i < PING_PONG_FRAMES; i++) {
            write.dstSet = computeDescriptorSet[i];
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }


    void create2DLinearImageSampler() {
        VkSamplerCreateInfo dataSamplerInfo{};
        dataSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        dataSamplerInfo.magFilter = VK_FILTER_LINEAR;
        dataSamplerInfo.minFilter = VK_FILTER_LINEAR;
        //dataSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        dataSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dataSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dataSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dataSamplerInfo.anisotropyEnable = VK_FALSE;
        dataSamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        dataSamplerInfo.unnormalizedCoordinates = VK_FALSE;

        vkCreateSampler(this->device, &dataSamplerInfo, nullptr, &this->fragImageSampler);
    }

    void create2DNearestImageSampler() {
        VkSamplerCreateInfo dataSamplerInfo{};
        dataSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        dataSamplerInfo.magFilter = VK_FILTER_NEAREST;
        dataSamplerInfo.minFilter = VK_FILTER_NEAREST;
        dataSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dataSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dataSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        dataSamplerInfo.anisotropyEnable = VK_FALSE;
        dataSamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        dataSamplerInfo.unnormalizedCoordinates = VK_FALSE;

        vkCreateSampler(this->device, &dataSamplerInfo, nullptr, &this->pathtracerImageSampler);
    }

    void createImage2D(uint32_t width, uint32_t height) {

        // Double Buffer
        for (int i = 0; i < PATHTRACER_IMG_COUNT; i++) {
            this->pathtracerImages[i] = {};
            this->pathtracerImagesMemory[i] = {};
            this->pathtracerImageViews[i] = {};

            VkImageCreateInfo dataImageInfo{};
            dataImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            dataImageInfo.imageType = VK_IMAGE_TYPE_2D;
            dataImageInfo.extent.width = width;
            dataImageInfo.extent.height = height;
            dataImageInfo.extent.depth = 1;
            dataImageInfo.mipLevels = 1;
            dataImageInfo.arrayLayers = 1;
            dataImageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            dataImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            dataImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            dataImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            dataImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            dataImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            vkCreateImage(this->device, &dataImageInfo, nullptr, &this->pathtracerImages[i]);

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(this->device, this->pathtracerImages[i], &memRequirements);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(this->device, &allocInfo, nullptr, &this->pathtracerImagesMemory[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to allocate image memory!");
            }

            vkBindImageMemory(this->device, pathtracerImages[i], pathtracerImagesMemory[i], 0);

            VkImageViewCreateInfo dataTextureInfo{};
            dataTextureInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            dataTextureInfo.image = this->pathtracerImages[i];
            dataTextureInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dataTextureInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            dataTextureInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            dataTextureInfo.subresourceRange.baseMipLevel = 0;
            dataTextureInfo.subresourceRange.levelCount = 1;
            dataTextureInfo.subresourceRange.baseArrayLayer = 0;
            dataTextureInfo.subresourceRange.layerCount = 1;

            vkCreateImageView(this->device, &dataTextureInfo, nullptr, &pathtracerImageViews[i]);

            VkImageLayout imgLayout = i != PATHTRACER_IMG_COUNT-1 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
            transitionImageLayout(this->pathtracerImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, imgLayout);
            pathtracerImageLayouts[i] = imgLayout;
        }

        // Acc image staging buffer
        Buffer stagingAcc{};
        stagingAcc.device = this->device;
        stagingAcc.size = this->WIDTH * this->HEIGHT * sizeof(glm::vec4);
        createBuffer(
            stagingAcc.size, // size in bytes
            VK_BUFFER_USAGE_TRANSFER_DST_BIT, // for vkCmdCopyImageToBuffer
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingAcc.buffer,
            stagingAcc.memory
        );
        this->stagingBuffers.push_back(std::move(stagingAcc));
    }

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer cmdbf = VK_NULL_HANDLE) const {
        VkCommandBuffer commandBuffer = cmdbf ? cmdbf : beginSingleTimeCommands();

        /*
            One of the most common ways to perform layout transitions is using an image memory barrier. A pipeline barrier like that is generally
            used to synchronize access to resources, like ensuring that a write to a buffer completes before reading from it,
            but it can also be used to transition image layouts and transfer queue family ownership when VK_SHARING_MODE_EXCLUSIVE is used.
            There is an equivalent buffer memory barrier to do this for buffers.
        */

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        if(!cmdbf) endSingleTimeCommands(commandBuffer);
    }

    void transitionCompute(VkCommandBuffer commandBuffer, VkImage image, int currentFrame) {
        // Transition SHADER_READ_ONLY_OPTIMAL -> GENERAL for compute
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        // Track old layout per frame
        //printf("=== (COMPUTE) BEFORE BARRIER LAYOUT: %d ===\n", pathtracerImageLayouts[textureIndex]);
        barrier.oldLayout = pathtracerImageLayouts[textureIndex];
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = (barrier.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        //printf("[FRAME %d] Pre-compute barrier: oldLayout=%d newLayout=%d\n", currentFrame, barrier.oldLayout, barrier.newLayout);

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,  // previous use
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,   // upcoming use
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        pathtracerImageLayouts[textureIndex] = VK_IMAGE_LAYOUT_GENERAL;

        // Bind compute pipeline and dispatch
        // Bind compute pipeline and descriptors
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &computeDescriptorSet[currentFrame%PING_PONG_FRAMES], 0, nullptr);

        // Dispatch compute
        // Tiling to avoid TDR
        //vkCmdDispatch(commandBuffer, (this->WIDTH + 7) / 8, (this->HEIGHT + 7) / 8, 1);

        const uint32_t TILE_Y = this->pathtracerConfig.GetTileSize().y;
        const uint32_t TILE_X = this->pathtracerConfig.GetTileSize().x;
        for (uint32_t tileY = 0; tileY < this->HEIGHT; tileY += TILE_Y) {
            for (uint32_t tileX = 0; tileX < this->WIDTH; tileX += TILE_X) {
                this->pathtracerPC.ct.tileOffset = { tileX, tileY };
                vkCmdPushConstants( commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Pathtracer::PushConstants), &this->pathtracerPC );
                vkCmdDispatch( commandBuffer, (TILE_X + 7) / 8, (TILE_Y + 7) / 8, 1 );
            }
        }

        // Transition GENERAL -> SHADER_READ_ONLY_OPTIMAL for graphics
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        //printf("[FRAME %d] Pre-compute barrier: oldLayout=%d newLayout=%d\n", currentFrame, barrier.oldLayout, barrier.newLayout);

        pathtracerImageLayouts[textureIndex] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    PathtracerUBO pathtracerState{};
    Buffer pathtracerUBO;
    void createGraphicsPipeline() {
        // Graphics Pipeline
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;

        if (vkCreateCommandPool(this->device, &cmdPoolInfo, nullptr, &this->commandPool) != VK_SUCCESS) {
            std::cerr << "Failed to create VkCreateCommandPool\n";
            return;
        }

        // Gotta change cmake.txt
        char buffer[FILENAME_MAX];
        _getcwd(buffer, FILENAME_MAX);
        std::cout << "Current working directory: " << buffer << std::endl;

        // Shaders
        system(RESOURCE("shaders\\runtime_compile.bat"));

        VkShaderModule vsShaderModule = this->createShaderModule("shaders\\vert.spv", ShaderType::VERTEX).value();
        VkShaderModule fsShaderModule = this->createShaderModule("shaders\\frag.spv", ShaderType::FRAGMENT).value();

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vsShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fsShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        // Pathtracer Descriptor Set
        pathtracerState.iFrame = 0;
        pathtracerState.iResolution = glm::vec2(this->WIDTH, this->HEIGHT);
        pathtracerState.iTime = 0;
        pathtracerState.camera.cameraPos = glm::vec4(0.0f, 0.1f, -0.4f, 0.0);
        pathtracerState.camera.cameraRot = glm::vec4(0, 0,0,0);
        pathtracerUBO = createUniformBuffer(sizeof(PathtracerUBO), &pathtracerState);

        std::vector<VkDescriptorSetLayoutBinding> computeBindings = {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::BVH_NODES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::TRIANGLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::VERTICES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::EMISSIVES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::MATERIALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { SSBOBinding::STATISTICS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        };
        createDescriptorSetLayout(computeBindings, &this->computeDescriptorSetLayout);


        std::vector<VkDescriptorSetLayoutBinding> fragBindings = {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        };
        createDescriptorSetLayout(fragBindings, &this->descriptorSetLayout);

        this->pathtracerSSBOs.reserve(SSBOsCount);
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * PING_PONG_FRAMES },         // Only for compute UBO
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * PING_PONG_FRAMES }, // 1 lastFrameTex + 2 fragment textures (double buffering)
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * PING_PONG_FRAMES },          // compute output image + acc image framebuffer
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SSBOsCount * PING_PONG_FRAMES } // SSBOs
        };

        createDescriptorPool(poolSizes, nDescriptorSets * PING_PONG_FRAMES);

        createImage2D(this->WIDTH, this->HEIGHT);
        create2DLinearImageSampler();
        create2DNearestImageSampler();

        for (int i = 0; i < PING_PONG_FRAMES; i++) {
            this->computeDescriptorSet[i] = allocateDescriptorSet(&computeDescriptorSetLayout);
            this->fragDescriptorSet[i] = allocateDescriptorSet(&descriptorSetLayout);
        }

        for (int i = 0; i < PING_PONG_FRAMES; i++) {

            // Compute descriptor set
            updateUniformBufferDescriptorSet(computeDescriptorSet[i], 1, pathtracerUBO.buffer, sizeof(PathtracerUBO));

            // Output storage image for compute
            updateStorageImageDescriptorSet(computeDescriptorSet[i], 0, pathtracerImageViews[i], VK_IMAGE_LAYOUT_GENERAL);

            // Acc image
            updateStorageImageDescriptorSet(computeDescriptorSet[i], 8, pathtracerImageViews[PATHTRACER_IMG_COUNT-1], VK_IMAGE_LAYOUT_GENERAL);

            // Input sampler for compute (previous frame texture)
            updateCombinedImageSamplerDescriptorSet(computeDescriptorSet[i], 2, pathtracerImageSampler, pathtracerImageViews[1 - i]);

            // Fragment shader reads current frame image
            updateCombinedImageSamplerDescriptorSet(fragDescriptorSet[i], 0, fragImageSampler, pathtracerImageViews[i]);
        }

        this->pathtracerPC.ct.tileSize = this->pathtracerConfig.GetTileSize();
        this->pathtracerPC.lightBounces = this->pathtracerConfig.GetLightBounces();

        std::string sceneFilepath = RESOURCE("3DModels\\") + this->pathtracerConfig.GetScene();
        OBJLoader objloader((sceneFilepath + ".obj").c_str());
        std::vector<OBJLoader::Triangle> triangles = objloader.GetTriangles();
        std::vector<OBJLoader::Vertex> objVertices = objloader.objVertices;

        // Load light geometry
        OBJLoader lightLoader((sceneFilepath + "-light.obj").c_str());
        std::vector<OBJLoader::Triangle> lightTris = lightLoader.GetTriangles();
        std::vector<OBJLoader::Vertex> lightVerts = lightLoader.objVertices;

        OBJLoader::Material lightMat = { glm::vec4(0.0f), 0.0f, 1.0f, 0.0f, 0.0f, glm::vec3(0.9f, 0.9f, 0.9f), 20.0f };
        std::vector<OBJLoader::Material> materials = objloader.GetMaterials();
        materials.push_back(lightMat);

        // Merge light vertices
        uint32_t vertexOffset = objVertices.size();
        objVertices.insert(objVertices.end(), lightVerts.begin(), lightVerts.end());
        for (auto& lt : lightTris) lt.indices = glm::uvec4(lt.indices.x+vertexOffset, lt.indices.y+vertexOffset, lt.indices.z+vertexOffset, materials.size()-1);

        // Create SSBOs
        std::vector<glm::uvec3> meshTris(triangles.size());
        for (size_t i = 0; i < triangles.size(); i++) meshTris[i] = { triangles[i].indices.x, triangles[i].indices.y, triangles[i].indices.z };
        OBJLoader::MeshGeometry mergedMesh{ objVertices, meshTris };

        BVH bvh = BVH(mergedMesh);
        auto t0 = std::chrono::high_resolution_clock::now();
        bvh.Build();
        auto t1 = std::chrono::high_resolution_clock::now();
        pathtracerStatistics.accStructBuildTime = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() / 1000.0f;

        auto bvht = bvh.GetTriangles();

        // Reordering to match BVH partitions (triIdx; triCount) in the shader
        std::vector<OBJLoader::Triangle> reordered(triangles.size());
        for (size_t i = 0; i < triangles.size(); i++) reordered[i] = triangles[bvht[i].oIdx];
        triangles = std::move(reordered);

        auto tree = bvh.GetTree();
        pathtracerStatistics.accStructMemoryUsage = tree.size() * sizeof(tree[0]);

        pathtracerSSBOs.push_back(createStorageBuffer(tree.size() * sizeof(tree[0]), tree.data()));
        createSSBO(pathtracerSSBOs.back().buffer, SSBOBinding::BVH_NODES); // BVH Nodes

        //bvh.Print();
        pathtracerSSBOs.push_back(createStorageBuffer(triangles.size() * sizeof(triangles[0]), triangles.data()));
        createSSBO(pathtracerSSBOs.back().buffer, SSBOBinding::TRIANGLES); // Triangles

        pathtracerSSBOs.push_back(createStorageBuffer(objVertices.size() * sizeof(objVertices[0]), objVertices.data()));
        createSSBO(pathtracerSSBOs.back().buffer, SSBOBinding::VERTICES); // Vertices

        pathtracerSSBOs.push_back(createStorageBuffer(lightTris.size() * sizeof(lightTris[0]), lightTris.data()));
        createSSBO(pathtracerSSBOs.back().buffer, SSBOBinding::EMISSIVES); // Emissives/Light

        pathtracerSSBOs.push_back(createStorageBuffer(materials.size() * sizeof(materials[0]), materials.data()));
        createSSBO(pathtracerSSBOs.back().buffer, SSBOBinding::MATERIALS); // Materials

        pathtracerSSBOs.push_back(createStorageBuffer(sizeof(Pathtracer::GPUTreeStatistics), nullptr, true));
        createSSBO(pathtracerSSBOs.back().buffer, SSBOBinding::STATISTICS); // Statistics


        // Dynamic State
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicStateInfo.pDynamicStates = dynamicStates.data();

        // Vertex Buffer
        std::vector<Vertex> vertices = {
            {{-1.0f, -1.0f}, {0.0f, 0.0f}},
            {{ 1.0f, -1.0f}, {1.0f, 0.0f}},
            {{-1.0f,  1.0f}, {0.0f, 1.0f}},

            {{-1.0f,  1.0f}, {0.0f, 1.0f}},
            {{ 1.0f, -1.0f}, {1.0f, 0.0f}},
            {{ 1.0f,  1.0f}, {1.0f, 1.0f}}
        };

        //VkDeviceMemory vertexBufferMemory;
        this->vertexBuffer.device = this->device;
        this->vertexBuffer.size = sizeof(vertices[0]) * vertices.size();
        VkBufferCreateInfo vertexBufferInfo{};
        vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfo.size = this->vertexBuffer.size;
        vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(this->device, &vertexBufferInfo, nullptr, &this->vertexBuffer.buffer) != VK_SUCCESS) {
            std::cerr << "Failed to create vertex buffer\n";
            return;
        }

        VkMemoryRequirements vertexBufferMemRequirements;
        vkGetBufferMemoryRequirements(this->device, this->vertexBuffer.buffer, &vertexBufferMemRequirements);

        VkPhysicalDeviceMemoryProperties vertexBufferMemProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &vertexBufferMemProperties);
        int memType = -1;
        for (uint32_t i = 0; i < vertexBufferMemProperties.memoryTypeCount && memType == -1; i++)
            if ((vertexBufferMemRequirements.memoryTypeBits & (1 << i)) &&
                (vertexBufferMemProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                memType = i;

        VkMemoryAllocateInfo vertexBufferAllocInfo{};
        vertexBufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vertexBufferAllocInfo.allocationSize = vertexBufferMemRequirements.size;
        vertexBufferAllocInfo.memoryTypeIndex = memType;

        if (vkAllocateMemory(this->device, &vertexBufferAllocInfo, nullptr, &vertexBuffer.memory) != VK_SUCCESS) {
            std::cerr << "Failed to allocate vertex buffer\n";
            return;
        }

        vkBindBufferMemory(this->device, this->vertexBuffer.buffer, vertexBuffer.memory, 0);

        // Once the vertex buffer is allocated and created, it's time to fill it with data
        void* data;
        vkMapMemory(this->device, vertexBuffer.memory, 0, vertexBufferInfo.size, 0, &data);
        memcpy(data, vertices.data(), vertexBufferInfo.size);
        vkUnmapMemory(this->device, vertexBuffer.memory);

        /*
            Unfortunately the driver may not immediately copy the data into the buffer memory, for example because of caching.
            It is also possible that writes to the buffer are not visible in the mapped memory yet.
            There are two ways to deal with that problem:

                Use a memory heap that is host coherent, indicated with VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                Call vkFlushMappedMemoryRanges after writing to the mapped memory, and call vkInvalidateMappedMemoryRanges before reading from the mapped memory

            We went for the first approach, which ensures that the mapped memory always matches the contents
            of the allocated memory. Do keep in mind that this may lead to slightly worse performance than explicit
            flushing.
            For a improved performance, use staging buffers.
        */

        // Vertex Input Layout
        auto bindingDescription = Vertex::getBindingDescription();
        VkVertexInputAttributeDescription attributeDescriptions[2];
        Vertex::getAttributeDescriptions(attributeDescriptions);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = 2;
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;


        this->viewport.x = 0.0f;
        this->viewport.y = 0.0f;
        this->viewport.width = (float)this->extent.width;
        this->viewport.height = (float)this->extent.height;
        this->viewport.minDepth = 0.0f;
        this->viewport.maxDepth = 1.0f;

        this->scissor.offset = { 0, 0 };
        this->scissor.extent = this->extent;

        VkPipelineViewportStateCreateInfo viewportStateInfo{};
        viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateInfo.viewportCount = 1;
        viewportStateInfo.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizerInfo{};
        rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizerInfo.depthClampEnable = VK_FALSE;
        rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizerInfo.lineWidth = 1.0f;
        rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizerInfo.depthBiasEnable = VK_FALSE;
        rasterizerInfo.depthBiasConstantFactor = 0.0f;
        rasterizerInfo.depthBiasClamp = 0.0f;
        rasterizerInfo.depthBiasSlopeFactor = 0.0f;


        VkPipelineMultisampleStateCreateInfo multisamplingInfo{};
        multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisamplingInfo.sampleShadingEnable = VK_FALSE;
        multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisamplingInfo.minSampleShading = 1.0f;
        multisamplingInfo.pSampleMask = nullptr;
        multisamplingInfo.alphaToCoverageEnable = VK_FALSE;
        multisamplingInfo.alphaToOneEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE; // VK_TRUE
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // VK_BLEND_FACTOR_SRC_ALPHA
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlendingInfo{}; // Array of structures for all of the framebuffers
        colorBlendingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendingInfo.logicOpEnable = VK_FALSE;
        colorBlendingInfo.logicOp = VK_LOGIC_OP_COPY;
        colorBlendingInfo.attachmentCount = 1;
        colorBlendingInfo.pAttachments = &colorBlendAttachment;
        colorBlendingInfo.blendConstants[0] = 0.0f;
        colorBlendingInfo.blendConstants[1] = 0.0f;
        colorBlendingInfo.blendConstants[2] = 0.0f;
        colorBlendingInfo.blendConstants[3] = 0.0f;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &this->descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &this->pipelineLayout) != VK_SUCCESS) {
            std::cerr << "Failed to create VkCreatePipelineLayout\n";
            return;
        }

        VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingInfo.colorAttachmentCount = 1;
        pipelineRenderingInfo.pColorAttachmentFormats = &this->surfaceFormat.format;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &pipelineRenderingInfo; // this is essential for dynamic rendering!
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportStateInfo;
        pipelineInfo.pRasterizationState = &rasterizerInfo;
        pipelineInfo.pMultisampleState = &multisamplingInfo;
        pipelineInfo.pDepthStencilState = nullptr;
        pipelineInfo.pColorBlendState = &colorBlendingInfo;
        pipelineInfo.pDynamicState = &dynamicStateInfo;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(this->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &this->graphicsPipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create VkCreatePipeline\n";
            return;
        }

        // Descriptor set layout for compute (storage image + uniform + sampler)
        VkPipelineLayoutCreateInfo computePipelineLayoutInfo{};
        computePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computePipelineLayoutInfo.setLayoutCount = 1;
        computePipelineLayoutInfo.pSetLayouts = &computeDescriptorSetLayout;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(Pathtracer::PushConstants);
        computePipelineLayoutInfo.pushConstantRangeCount = 1;
        computePipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        
        vkCreatePipelineLayout(device, &computePipelineLayoutInfo, nullptr, &computePipelineLayout);

        // Compute pipeline
        VkShaderModule pathtracerComputeShaderModule = this->createShaderModule("shaders\\pathtracer.spv", ShaderType::COMPUTE).value();
        VkComputePipelineCreateInfo computePipelineInfo{};
        computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computePipelineInfo.stage.module = pathtracerComputeShaderModule;
        computePipelineInfo.stage.pName = "main";
        computePipelineInfo.layout = computePipelineLayout;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &computePipeline);

        vkDestroyShaderModule(device, vsShaderModule, nullptr);
        vkDestroyShaderModule(device, fsShaderModule, nullptr);
        vkDestroyShaderModule(device, pathtracerComputeShaderModule, nullptr);

        // 
        VkCommandBufferAllocateInfo cmdBufferAllocInfo{};
        cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufferAllocInfo.commandPool = this->commandPool;
        cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufferAllocInfo.commandBufferCount = (uint32_t)MAX_FRAMES_IN_FLIGHT;

        if (vkAllocateCommandBuffers(this->device, &cmdBufferAllocInfo, this->commandBuffers) != VK_SUCCESS) {
            std::cerr << "Failed to allocate with VkAllocateCommandBuffers\n";
            return;
        }

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Workaround to avoid vkWaitForFences block the first frame forever

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateSemaphore(this->device, &semaphoreInfo, nullptr, this->imageAvailableSemaphores + i) != VK_SUCCESS ||
                vkCreateFence(this->device, &fenceInfo, nullptr, this->inFlightFences + i) != VK_SUCCESS) {
                std::cerr << "Failed to create a VkSemaphore or VkFence\n";
                return;
            }
        }

        for (size_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; ++i) {
            if (vkCreateSemaphore(this->device, &semaphoreInfo, nullptr, this->renderFinishedSemaphores + i) != VK_SUCCESS) {
                std::cerr << "Failed to create a VkSemaphore or VkFence\n";
                return;
            }
        }

        this->imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);
    }

public:
    void shutdown() {
        vkDeviceWaitIdle(device);

        
        // Synchronization objects
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; ++i)
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);

        imagesInFlight.clear();

        
        // Descriptor resources
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);

        
        // Pipelines & layouts
        vkDestroyPipeline(device, graphicsPipeline, nullptr);

        vkDestroyPipeline(device, computePipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

        vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);

        
        // Command pool (implicitly frees command buffers)
        vkDestroyCommandPool(device, commandPool, nullptr);

        
        // Path tracer ping-pong images
        for (uint32_t i = 0; i < PATHTRACER_IMG_COUNT; ++i) {
            vkDestroyImageView(device, pathtracerImageViews[i], nullptr);
            vkDestroyImage(device, pathtracerImages[i], nullptr);
            vkFreeMemory(device, pathtracerImagesMemory[i], nullptr);
        }

        vkDestroySampler(device, pathtracerImageSampler, nullptr);

        vkDestroySampler(device, fragImageSampler, nullptr);

        
        // Swapchain resources
        for (VkImageView view : swapChainImageViews)
            vkDestroyImageView(device, view, nullptr);

        swapChainImageViews.clear();
        swapChainImages.clear();

        vkDestroySwapchainKHR(device, swapChain, nullptr);

        
        // Buffers
        vertexBuffer.~Buffer();
        
        pathtracerSSBOs.clear();
        stagingBuffers.clear();

        pathtracerUBO.~Buffer();
        
        // Device / instance
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
    }



    void run(uint32_t& currentFrame) {
        //puts("=== START LOOP ===");

        // Wait for frame's fence (makes command buffer safe to reuse)
        VkResult waitResult = vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        if (waitResult != VK_SUCCESS) {
            puts("inFlightFences waiting");
            return;
        }

        // Acquire swapchain image
        uint32_t imageIndex;
        VkResult acquireResult = vkAcquireNextImageKHR(device, swapChain, 2000000000ULL, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) return;
        if (acquireResult != VK_SUCCESS) {
            fprintf(stderr, "vkAcquireNextImageKHR failed: %d\n", acquireResult);
            vkDeviceWaitIdle(device);
            return;
        }


        // If this swapchain image is already in-flight, wait for it
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            waitResult = vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, 2000000000ULL);
            if (waitResult != VK_SUCCESS) {
                vkDeviceWaitIdle(device);
                return;
            }
        }

        // Reset the fence AFTER it is no longer used
        vkResetFences(device, 1, &inFlightFences[currentFrame]);

        // Assign fence for this frame to this image
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        // Reset command buffer
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        //puts("==== AFTER FENCES ====");

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferBeginInfo.flags = 0;
        cmdBufferBeginInfo.pInheritanceInfo = nullptr;

        if (vkBeginCommandBuffer(this->commandBuffers[currentFrame], &cmdBufferBeginInfo) != VK_SUCCESS) {
            std::cerr << "Failed to record VkBeginCommandBuffer\n";
            return;
        }

        VkImageMemoryBarrier barrier{}; // Transition of Layouts
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        //printf("=== LAYOUT: %d ===\n", pathtracerImageLayouts[textureIndex]);
        barrier.oldLayout = swapchainImageLayouts[imageIndex];
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = this->swapChainImages[imageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            this->commandBuffers[currentFrame],
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        swapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        updateUniformBuffer(pathtracerUBO, &pathtracerState, sizeof(PathtracerUBO));
        transitionCompute(this->commandBuffers[currentFrame], pathtracerImages[textureIndex], currentFrame);

        VkRenderingAttachmentInfo colorAttachment{};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = this->swapChainImageViews[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        colorAttachment.clearValue = clearColor;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = { 0, 0 };
        renderingInfo.renderArea.extent = this->extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(this->commandBuffers[currentFrame], &renderingInfo);

        // Record draw commands here
        vkCmdBindPipeline(this->commandBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, this->graphicsPipeline);

        // bind vertex buffers, descriptor sets, and issue draw calls...
        VkBuffer vertexBuffers[] = { this->vertexBuffer.buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(this->commandBuffers[currentFrame], 0, 1, vertexBuffers, offsets);
        //vkCmdDraw(this->commandBuffers[currentFrame], 6, 1, 0, 0);

        vkCmdBindDescriptorSets(
            this->commandBuffers[currentFrame],
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            this->pipelineLayout,
            0,              // First set
            1, &this->fragDescriptorSet[currentFrame%PING_PONG_FRAMES],
            0, nullptr
        );

        vkCmdSetViewport(this->commandBuffers[currentFrame], 0, 1, &this->viewport);
        vkCmdSetScissor(this->commandBuffers[currentFrame], 0, 1, &this->scissor);

        vkCmdDraw(this->commandBuffers[currentFrame], 6, 1, 0, 0);

        vkCmdEndRendering(this->commandBuffers[currentFrame]);

        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = this->swapChainImages[imageIndex];  // <- The swapchain image used this frame
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;

        vkCmdPipelineBarrier(
            this->commandBuffers[currentFrame],
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
        swapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        if (vkEndCommandBuffer(this->commandBuffers[currentFrame]) != VK_SUCCESS) {
            std::cerr << "Failed to record VkEndCommandBuffer\n";
            return;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { this->imageAvailableSemaphores[currentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &this->commandBuffers[currentFrame];
        VkSemaphore signalSemaphores[] = { this->renderFinishedSemaphores[imageIndex] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, this->inFlightFences[currentFrame]) != VK_SUCCESS) {
            std::cerr << "Failed to submit to the graphics queue on VkQueueSubmit\n";
            return;
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { this->swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;

        vkQueuePresentKHR(this->presentQueue, &presentInfo);

        this->textureIndex ^= 1;
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        pathtracerState.iFrame++;
        //puts("=== END LOOP ===");
    }

    Pathtracer::Statistics GetStatistics() const {
        Pathtracer::Benchmark binfo = this->pathtracerConfig.GetBenchmarkInfo();
        if (!binfo.btype) return {};

        const uint32_t STAGING_STATS = 1, STAGING_ACC = 0;
        VkImage accImg = this->pathtracerImages[PATHTRACER_IMG_COUNT - 1];

        VkCommandBuffer cmdbf = beginSingleTimeCommands();
        transitionImageLayout(accImg, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmdbf);

        VkBufferImageCopy imgCopy{};
        imgCopy.bufferOffset = 0;
        imgCopy.bufferRowLength = 0; // tightly packed
        imgCopy.bufferImageHeight = 0;
        imgCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgCopy.imageSubresource.mipLevel = 0;
        imgCopy.imageSubresource.baseArrayLayer = 0;
        imgCopy.imageSubresource.layerCount = 1;
        imgCopy.imageOffset = { 0, 0, 0 };
        imgCopy.imageExtent = { this->WIDTH, this->HEIGHT, 1 };

        vkCmdCopyImageToBuffer(cmdbf, accImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, this->stagingBuffers[STAGING_ACC].buffer, 1, &imgCopy);

        const Buffer& statsSSBO = this->pathtracerSSBOs.back();
        const Buffer& stagingStats = this->stagingBuffers[STAGING_STATS];

        VkBufferCopy statsCopy{};
        statsCopy.srcOffset = 0;
        statsCopy.dstOffset = 0;
        statsCopy.size = sizeof(Pathtracer::GPUTreeStatistics);

        vkCmdCopyBuffer(cmdbf, statsSSBO.buffer, stagingStats.buffer, 1, &statsCopy);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdbf;

        endSingleTimeCommands(cmdbf);
        vkQueueWaitIdle(this->graphicsQueue);

        std::vector<glm::vec4> pixels;
        void* mappedPixels = nullptr;
        vkMapMemory(this->device, this->stagingBuffers[STAGING_ACC].memory, 0, VK_WHOLE_SIZE, 0, &mappedPixels);
        pixels.resize(this->WIDTH * this->HEIGHT);
        memcpy(pixels.data(), mappedPixels, this->WIDTH * this->HEIGHT * sizeof(glm::vec4));
        vkUnmapMemory(this->device, this->stagingBuffers[STAGING_ACC].memory);

        void* mappedStats = nullptr;
        vkMapMemory(this->device, stagingStats.memory, 0, sizeof(Pathtracer::GPUTreeStatistics), 0, &mappedStats);
        Pathtracer::GPUTreeStatistics gpuTreeStats = *reinterpret_cast<Pathtracer::GPUTreeStatistics*>(mappedStats);
        vkUnmapMemory(this->device, stagingStats.memory);

        std::cout << "[STATS]\n RAYS: " << gpuTreeStats.rays.hi << " " << gpuTreeStats.rays.lo << "\n TRAVERSALS: " << gpuTreeStats.traversals.hi << " " << gpuTreeStats.traversals.lo << "\n ISECS: " << gpuTreeStats.isecs.hi << " " << gpuTreeStats.isecs.lo << "\n";
        Pathtracer::TreeStatistics treeStats { gpuTreeStats.rays.lo | (uint64_t(gpuTreeStats.rays.hi) << 32ULL), gpuTreeStats.isecs.lo | (uint64_t(gpuTreeStats.isecs.hi) << 32ULL), gpuTreeStats.traversals.lo | (uint64_t(gpuTreeStats.traversals.hi) << 32ULL) };
        
        /*
        VkClearColorValue clearVal = { 0.f, 0.f, 0.f, 0.f };
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmdbf, accImg, VK_IMAGE_LAYOUT_GENERAL, &clearVal, 1, &range);
        */

        for (auto& px : pixels)
            px /= static_cast<float>(binfo.spp);
        
        if (this->pathtracerConfig.ShouldSaveImage()) {
            writePPM(RESOURCE("outputs\\output.ppm"), pixels, this->WIDTH, this->HEIGHT, false);
            writePPM(RESOURCE("outputs\\output-g.ppm"), pixels, this->WIDTH, this->HEIGHT, true);
        }

        Pathtracer::Statistics stats = this->pathtracerStatistics;
        stats.treeStats = treeStats;
        return stats;
    }

private:
    void writePPM(const std::string& filename, const std::vector<glm::vec4>& pixels, int width, int height, bool useGamma = true) const {
        std::ofstream out(filename, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to open file for writing PPM");
        }

        auto linearToSRGB = [](float c) {
            c = std::clamp(c, 0.f, 1.f);
            return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        };

        // Header
        out << "P6\n" << width << " " << height << "\n255\n";

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const glm::vec4& c = pixels[y * width + x];
                float r = std::clamp(c.r, 0.0f, 1.0f);
                float g = std::clamp(c.g, 0.0f, 1.0f);
                float b = std::clamp(c.b, 0.0f, 1.0f);

                if (useGamma) {
                    r = linearToSRGB(r);
                    g = linearToSRGB(g);
                    b = linearToSRGB(b);
                }

                unsigned char rgb[3];
                rgb[0] = static_cast<unsigned char>(r * 255.0f);
                rgb[1] = static_cast<unsigned char>(g * 255.0f);
                rgb[2] = static_cast<unsigned char>(b * 255.0f);

                out.write(reinterpret_cast<char*>(rgb), 3);
            }
        }

        out.close();
    }
};