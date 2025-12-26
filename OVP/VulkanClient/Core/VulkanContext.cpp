// ==============================================================
// VulkanContext.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// VMA implementation - must be defined in exactly one .cpp file
#define VMA_IMPLEMENTATION
#include "VulkanContext.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif

// Validation layers
static const char* validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[Vulkan] " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

static bool CheckValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            return false;
        }
    }
    return true;
}
#endif

VulkanContext::VulkanContext()
    : m_initialized(false)
    , m_instance(VK_NULL_HANDLE)
    , m_physicalDevice(VK_NULL_HANDLE)
    , m_device(VK_NULL_HANDLE)
    , m_graphicsQueue(VK_NULL_HANDLE)
    , m_presentQueue(VK_NULL_HANDLE)
    , m_graphicsQueueFamily(0)
    , m_presentQueueFamily(0)
    , m_commandPool(VK_NULL_HANDLE)
    , m_allocator(VK_NULL_HANDLE)
    , m_enableValidation(false)
    , m_surfaceEnabled(false)
    , m_hasSynchronization2(false)
#ifdef _DEBUG
    , m_debugMessenger(VK_NULL_HANDLE)
#endif
{
}

VulkanContext::~VulkanContext()
{
    Shutdown();
}

bool VulkanContext::Init(const VulkanContextCreateInfo& info)
{
    if (m_initialized) {
        Shutdown();
    }

    m_enableValidation = info.enableValidation;
    m_surfaceEnabled = info.enableSurface;

    if (!CreateInstance(info)) {
        std::cerr << "[VulkanContext] Failed to create Vulkan instance" << std::endl;
        return false;
    }
    std::cout << "[VulkanContext] Instance created: OK" << std::endl;

    VkSurfaceKHR surface = info.surface;

    // If surface is required but not provided, try to create via factory
    if (m_surfaceEnabled && surface == VK_NULL_HANDLE && info.surfaceFactory) {
        surface = info.surfaceFactory(m_instance);
        if (surface == VK_NULL_HANDLE) {
            std::cerr << "[VulkanContext] Surface factory failed" << std::endl;
            DestroyInstance();
            return false;
        }
    }

    if (!PickPhysicalDevice(surface)) {
        std::cerr << "[VulkanContext] Failed to find suitable GPU" << std::endl;
        return false;
    }
    std::cout << "[VulkanContext] Physical device: " << GetGPUName() << std::endl;

    if (!CreateLogicalDevice(surface)) {
        std::cerr << "[VulkanContext] Failed to create logical device" << std::endl;
        return false;
    }
    std::cout << "[VulkanContext] Logical device created: OK" << std::endl;

    if (!CreateCommandPool()) {
        std::cerr << "[VulkanContext] Failed to create command pool" << std::endl;
        return false;
    }
    std::cout << "[VulkanContext] Command pool created: OK" << std::endl;

    if (!CreateAllocator()) {
        std::cerr << "[VulkanContext] Failed to create VMA allocator" << std::endl;
        return false;
    }
    std::cout << "[VulkanContext] VMA allocator created: OK" << std::endl;

    m_initialized = true;
    return true;
}

void VulkanContext::Shutdown()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    DestroyAllocator();
    DestroyCommandPool();
    DestroyLogicalDevice();
    DestroyInstance();

    m_initialized = false;
}

bool VulkanContext::CreateInstance(const VulkanContextCreateInfo& info)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = info.appName;
    appInfo.applicationVersion = info.appVersion;
    appInfo.pEngineName = "Orbiter VulkanClient";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Build extension list
    std::vector<const char*> extensions;

    if (m_surfaceEnabled) {
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
    }

#ifdef _DEBUG
    if (m_enableValidation) {
        if (!CheckValidationLayerSupport()) {
            std::cerr << "[VulkanContext] Validation layers not available" << std::endl;
            m_enableValidation = false;
        }
    }

    if (m_enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
    }
#endif

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        return false;
    }

