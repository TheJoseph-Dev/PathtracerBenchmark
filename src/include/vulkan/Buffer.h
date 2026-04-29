#ifndef VULKAN_BUFFER_H
#define VULKAN_BUFFER_H
#include <vulkan/vulkan.h>
#include "Vulkan.h"

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

        Vulkan::createBuffer(device, physicalDevice, size, usageFlags, propertyFlags, this->buffer, this->memory);

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

};

#endif