#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H
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
#include "models/io/OBJLoader.h"
#include "models/io/EXR.h"
#include "models/io/PPM.h"
#include "models/acceleration_structures/BVH.h"
#include "models/acceleration_structures/KdTreeBinned.h"
#include "models/acceleration_structures/KdTree.h"
#include "models/vulkan/Buffer.h"
#include "models/vulkan/Shader.h"
#include "models/vulkan/Vulkan.h"
#include "PathtracerSettings.h"
#include "models/compute_backends/SPIRV.h"
#ifdef HAS_CUDA
#include "models/compute_backends/CUDA.h"
#endif

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

class Renderer {

    // How many frames you let the CPU start rendering before the GPU is done
    // MAX_FRAMES_IN_FLIGHT: How many frames the CPU can work on at once
    // Swap Chain: How many images are available to render into
    // They are not directly tied, but you need to consider things like: MAX_FRAMES_IN_FLIGHT = min(MAX_FRAMES_IN_FLIGHT, SC.size())
    static constexpr uint32_t SWAPCHAIN_IMAGE_COUNT = 2;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2; //SWAPCHAIN_IMAGE_COUNT - 1;

    std::unique_ptr<Pathtracer::ComputeBackend> computeBackend;

    VkInstance instance; // Connection between Vulkan and the main program
    VkPhysicalDevice physicalDevice;
    VkDevice device; // After selecting a Physical Device, create a logical device to interface with it
    VkPhysicalDeviceProperties physicalDeviceProperties;

#ifdef HAS_CUDA
    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
        //VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME - Not available
    };
#else
    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        //VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME - Not available
    };
#endif


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
    VkDescriptorPool descriptorPool;
    VkDescriptorSet fragDescriptorSet[PING_PONG_FRAMES];

    /*VkImage pathtracerImages[PATHTRACER_IMG_COUNT];
    VkDeviceMemory pathtracerImagesMemory[PATHTRACER_IMG_COUNT];
    VkImageView pathtracerImageViews[PATHTRACER_IMG_COUNT];
    VkImageLayout pathtracerImageLayouts[PATHTRACER_IMG_COUNT];
    VkSampler pathtracerImageSampler;*/
    VkSampler fragImageSampler;
    int textureIndex = 0;

    Buffer vertexBuffer;
    VkPipelineLayout pipelineLayout;
    //VkPipelineLayout computePipelineLayout;
    VkPipeline graphicsPipeline;
    //VkPipeline computePipeline;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];

    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT]; // Ensures the submit waits until the acquired image is ready (from vkAcquireNextImageKHR)
    VkSemaphore renderFinishedSemaphores[SWAPCHAIN_IMAGE_COUNT]; // Ensures the present (vkQueuePresentKHR) waits until rendering to that image is done.

    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT];
    std::vector<VkFence> imagesInFlight;

    Pathtracer::Config pathtracerConfig;
    Pathtracer::Statistics pathtracerStatistics;
    Pathtracer::FrameContext pathtracerState{};
    const uint32_t WIDTH, HEIGHT;
