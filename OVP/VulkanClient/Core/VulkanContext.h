// ==============================================================
// VulkanContext.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Shared Vulkan context: instance, device, queues, command pool.
// Used by both HeadlessRenderer and VulkanClient.
// ==============================================================

#ifndef VULKANCONTEXT_H
#define VULKANCONTEXT_H

#include <vulkan/vulkan.h>
#include "VulkanMemory.h"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

struct VulkanContextCreateInfo {
    const char* appName = "Orbiter";
    uint32_t appVersion = VK_MAKE_VERSION(1, 0, 0);
    bool enableValidation = true;
    bool enableSurface = false;  // True for windowed mode (adds surface extensions)
    VkSurfaceKHR surface = VK_NULL_HANDLE;  // Optional pre-created surface
    std::function<VkSurfaceKHR(VkInstance)> surfaceFactory;  // Optional factory to create a surface after instance creation
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // Lifecycle
    bool Init(const VulkanContextCreateInfo& info);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Accessors
    VkInstance GetInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue GetPresentQueue() const { return m_presentQueue; }
    uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    uint32_t GetPresentQueueFamily() const { return m_presentQueueFamily; }
    VkCommandPool GetCommandPool() const { return m_commandPool; }
    VmaAllocator GetAllocator() const { return m_allocator; }
    bool HasSynchronization2() const { return m_hasSynchronization2; }

    // Utilities
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void WaitIdle() const;

    // Diagnostics
    std::string GetGPUName() const;

private:
    // Initialization helpers
    bool CreateInstance(const VulkanContextCreateInfo& info);
    bool PickPhysicalDevice(VkSurfaceKHR surface);
    bool CreateLogicalDevice(VkSurfaceKHR surface);
    bool CreateCommandPool();
    bool CreateAllocator();

    // Cleanup helpers
    void DestroyAllocator();
    void DestroyCommandPool();
    void DestroyLogicalDevice();
    void DestroyInstance();

    // State
    bool m_initialized;

    // Vulkan core
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    uint32_t m_graphicsQueueFamily;
    uint32_t m_presentQueueFamily;
    VkCommandPool m_commandPool;
    VmaAllocator m_allocator;

    // Configuration
    bool m_enableValidation;
    bool m_surfaceEnabled;
    bool m_hasSynchronization2;

#ifdef _DEBUG
    VkDebugUtilsMessengerEXT m_debugMessenger;
#endif
};

#endif // VULKANCONTEXT_H
