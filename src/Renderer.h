#ifndef VULKAN_RENDERER_H
#define VULKAN_RENDERER_H
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <set>
#include <fstream>
#include <optional>
#include <chrono>
#include <filesystem>
#include <glm/glm.hpp>
#include "include/io/OBJLoader.h"
#include "include/io/EXR.h"
#include "include/io/PPM.h"
#include "include/acceleration_structures/BVH.h"
#include "include/acceleration_structures/BVH4.h"
#include "include/acceleration_structures/KdTree.h"
#include "include/vulkan/Buffer.h"
#include "include/vulkan/Vulkan.h"
#include "PathtracerSettings.h"
#include "include/compute_backends/SPIRV.h"
#ifdef HAS_CUDA
#include "include/compute_backends/CUDA.h"
#endif

#ifdef DEBUG
constexpr bool enableValidationLayers = true;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
#else
constexpr bool enableValidationLayers = false;
#endif

class Renderer {

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    std::unique_ptr<Pathtracer::ComputeBackend> computeBackend;
    bool initSucceeded = false;

    VkInstance instance = VK_NULL_HANDLE; // Connection between Vulkan and the main program
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE; // After selecting a Physical Device, create a logical device to interface with it
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

#ifdef HAS_CUDA
    const std::vector<const char*> deviceExtensions = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
    };
#else
    const std::vector<const char*> deviceExtensions = {};
#endif


    uint32_t computeQueueFamilyIndex = 0;
    VkQueue computeQueue = VK_NULL_HANDLE;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    int textureIndex = 0;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT]{};

    VkSemaphore backendWaitSemaphores[MAX_FRAMES_IN_FLIGHT]{};
    VkSemaphore backendSignalSemaphores[MAX_FRAMES_IN_FLIGHT]{};
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT]{};

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

    void init() {
        initSucceeded = false;

        if (!createInstance()) return;
        if (!createDevice()) return;

        vkGetDeviceQueue(this->device, computeQueueFamilyIndex, 0, &this->computeQueue);
        createCommandPool();
        createDescriptorPool();
        createComputeBackend();
        initComputeBackend();
        createFrameResources();

        if (this->instance == VK_NULL_HANDLE ||
            this->device == VK_NULL_HANDLE ||
            this->commandPool == VK_NULL_HANDLE ||
            !this->computeBackend) {
            std::cerr << "Renderer initialization failed and was aborted.\n";
            return;
        }

        initPathtracerState();
        initSucceeded = true;
    }