public:
    Renderer(const Pathtracer::Config& pathtracerConfig) :
        pathtracerConfig(pathtracerConfig), WIDTH(pathtracerConfig.GetResolution().x), HEIGHT(pathtracerConfig.GetResolution().y)
    {
    }

    ~Renderer() {
        this->shutdown();
    }

    void init(GLFWwindow* window) {
        createInstance();
        createWindowSurface(window);
        createDevice();
        createSwapchain(window);
        createCommandPool();
        createDescriptorPool();
        createComputeBackend();
        createGraphicsPipeline();
        initPathtracerState();
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

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        #ifdef HAS_CUDA
        extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
        #endif

        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());;
        instanceInfo.ppEnabledExtensionNames = extensions.data();
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
        VkPhysicalDeviceShaderAtomicInt64Features atomic64Feature{};
        atomic64Feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
        atomic64Feature.shaderBufferInt64Atomics = VK_TRUE;
        atomic64Feature.shaderSharedInt64Atomics = VK_TRUE;

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
                physicalDeviceProperties = deviceProperties;
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
                //graphicsQueueFamilyIndex = i;
                std::cout << " Queue family " << i << " supports graphics operations\n";
            };
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                //computeQueueFamilyIndex = i;
                std::cout << " Queue family " << i << " supports compute operations\n";
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, this->surface, &presentSupport);
            if (presentSupport) {
                //presentQueueFamilyIndex = i;
                std::cout << " Queue family " << i << " supports window surface\n";
            }
            
            if((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && presentSupport)
                graphicsQueueFamilyIndex = computeQueueFamilyIndex = presentQueueFamilyIndex = i;
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
            queueCreateInfos.emplace_back(queueCreateInfo);
        }

        dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

        dynamicRenderingFeatures.pNext = &atomic64Feature;
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.features = {};
        deviceFeatures2.features.shaderInt64 = VK_TRUE;
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

    void createComputeBackend() {
        Pathtracer::SPIRV::VulkanContext vkCtx = { this->physicalDevice, this->device, this->commandPool, this->graphicsQueue, this->descriptorPool };
        if (pathtracerConfig.GetComputeBackendType() == Pathtracer::ComputeBackendType::SPIRV_T) 
            this->computeBackend = std::make_unique<Pathtracer::SPIRV>(vkCtx, this->pathtracerConfig);
        else {
            #ifdef HAS_CUDA
            //Pathtracer::CUDA::CudaContext cuCtx;
            this->computeBackend = std::make_unique<Pathtracer::CUDA>(vkCtx, this->pathtracerConfig);
            #endif
        }
    }

    void createDescriptorPool() {
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * PING_PONG_FRAMES }, // 2 fragment textures (double buffering)
        };

        if (this->pathtracerConfig.GetComputeBackendType() == Pathtracer::SPIRV_T) {
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * PING_PONG_FRAMES });  // 1 lastFrameTex
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * PING_PONG_FRAMES });          // Only for compute UBO
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * PING_PONG_FRAMES });           // compute output image + acc image framebuffer
            poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Pathtracer::SPIRV::SSBOsCount * PING_PONG_FRAMES }); // SSBOs
        }

        const uint32_t nDescriptorSets = 1 + (this->pathtracerConfig.GetComputeBackendType() == Pathtracer::SPIRV_T);
        const uint32_t maxSets = nDescriptorSets * PING_PONG_FRAMES;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = maxSets;

        vkCreateDescriptorPool(this->device, &poolInfo, nullptr, &this->descriptorPool);
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

    VkCommandBuffer beginSingleTimeCommands() const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

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

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void dispatchCompute(VkCommandBuffer commandBuffer, int currentFrame) {
        /*
        A timestamp query measures GPU time between two points in the command buffer
        It does not measure "this dispatch" unless you bracket it
        */

        Pathtracer::ComputeBackend::DispatchConext dispatchCtx = {
            .commandBuffer = commandBuffer,
            .currentFrame = (uint32_t)currentFrame,
            .textureIndex = (uint32_t)textureIndex,
            .tileSize = this->pathtracerConfig.GetTileSize(),
            .lightBounces = this->pathtracerConfig.GetLightBounces()
        };
        this->computeBackend->dispatch(dispatchCtx);
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;

        if (vkCreateCommandPool(this->device, &cmdPoolInfo, nullptr, &this->commandPool) != VK_SUCCESS) {
            std::cerr << "Failed to create VkCreateCommandPool\n";
            return;
        }
    }

    void initPathtracerState() {
        pathtracerState.iFrame = 0;
        pathtracerState.iResolution = glm::vec2(this->WIDTH, this->HEIGHT);
        pathtracerState.iTime = 0;
        pathtracerState.camera.cameraPos = glm::vec4(-0.0f, 0.1f, -0.6f, 0.0);
        pathtracerState.camera.cameraRot = glm::vec4(0, 0, 0, 0);
    }

    void initComputeBackend() {
        std::string sceneFilepath = RESOURCE("scenes\\") + this->pathtracerConfig.GetScene();
        OBJLoader objloader((sceneFilepath + ".obj").c_str());
        std::vector<OBJLoader::Triangle> triangles = objloader.GetTriangles();
        std::vector<OBJLoader::Vertex> objVertices = objloader.GetObjVertices();

        pathtracerStatistics.sceneTriangles = triangles.size();

        // Load light geometry
        OBJLoader lightLoader((sceneFilepath + "-light.obj").c_str());
        std::vector<OBJLoader::Triangle> lightTris = lightLoader.GetTriangles();
        std::vector<OBJLoader::Vertex> lightVerts = lightLoader.GetObjVertices();

        OBJLoader::Material lightMat = { glm::vec4(0.0f), 0.0f, 1.0f, 0.0f, 0.0f, glm::vec3(1.0f), 32.0f };
        std::vector<OBJLoader::Material> materials = objloader.GetMaterials();
        materials.emplace_back(lightMat);

        // Merge light vertices
        uint32_t vertexOffset = objVertices.size();
        objVertices.insert(objVertices.end(), lightVerts.begin(), lightVerts.end());
        for (auto& lt : lightTris) lt.indices = glm::uvec4(lt.indices.x + vertexOffset, lt.indices.y + vertexOffset, lt.indices.z + vertexOffset, materials.size() - 1);

        // Create SSBOs
        OBJLoader::MeshGeometry mergedMesh{ objVertices, triangles };

        std::vector<uint32_t> indices = {};
        std::variant<std::vector<BVH::Node>, std::vector<KdTree::Node>> treeVariant;

        if (this->pathtracerConfig.GetAccelerationStructureType() == Pathtracer::AccelerationStructureType::BVH) {
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

            const std::vector<BVH::Node>& tree = bvh.GetTree();
            treeVariant = tree;
            pathtracerStatistics.accStructMemoryUsage = tree.size() * sizeof(tree[0]);
        }
        else {
            KdTree kdh = KdTree(mergedMesh);
            auto t0 = std::chrono::high_resolution_clock::now();
            kdh.Build();
            auto t1 = std::chrono::high_resolution_clock::now();
            pathtracerStatistics.accStructBuildTime = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() / 1000.0f;
            //kdh.Print();

            const std::vector<KdTree::Node>& tree = kdh.GetTree();
            treeVariant = tree;
            pathtracerStatistics.accStructMemoryUsage = tree.size() * sizeof(tree[0]);

            /*
            objVertices = kdh.GetVertices();
            triangles = kdh.GetTriangles();
            */

            indices = kdh.GetIndices();


            /*
            for (uint32_t idx = 0; idx < triangles.size(); idx++) {
                printf("v %.2f %.2f %.2f\n", objVertices[triangles[idx].indices.x].position.x, objVertices[triangles[idx].indices.x].position.y, objVertices[triangles[idx].indices.x].position.z);
                printf("v %.2f %.2f %.2f\n", objVertices[triangles[idx].indices.y].position.x, objVertices[triangles[idx].indices.y].position.y, objVertices[triangles[idx].indices.y].position.z);
                printf("v %.2f %.2f %.2f\n", objVertices[triangles[idx].indices.z].position.x, objVertices[triangles[idx].indices.z].position.y, objVertices[triangles[idx].indices.z].position.z);
            }
            */
        }

        Pathtracer::ComputeBackend::SceneData sceneData = {
            .vertices = objVertices,
            .triangles = triangles,
            .lightTriangles = lightTris,
            .tree = std::move(treeVariant),
            .indices_kdtree = indices,
            .materials = materials
        };

        this->computeBackend->init(sceneData);
    }

    void createGraphicsPipeline() {
        // Graphics Pipeline
        // Gotta change cmake.txt
        char buffer[FILENAME_MAX];
        _getcwd(buffer, FILENAME_MAX);
        std::cout << "Current working directory: " << buffer << std::endl;

        // Shaders
        system(RESOURCE("shaders\\runtime_compile.bat"));

        Shader vertexShader = Shader("shaders\\vert.spv", Shader::Type::VERTEX, this->device);
        Shader fragmentShader = Shader("shaders\\frag.spv", Shader::Type::FRAGMENT, this->device);

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertexShader.GetShaderCreateInfo(), fragmentShader.GetShaderCreateInfo() };

        initComputeBackend();

        std::vector<VkDescriptorSetLayoutBinding> fragBindings = {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
        };
        Vulkan::createDescriptorSetLayout(device, fragBindings, &this->descriptorSetLayout);

        for (int i = 0; i < PING_PONG_FRAMES; i++)
            this->fragDescriptorSet[i] = Vulkan::allocateDescriptorSet(this->device, this->descriptorPool, &descriptorSetLayout);

        create2DLinearImageSampler();
        for (int i = 0; i < PING_PONG_FRAMES; i++)
            // Fragment shader reads current frame image
            Vulkan::updateCombinedImageSamplerDescriptorSet(device, fragDescriptorSet[i], 0, fragImageSampler, this->computeBackend->getPathtracerImageView(i), pathtracerConfig.GetComputeBackendType() == Pathtracer::CUDA_T ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


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

        // Compute Pipeline

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

        computeBackend.reset();
        
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


        
        // Pipelines & layouts
        vkDestroyPipeline(device, graphicsPipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

        
        // Command pool (implicitly frees command buffers)
        vkDestroyCommandPool(device, commandPool, nullptr);

        vkDestroySampler(device, fragImageSampler, nullptr);

        
        // Swapchain resources
        for (VkImageView view : swapChainImageViews)
            vkDestroyImageView(device, view, nullptr);

        swapChainImageViews.clear();
        swapChainImages.clear();

        vkDestroySwapchainKHR(device, swapChain, nullptr);

        
        // Buffers
        vertexBuffer.~Buffer();
        
        // Device / instance
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
    }



    void run(uint32_t& currentFrame) {
        //puts("=== START LOOP ===");

        // Wait for frame's fence (makes command buffer safe to reuse)
        VkResult waitResult = vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX); // guarantees the previous submission/cmdbuf that used currentFrame is finished
        if (waitResult != VK_SUCCESS) {
            puts("inFlightFences waiting");
            return;
        }

        if (pathtracerState.iFrame >= MAX_FRAMES_IN_FLIGHT)
            // Safe to read timestamp for the frame that last used this index
            this->pathtracerStatistics.avgKernelTime += this->computeBackend->queryDispatchTime(currentFrame, physicalDeviceProperties.limits.timestampPeriod);

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

        this->computeBackend->updateFrameContext(&pathtracerState, sizeof(pathtracerState));
        if(this->pathtracerConfig.GetComputeBackendType() == Pathtracer::SPIRV_T)
            dispatchCompute(this->commandBuffers[currentFrame], currentFrame);

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


        std::vector<VkSemaphore> waitSemaphores = { this->imageAvailableSemaphores[currentFrame] };
        std::vector<VkPipelineStageFlags> waitStages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        
        std::vector<VkSemaphore> signalSemaphores = { this->renderFinishedSemaphores[imageIndex] };
        
        Pathtracer::ComputeBackend::SyncContext syncCtx = { waitSemaphores, waitStages, signalSemaphores, currentFrame };
        this->computeBackend->sync(syncCtx);
        
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = waitSemaphores.size();
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &this->commandBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = signalSemaphores.size();
        submitInfo.pSignalSemaphores = signalSemaphores.data();

        if (vkQueueSubmit(this->graphicsQueue, 1, &submitInfo, this->inFlightFences[currentFrame]) != VK_SUCCESS) {
            std::cerr << "Failed to submit to the graphics queue on VkQueueSubmit\n";
            return;
        }

        // Dispatching here due to sync problems
        if (this->pathtracerConfig.GetComputeBackendType() == Pathtracer::CUDA_T)
            dispatchCompute(VK_NULL_HANDLE, currentFrame);

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = signalSemaphores.size();
        presentInfo.pWaitSemaphores = signalSemaphores.data();

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

    void wait() const {
        vkQueueWaitIdle(this->graphicsQueue);
    }

    Pathtracer::Statistics GetStatistics() const {
        Pathtracer::Benchmark binfo = this->pathtracerConfig.GetBenchmarkInfo();
        if (!binfo.btype) return {};

        //const uint32_t STAGING_STATS = 1, STAGING_ACC = 0;

        std::vector<glm::vec4> pixels;
        this->computeBackend->getBackendAccOutImgPixels(pixels);

        /*
        VkClearColorValue clearVal = { 0.f, 0.f, 0.f, 0.f };
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmdbf, accImg, VK_IMAGE_LAYOUT_GENERAL, &clearVal, 1, &range);
        */

        Pathtracer::Statistics stats = this->pathtracerStatistics;
        stats.treeStats = this->computeBackend->getBackendStatistics();
        
        for (auto& px : pixels)
            px /= static_cast<float>(binfo.spp);

        auto mse = [](glm::vec4 p1, glm::vec4 p2) -> double {
            double dx = p1.x - p2.x;
            double dy = p1.y - p2.y;
            double dz = p1.z - p2.z;
            return (dx * dx + dy * dy + dz * dz) / 3;
        };

        
        if (binfo.btype == Pathtracer::IMGREF) {
            std::string filepath = RESOURCE_PATH_PREFIX + "outputs\\" + this->pathtracerConfig.GetScene() + "\\ref.exr";
            std::vector<glm::vec4> groundTruth;
            uint32_t w, h;
            EXR::Load(filepath, groundTruth, w, h);
            double smse = 0.0;
            for (size_t i = 0; i < pixels.size(); i++)
                smse += mse(pixels[i], groundTruth[i]);
            smse /= pixels.size();
            stats.rmse = sqrt(smse);
            stats.psnr = 10.0 * log10(1.0 / smse);
        }
        
        if (this->pathtracerConfig.ShouldSaveImage()) {
            std::string filepath = RESOURCE_PATH_PREFIX + "outputs\\" + (binfo.btype != Pathtracer::IMGREF ? "output" : this->pathtracerConfig.GetScene() + "\\output" + this->pathtracerConfig.InlineString());
            EXR::Save(filepath + ".exr", pixels, this->WIDTH, this->HEIGHT);
            PPM::Save(filepath + ".ppm", pixels, this->WIDTH, this->HEIGHT, false);
            //if(binfo.btype != Pathtracer::IMGREF) PPM::Save(filepath + "-g.ppm", pixels, this->WIDTH, this->HEIGHT, true);
        }

        return stats;
    }

};

#endif