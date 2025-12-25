// ==============================================================
// VulkanClient.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#define OAPI_IMPLEMENTATION

#include "VulkanClient.h"
#include "OrbiterAPI.h"
#include <vulkan/vulkan_win32.h>
#include <cstring>

// Validation layers for debug builds
static const std::vector<const char*> validationLayers = {
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
        oapiWriteLog(const_cast<char*>(pCallbackData->pMessage));
    }
    return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}
#endif

// ======================================================================
// Module interface
// ======================================================================

static VulkanClient* g_client = nullptr;

DLLCLBK void InitModule(HINSTANCE hDLL)
{
    g_client = new VulkanClient(hDLL);
    if (!oapiRegisterGraphicsClient(g_client)) {
        delete g_client;
        g_client = nullptr;
    }
}

DLLCLBK void ExitModule(HINSTANCE hDLL)
{
    if (g_client) {
        oapiUnregisterGraphicsClient(g_client);
        delete g_client;
        g_client = nullptr;
    }
}

// ======================================================================
// class VulkanClient
// ======================================================================

VulkanClient::VulkanClient(HINSTANCE hInstance)
    : GraphicsClient(hInstance)
    , m_instance(VK_NULL_HANDLE)
#ifdef _DEBUG
    , m_debugMessenger(VK_NULL_HANDLE)
    , m_enableValidationLayers(true)
#else
    , m_enableValidationLayers(false)
#endif
{
}

VulkanClient::~VulkanClient()
{
    DestroyInstance();
}

bool VulkanClient::clbkInitialise()
{
    // Call base class initialization
    if (!GraphicsClient::clbkInitialise()) {
        return false;
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Initializing..."));

    if (!CreateInstance()) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to create Vulkan instance"));
        return false;
    }

#ifdef _DEBUG
    if (m_enableValidationLayers) {
        if (!SetupDebugMessenger()) {
            oapiWriteLog(const_cast<char*>("VulkanClient: Warning - Failed to set up debug messenger"));
        }
    }
#endif

    oapiWriteLog(const_cast<char*>("VulkanClient: Vulkan instance created successfully"));
    return true;
}

bool VulkanClient::CreateInstance()
{
    if (m_enableValidationLayers && !CheckValidationLayerSupport()) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Validation layers requested but not available"));
        m_enableValidationLayers = false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Orbiter Space Flight Simulator";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Orbiter VulkanClient";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef _DEBUG
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
#else
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
#endif

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        char buf[256];
        sprintf_s(buf, "VulkanClient: vkCreateInstance failed with error %d", result);
        oapiWriteLog(buf);
        return false;
    }

    return true;
}

void VulkanClient::DestroyInstance()
{
#ifdef _DEBUG
    DestroyDebugMessenger();
#endif

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

bool VulkanClient::CheckValidationLayerSupport()
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

std::vector<const char*> VulkanClient::GetRequiredExtensions()
{
    std::vector<const char*> extensions;

    // Required for windowed rendering on Windows
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

#ifdef _DEBUG
    if (m_enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
#endif

    return extensions;
}

#ifdef _DEBUG
bool VulkanClient::SetupDebugMessenger()
{
    if (!m_enableValidationLayers) return true;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    return CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) == VK_SUCCESS;
}

void VulkanClient::DestroyDebugMessenger()
{
    if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
}
#endif

void VulkanClient::clbkGetViewportSize(DWORD *width, DWORD *height) const
{
    // TODO: Return actual viewport size when rendering is implemented
    *width = 1920;
    *height = 1080;
}

bool VulkanClient::clbkGetRenderParam(DWORD param, DWORD *value) const
{
    // TODO: Implement render parameter queries
    *value = 0;
    return false;
}