private:
    bool checkInstanceExtensionSupport(const std::vector<const char*>& requiredExtensions) const {
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> availableExtensionNames;
        for (const auto& extension : availableExtensions)
            availableExtensionNames.insert(extension.extensionName);

        std::cout << "\n Required instance extensions:\n";
        bool allAvailable = true;
        for (const char* extension : requiredExtensions) {
            const bool found = availableExtensionNames.find(extension) != availableExtensionNames.end();
            std::cout << "  - " << extension << (found ? " [OK]" : " [MISSING]") << std::endl;
            allAvailable &= found;
        }

        if (!allAvailable)
            std::cerr << "ERROR: Not all required instance extensions are available.\n";
        else
            std::cout << " All required instance extensions are available.\n";

        return allAvailable;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice candidateDevice, std::vector<std::string>& missingExtensions) const {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(candidateDevice, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(candidateDevice, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(this->deviceExtensions.begin(), this->deviceExtensions.end());
        for (const auto& extension : availableExtensions)
            requiredExtensions.erase(extension.extensionName);

        missingExtensions.assign(requiredExtensions.begin(), requiredExtensions.end());
        return missingExtensions.empty();
    }

    bool checkDeviceFeatureSupport(VkPhysicalDevice candidateDevice, std::vector<std::string>& missingFeatures) const {
        VkPhysicalDeviceFeatures2 availableFeatures2{};
        availableFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        VkPhysicalDeviceShaderAtomicInt64Features availableAtomic64{};
        availableAtomic64.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;

        VkPhysicalDeviceScalarBlockLayoutFeatures availableScalar{};
        availableScalar.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;

        availableFeatures2.pNext = &availableAtomic64;
        availableAtomic64.pNext = &availableScalar;
        vkGetPhysicalDeviceFeatures2(candidateDevice, &availableFeatures2);

        missingFeatures.clear();
        if (availableFeatures2.features.shaderInt64 != VK_TRUE)
            missingFeatures.emplace_back("shaderInt64");
        if (availableAtomic64.shaderBufferInt64Atomics != VK_TRUE)
            missingFeatures.emplace_back("shaderBufferInt64Atomics");
        if (availableAtomic64.shaderSharedInt64Atomics != VK_TRUE)
            missingFeatures.emplace_back("shaderSharedInt64Atomics");
        if(availableScalar.scalarBlockLayout != VK_TRUE)
            missingFeatures.emplace_back("scalarBlockLayout");

        return missingFeatures.empty();
    }

    bool createInstance() {
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

        std::vector<const char*> extensions;

        #ifdef HAS_CUDA
        extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
        #endif

    #ifdef DEBUG
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    #endif

        if (!checkInstanceExtensionSupport(extensions)) {
            std::cerr << "Aborting renderer initialization due to missing instance extensions.\n";
            return false;
        }

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
            return false;
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
            if (func(instance, &debugCreateInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
                std::cerr << "Failed to create Vulkan debug messenger!" << std::endl;
            }
        }
#endif
        return true;
    }

    bool createDevice() {
        this->physicalDevice = VK_NULL_HANDLE;

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (!deviceCount) {
            std::cerr << "Failed to find GPUs with Vulkan support\n";
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        VkPhysicalDevice firstSupportedDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties firstSupportedDeviceProperties{};

        std::cout << "\n Available Devices:\n";
        VkPhysicalDeviceProperties deviceProperties{};
        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        VkPhysicalDeviceShaderAtomicInt64Features atomic64Feature{};
        atomic64Feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
        atomic64Feature.shaderBufferInt64Atomics = VK_TRUE;
        atomic64Feature.shaderSharedInt64Atomics = VK_TRUE;
        VkPhysicalDeviceScalarBlockLayoutFeatures scalarFeatures{};
        scalarFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
        scalarFeatures.scalarBlockLayout = VK_TRUE;

        for (const auto& candidateDevice : devices) {
            vkGetPhysicalDeviceProperties(candidateDevice, &deviceProperties);

            std::vector<std::string> missingExtensions;
            const bool hasRequiredExtensions = checkDeviceExtensionSupport(candidateDevice, missingExtensions);

            std::vector<std::string> missingFeatures;
            const bool hasRequiredFeatures = checkDeviceFeatureSupport(candidateDevice, missingFeatures);

            std::cout << "\n - " << deviceProperties.deviceName;
            if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                std::cout << " (Discrete)";
            }

            if (hasRequiredExtensions)
                std::cout << " (Extensions Available)";
            else {
                std::cout << " (Missing Extensions:";
                for (size_t i = 0; i < missingExtensions.size(); ++i) {
                    std::cout << " " << missingExtensions[i];
                    if (i + 1 < missingExtensions.size()) std::cout << ",";
                }
                std::cout << ")";
            }

            if (hasRequiredFeatures)
                std::cout << " (Features Available)";
            else {
                std::cout << " (Missing Features:";
                for (size_t i = 0; i < missingFeatures.size(); ++i) {
                    std::cout << " " << missingFeatures[i];
                    if (i + 1 < missingFeatures.size()) std::cout << ",";
                }
                std::cout << ")";
            }

            uint32_t apiVersion = deviceProperties.apiVersion;
            uint32_t major = VK_VERSION_MAJOR(apiVersion);
            uint32_t minor = VK_VERSION_MINOR(apiVersion);
            uint32_t patch = VK_VERSION_PATCH(apiVersion);

            std::cout << " (" << major << "." << minor << "." << patch << ")";

            std::cout << std::endl;

            if (!hasRequiredExtensions || !hasRequiredFeatures)
                continue;

            if (firstSupportedDevice == VK_NULL_HANDLE) {
                firstSupportedDevice = candidateDevice;
                firstSupportedDeviceProperties = deviceProperties;
            }

            if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                this->physicalDevice = candidateDevice;
                this->physicalDeviceProperties = deviceProperties;
                break;
            }
        }
        std::cout << std::endl;

        if (this->physicalDevice == VK_NULL_HANDLE)
            this->physicalDevice = firstSupportedDevice;
        if (this->physicalDeviceProperties.deviceName[0] == '\0')
            this->physicalDeviceProperties = firstSupportedDeviceProperties;

        if (this->physicalDevice == VK_NULL_HANDLE) {
            std::cerr << "ERROR: No physical device supports all required device extensions/features.\n";
            std::cerr << "Aborting renderer initialization due to missing device extensions/features.\n";
            return false;
        }

        std::cout << " Selected device: " << this->physicalDeviceProperties.deviceName << std::endl;
        std::cout << " Required device extensions/features are available.\n";

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);


        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
        bool hasComputeQueueFamily = false;
        for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                std::cout << " Queue family " << i << " supports compute operations\n";
                if (!hasComputeQueueFamily) {
                    computeQueueFamilyIndex = i;
                    hasComputeQueueFamily = true;
                }
            }
        }

        if (!hasComputeQueueFamily) {
            std::cerr << "Failed to find a queue family that supports compute operations.\n";
            return false;
        }


        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> queueFamilyIndicies = { computeQueueFamilyIndex };
        for (uint32_t queueFamilyIndex : queueFamilyIndicies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
            queueCreateInfo.queueCount = 1;
            float queuePriority = 1.0f;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.emplace_back(queueCreateInfo);
        }

        atomic64Feature.pNext = &scalarFeatures;
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.features = {};
        deviceFeatures2.features.shaderInt64 = VK_TRUE;
        deviceFeatures2.pNext = &atomic64Feature;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceInfo.queueCreateInfoCount = queueCreateInfos.size();

        deviceInfo.pEnabledFeatures = nullptr;
        deviceInfo.pNext = &deviceFeatures2;
        deviceInfo.enabledExtensionCount = this->deviceExtensions.size();
        deviceInfo.ppEnabledExtensionNames = this->deviceExtensions.data();

        if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &this->device) != VK_SUCCESS) {
            std::cerr << "VkDevice Creation Error\n";
            return false;
        }

        return true;
    }



    void createComputeBackend() {
        Pathtracer::SPIRV::VulkanContext vkCtx = { this->physicalDevice, this->device, this->commandPool, this->computeQueue, this->descriptorPool };
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
        if (this->pathtracerConfig.GetComputeBackendType() != Pathtracer::ComputeBackendType::SPIRV_T)
            return;

        std::vector<VkDescriptorPoolSize> poolSizes;

        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, PING_PONG_FRAMES });
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * PING_PONG_FRAMES });
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Pathtracer::SPIRV::SSBOsCount * PING_PONG_FRAMES });

        const uint32_t maxSets = PING_PONG_FRAMES;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = maxSets;

        vkCreateDescriptorPool(this->device, &poolInfo, nullptr, &this->descriptorPool);
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

        vkQueueSubmit(this->computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(this->computeQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    Pathtracer::ComputeBackend::DispatchConext createDispatchContext(VkCommandBuffer commandBuffer, int currentFrame) const {
        /*
        A timestamp query measures GPU time between two points in the command buffer
        It does not measure "this dispatch" unless you bracket it
        */

        return {
            .commandBuffer = commandBuffer,
            .currentFrame = (uint32_t)currentFrame,
            .textureIndex = (uint32_t)textureIndex,
            .tileSize = this->pathtracerConfig.GetTileSize(),
            .lightBounces = this->pathtracerConfig.GetLightBounces()
        };
    }

    void dispatchComputeBeforeQueueSubmit(VkCommandBuffer commandBuffer, int currentFrame) {
        this->computeBackend->dispatchBeforeQueueSubmit(createDispatchContext(commandBuffer, currentFrame));
    }

    void dispatchComputeAfterQueueSubmit(int currentFrame) {
        this->computeBackend->dispatchAfterQueueSubmit(createDispatchContext(VK_NULL_HANDLE, currentFrame));
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = computeQueueFamilyIndex;

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
        std::string sceneFilepath;
        if (this->pathtracerConfig.HasCustomScene()) {
            const std::filesystem::path inputPath(this->pathtracerConfig.GetCustomScenePath());
            const std::string baseName = inputPath.stem().string();
            sceneFilepath = RESOURCE("scenes\\") + baseName;
        } else {
            sceneFilepath = RESOURCE("scenes\\") + this->pathtracerConfig.GetScene();
        }
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
        std::vector<BVH::Node> bvhNodes;
        std::vector<BVH4::Node> bvh4Nodes;
        std::vector<KdTree::Node> kdNodes;

        auto measureBuildTime = [&](auto&& buildFn) {
            auto t0 = std::chrono::high_resolution_clock::now();
            buildFn();
            auto t1 = std::chrono::high_resolution_clock::now();
            pathtracerStatistics.accStructBuildTime = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() / 1000.0f;
        };

        auto reorderTriangles = [&](const auto& reorderedTrianglesRef) {
            std::vector<OBJLoader::Triangle> reordered(triangles.size());
            for (size_t i = 0; i < triangles.size(); i++) reordered[i] = triangles[reorderedTrianglesRef[i].oIdx];
            triangles = std::move(reordered);
        };

        switch (this->pathtracerConfig.GetAccelerationStructureType()) {
        case Pathtracer::AccelerationStructureType::BVH4: {
            BVH4 bvh4(mergedMesh);
            measureBuildTime([&]() { bvh4.Build(); });
            reorderTriangles(bvh4.GetTriangles());
            bvh4Nodes = bvh4.GetTree();
            pathtracerStatistics.accStructMemoryUsage = static_cast<uint32_t>(bvh4Nodes.size() * sizeof(BVH4::Node));
            pathtracerStatistics.accStructAuxBytes = 0;
            pathtracerStatistics.accStructHeight = static_cast<uint32_t>(bvh4.GetHeight());
            printf("\nBVH4 Height: %d\n", bvh4.GetHeight());
            break;
        }
        case Pathtracer::AccelerationStructureType::BVH: {
            BVH bvh(mergedMesh);
            measureBuildTime([&]() { bvh.Build(); });
            reorderTriangles(bvh.GetTriangles());
            bvhNodes = bvh.GetTree();
            pathtracerStatistics.accStructMemoryUsage = static_cast<uint32_t>(bvhNodes.size() * sizeof(BVH::Node));
            pathtracerStatistics.accStructAuxBytes = 0;
            pathtracerStatistics.accStructHeight = static_cast<uint32_t>(bvh.GetHeight());
            printf("\BVH Height: %d\n", bvh.GetHeight());
            break;
        }
        case Pathtracer::AccelerationStructureType::KD_TREE:
        default: {
            KdTree kdh(mergedMesh);
            measureBuildTime([&]() { kdh.Build(); });
            kdNodes = kdh.GetTree();
            pathtracerStatistics.accStructMemoryUsage = static_cast<uint32_t>(kdNodes.size() * sizeof(KdTree::Node));
            indices = kdh.GetIndices();
            pathtracerStatistics.accStructAuxBytes = static_cast<uint32_t>(indices.size() * sizeof(indices[0]));
            pathtracerStatistics.accStructHeight = static_cast<uint32_t>(kdh.GetHeight());
            printf("\nKdTree Height: %d\n", kdh.GetHeight());
            break;
        }
        }

        Pathtracer::ComputeBackend::SceneData sceneData = {
            .accelerationStructureType = this->pathtracerConfig.GetAccelerationStructureType(),
            .vertices = objVertices,
            .triangles = triangles,
            .lightTriangles = lightTris,
            .bvhNodes = bvhNodes,
            .bvh4Nodes = bvh4Nodes,
            .kdNodes = kdNodes,
            .indices_kdtree = indices,
            .materials = materials
        };

        this->computeBackend->init(sceneData);
    }

    void createFrameResources() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = this->commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        if (vkAllocateCommandBuffers(this->device, &allocInfo, this->commandBuffers) != VK_SUCCESS) {
            std::cerr << "Failed to allocate compute command buffers\n";
            return;
        }
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateFence(this->device, &fenceInfo, nullptr, &this->inFlightFences[i]) != VK_SUCCESS ||
                vkCreateSemaphore(this->device, &semaphoreInfo, nullptr, &this->backendWaitSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(this->device, &semaphoreInfo, nullptr, &this->backendSignalSemaphores[i]) != VK_SUCCESS) {
                std::cerr << "Failed to create compute synchronization resources\n";
                return;
            }
        }
    }


