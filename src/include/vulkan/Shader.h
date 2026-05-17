#ifndef VULKAN_SHADER_H
#define VULKAN_SHADER_H

#include <vulkan/vulkan.h>
#include <fstream>
#include <iostream>
#include "include/Helper.h"

class Shader {
public:
    enum Type {
        VERTEX = 0,
        FRAGMENT = 1,
        COMPUTE = 2
    };


    Shader(const std::string& path, Type type, VkDevice device) {
        this->device = device;
        this->type = type;
        this->shaderModule = createShaderModule(path).value();
    }

    ~Shader() {
        if (this->shaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, this->shaderModule, nullptr);
    }

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept : device(other.device), shaderModule(other.shaderModule), type(other.type) {
        other.shaderModule = VK_NULL_HANDLE;
    }

    Shader& operator=(Shader&& other) noexcept {
        if (this != &other) {
            vkDestroyShaderModule(device, shaderModule, nullptr);

            device = other.device;
            shaderModule = other.shaderModule;
            type = other.type;

            other.shaderModule = VK_NULL_HANDLE;
        }
        return *this;
    }

private:

    std::optional<VkShaderModule> createShaderModule(const std::string& path) const {
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


public:

    inline VkShaderStageFlagBits GetStage() const {
        switch (this->type) {
        case VERTEX:   return VK_SHADER_STAGE_VERTEX_BIT;
        case FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case COMPUTE:  return VK_SHADER_STAGE_COMPUTE_BIT;
        default: assert(false);
        }
        return VK_SHADER_STAGE_ALL;
    }

    inline VkShaderModule GetShaderModule() const { return this->shaderModule; }

    inline VkPipelineShaderStageCreateInfo GetShaderCreateInfo() const {
        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = this->GetStage();
        shaderStageInfo.module = this->shaderModule;
        shaderStageInfo.pName = "main";
        return shaderStageInfo;
    }

private:

    VkDevice device;
    VkShaderModule shaderModule;
    Type type;
};

#endif