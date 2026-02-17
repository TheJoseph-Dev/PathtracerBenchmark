#ifndef VULKAN_BUFFER_H
#define VULKAN_BUFFER_H
#include <vulkan/vulkan.h>

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
    Buffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size, const void* initialData, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propertyFlags) {
        this->device = device;
        this->size = size;

        createBuffer(size, usageFlags, propertyFlags, this->buffer, this->memory, physicalDevice);

        // Upload initial data
        if (initialData) {
            void* data;
            vkMapMemory(device, this->memory, 0, size, 0, &data);
            memcpy(data, initialData, (size_t)size);
            vkUnmapMemory(device, this->memory);
        }
    };

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

private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkPhysicalDevice physicalDevice) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                return i;

        throw std::runtime_error("failed to find suitable memory type!");
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory, VkPhysicalDevice physicalDevice) {
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
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties, physicalDevice);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        vkBindBufferMemory(this->device, buffer, bufferMemory, 0);
    }
};

#endif