public:
    void shutdown() {
        initSucceeded = false;

        if (device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device);

        computeBackend.reset();
        
        if (device != VK_NULL_HANDLE) {
            // Fence resources
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                if (inFlightFences[i] != VK_NULL_HANDLE)
                    vkDestroyFence(device, inFlightFences[i], nullptr);
                if (backendWaitSemaphores[i] != VK_NULL_HANDLE)
                    vkDestroySemaphore(device, backendWaitSemaphores[i], nullptr);
                if (backendSignalSemaphores[i] != VK_NULL_HANDLE)
                    vkDestroySemaphore(device, backendSignalSemaphores[i], nullptr);
                inFlightFences[i] = VK_NULL_HANDLE;
                backendWaitSemaphores[i] = VK_NULL_HANDLE;
                backendSignalSemaphores[i] = VK_NULL_HANDLE;
            }

            // Descriptor resources
            if (descriptorPool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;

            // Command pool (implicitly frees command buffers)
            if (commandPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;

            // Device
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

#ifdef DEBUG
        if (instance != VK_NULL_HANDLE && debugMessenger != VK_NULL_HANDLE) {
            auto destroyFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            if (destroyFunc != nullptr)
                destroyFunc(instance, debugMessenger, nullptr);
            debugMessenger = VK_NULL_HANDLE;
        }
#endif

        // Instance resources
        if (instance != VK_NULL_HANDLE)
            vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
        physicalDevice = VK_NULL_HANDLE;
    }



    void run(uint32_t& currentFrame) {
        if (!initSucceeded) return;

        if (vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            std::cerr << "Failed waiting for compute fence\n";
            return;
        }

        if (pathtracerState.iFrame >= MAX_FRAMES_IN_FLIGHT)
            pathtracerStatistics.avgKernelTime += computeBackend->queryDispatchTime(currentFrame, physicalDeviceProperties.limits.timestampPeriod);

        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
            std::cerr << "Failed to begin compute command buffer\n";
            return;
        }

        computeBackend->updateFrameContext(&pathtracerState, sizeof(pathtracerState));
        dispatchComputeBeforeQueueSubmit(commandBuffers[currentFrame], currentFrame);

        if (vkEndCommandBuffer(commandBuffers[currentFrame]) != VK_SUCCESS) {
            std::cerr << "Failed to end compute command buffer\n";
            return;
        }

        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        Pathtracer::ComputeBackend::SyncContext syncCtx = {
            waitSemaphores, waitStages, signalSemaphores, currentFrame
        };
        computeBackend->sync(syncCtx);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.data();
        if (vkQueueSubmit(computeQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
            std::cerr << "Failed to submit compute command buffer\n";
            return;
        }
        dispatchComputeAfterQueueSubmit(currentFrame);

        textureIndex ^= 1;
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        pathtracerState.iFrame++;
    }


    void wait() const {
        vkQueueWaitIdle(this->computeQueue);
    }

    std::string GetGPUName() const {
        return std::string(this->physicalDeviceProperties.deviceName);
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
            std::string filepath = RESOURCE("outputs\\") + this->pathtracerConfig.GetScene() + "\\ref.exr";
            std::vector<glm::vec4> groundTruth;
            uint32_t w, h;
            EXR::Load(filepath, groundTruth, w, h);
            double smse = 0.0;
            double peak = 0.0;
            for (size_t i = 0; i < pixels.size(); i++) {
                smse += mse(pixels[i], groundTruth[i]);

                peak = std::max(peak, (double)groundTruth[i].r);
                peak = std::max(peak, (double)groundTruth[i].g);
                peak = std::max(peak, (double)groundTruth[i].b);
            }
            smse /= pixels.size();
            stats.rmse = sqrt(smse);
            if (smse > 0.0) stats.psnr = 10.0 * log10((peak * peak) / smse);
            else stats.psnr = std::numeric_limits<double>::infinity();
        }
        
        if (this->pathtracerConfig.ShouldSaveImage()) {
            std::string filepath = RESOURCE("outputs\\") + (binfo.btype != Pathtracer::IMGREF ? "output" : this->pathtracerConfig.GetScene() + "\\output" + this->pathtracerConfig.InlineString());
            EXR::Save(filepath + ".exr", pixels, this->WIDTH, this->HEIGHT);
            PPM::Save(filepath + ".ppm", pixels, this->WIDTH, this->HEIGHT, false);
            //if(binfo.btype != Pathtracer::IMGREF) PPM::Save(filepath + "-g.ppm", pixels, this->WIDTH, this->HEIGHT, true);
        }

        return stats;
    }

};

#endif