#ifdef _DEBUG
    if (m_enableValidation) {
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = debugCallback;

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, &debugInfo, nullptr, &m_debugMessenger);
        }
    }
#endif

    return true;
}

bool VulkanContext::PickPhysicalDevice(VkSurfaceKHR surface)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Find a device with graphics queue (and present queue if surface provided)
    for (const auto& device : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        bool hasGraphics = false;
        bool hasPresent = (surface == VK_NULL_HANDLE);  // If no surface, don't require present
        uint32_t graphicsFamily = 0;
        uint32_t presentFamily = 0;

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                hasGraphics = true;
                graphicsFamily = i;
            }

            if (surface != VK_NULL_HANDLE) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
                if (presentSupport) {
                    hasPresent = true;
                    presentFamily = i;
                }
            }

            if (hasGraphics && hasPresent) {
                break;
            }
        }

        if (hasGraphics && hasPresent) {
            m_physicalDevice = device;
            m_graphicsQueueFamily = graphicsFamily;
            m_presentQueueFamily = (surface != VK_NULL_HANDLE) ? presentFamily : graphicsFamily;
            return true;
        }
    }

    return false;
}

// Helper: Check if a device extension is supported
static bool IsExtensionSupported(VkPhysicalDevice device, const char* extensionName)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, extensionName) == 0) {
            return true;
        }
    }
    return false;
}

bool VulkanContext::CreateLogicalDevice(VkSurfaceKHR surface)
{
    // Collect unique queue families
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::vector<uint32_t> uniqueFamilies = { m_graphicsQueueFamily };

    if (surface != VK_NULL_HANDLE && m_presentQueueFamily != m_graphicsQueueFamily) {
        uniqueFamilies.push_back(m_presentQueueFamily);
    }

    float queuePriority = 1.0f;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = family;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    // Device extensions
    std::vector<const char*> deviceExtensions;
    if (m_surfaceEnabled) {
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    // Check for VK_KHR_synchronization2 support (modern barrier APIs)
    m_hasSynchronization2 = IsExtensionSupported(m_physicalDevice, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    if (m_hasSynchronization2) {
        deviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }

    // Build feature chain for Vulkan 1.2+ features
    VkPhysicalDeviceSynchronization2Features sync2Features{};
    sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2Features.synchronization2 = m_hasSynchronization2 ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.features = deviceFeatures;
    if (m_hasSynchronization2) {
        deviceFeatures2.pNext = &sync2Features;
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pNext = &deviceFeatures2;  // Use pNext for features2 instead of pEnabledFeatures
    createInfo.pEnabledFeatures = nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();

#ifdef _DEBUG
    if (m_enableValidation) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
    }
#endif

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    if (surface != VK_NULL_HANDLE) {
        vkGetDeviceQueue(m_device, m_presentQueueFamily, 0, &m_presentQueue);
    } else {
        m_presentQueue = m_graphicsQueue;
    }

    if (m_hasSynchronization2) {
        std::cout << "[VulkanContext] Synchronization2 extension enabled: OK" << std::endl;
    }

    return true;
}

bool VulkanContext::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

    return vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) == VK_SUCCESS;
}

bool VulkanContext::CreateAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;

    return vmaCreateAllocator(&allocatorInfo, &m_allocator) == VK_SUCCESS;
}

uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return 0;
}

void VulkanContext::WaitIdle() const
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

std::string VulkanContext::GetGPUName() const
{
    if (m_physicalDevice == VK_NULL_HANDLE) {
        return "Unknown";
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    return props.deviceName;
}

void VulkanContext::DestroyAllocator()
{
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

void VulkanContext::DestroyCommandPool()
{
    if (m_commandPool != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

void VulkanContext::DestroyLogicalDevice()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    m_graphicsQueue = VK_NULL_HANDLE;
    m_presentQueue = VK_NULL_HANDLE;
}

void VulkanContext::DestroyInstance()
{
#ifdef _DEBUG
    if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}
