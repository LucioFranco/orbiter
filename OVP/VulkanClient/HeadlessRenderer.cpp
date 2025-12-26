// ==============================================================
// HeadlessRenderer.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "HeadlessRenderer.h"
#include <cstring>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

// RenderDoc API definitions
typedef enum RENDERDOC_CaptureOption {
    eRENDERDOC_Option_AllowVSync = 0,
} RENDERDOC_CaptureOption;

struct RENDERDOC_API_1_6_0 {
    void (*GetAPIVersion)(int* major, int* minor, int* patch);
    int (*SetCaptureOptionU32)(RENDERDOC_CaptureOption opt, uint32_t val);
    int (*SetCaptureOptionF32)(RENDERDOC_CaptureOption opt, float val);
    uint32_t (*GetCaptureOptionU32)(RENDERDOC_CaptureOption opt);
    float (*GetCaptureOptionF32)(RENDERDOC_CaptureOption opt);
    void (*SetFocusToggleKeys)(void* keys, int num);
    void (*SetCaptureKeys)(void* keys, int num);
    uint32_t (*GetOverlayBits)();
    void (*MaskOverlayBits)(uint32_t And, uint32_t Or);
    void (*RemoveHooks)();
    void (*UnloadCrashHandler)();
    void (*SetCaptureFilePathTemplate)(const char* pathtemplate);
    const char* (*GetCaptureFilePathTemplate)();
    uint32_t (*GetNumCaptures)();
    uint32_t (*GetCapture)(uint32_t idx, char* filename, uint32_t* pathlength, uint64_t* timestamp);
    void (*TriggerCapture)();
    uint32_t (*IsTargetControlConnected)();
    uint32_t (*LaunchReplayUI)(uint32_t connectTargetControl, const char* cmdline);
    void (*SetActiveWindow)(void* device, void* wndHandle);
    void (*StartFrameCapture)(void* device, void* wndHandle);
    uint32_t (*IsFrameCapturing)();
    void (*EndFrameCapture)(void* device, void* wndHandle);
    uint32_t (*TriggerMultiFrameCapture)(uint32_t numFrames);
    void (*SetCaptureFileComments)(const char* filePath, const char* comments);
    uint32_t (*DiscardFrameCapture)(void* device, void* wndHandle);
    void (*ShowReplayUI)();
    void (*SetCaptureTitle)(const char* title);
};

typedef int (*pRENDERDOC_GetAPI)(uint32_t version, void** outAPIPointers);
#define RENDERDOC_API_VERSION_1_6_0 10600

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
#endif

HeadlessRenderer::HeadlessRenderer()
    : m_initialized(false)
    , m_width(0)
    , m_height(0)
    , m_instance(VK_NULL_HANDLE)
    , m_physicalDevice(VK_NULL_HANDLE)
    , m_device(VK_NULL_HANDLE)
    , m_queue(VK_NULL_HANDLE)
    , m_queueFamilyIndex(0)
    , m_commandPool(VK_NULL_HANDLE)
    , m_commandBuffer(VK_NULL_HANDLE)
    , m_colorImage(VK_NULL_HANDLE)
    , m_colorMemory(VK_NULL_HANDLE)
    , m_colorImageView(VK_NULL_HANDLE)
    , m_colorFormat(VK_FORMAT_R8G8B8A8_UNORM)
    , m_renderPass(VK_NULL_HANDLE)
    , m_framebuffer(VK_NULL_HANDLE)
    , m_stagingBuffer(VK_NULL_HANDLE)
    , m_stagingMemory(VK_NULL_HANDLE)
    , m_stagingSize(0)
    , m_renderDocModule(nullptr)
    , m_renderDocApi(nullptr)
#ifdef _DEBUG
    , m_debugMessenger(VK_NULL_HANDLE)
    , m_enableValidation(true)
#endif
{
    m_clearColor[0] = 0.0f;
    m_clearColor[1] = 0.0f;
    m_clearColor[2] = 0.0f;
    m_clearColor[3] = 1.0f;
}

HeadlessRenderer::~HeadlessRenderer()
{
    Shutdown();
}

bool HeadlessRenderer::Init(uint32_t width, uint32_t height)
{
    if (m_initialized) {
        Shutdown();
    }

    m_width = width;
    m_height = height;

    std::cout << "[HeadlessRenderer] Initializing " << width << "x" << height << std::endl;

    // Load RenderDoc first (before Vulkan instance)
    LoadRenderDoc();

    if (!CreateInstance()) {
        std::cerr << "[HeadlessRenderer] Failed to create Vulkan instance" << std::endl;
        return false;
    }
    std::cout << "[API] Instance created: OK" << std::endl;

    if (!PickPhysicalDevice()) {
        std::cerr << "[HeadlessRenderer] Failed to find suitable GPU" << std::endl;
        return false;
    }
    std::cout << "[API] Physical device found: OK (" << GetGPUName() << ")" << std::endl;

    if (!CreateLogicalDevice()) {
        std::cerr << "[HeadlessRenderer] Failed to create logical device" << std::endl;
        return false;
    }
    std::cout << "[API] Logical device created: OK" << std::endl;

    if (!CreateCommandPool()) {
        std::cerr << "[HeadlessRenderer] Failed to create command pool" << std::endl;
        return false;
    }
    std::cout << "[API] Command pool created: OK" << std::endl;

    if (!CreateColorImage()) {
        std::cerr << "[HeadlessRenderer] Failed to create color image" << std::endl;
        return false;
    }
    std::cout << "[API] Color image created: OK" << std::endl;

    if (!CreateStagingBuffer()) {
        std::cerr << "[HeadlessRenderer] Failed to create staging buffer" << std::endl;
        return false;
    }
    std::cout << "[API] Staging buffer created: OK" << std::endl;

    if (!CreateRenderPass()) {
        std::cerr << "[HeadlessRenderer] Failed to create render pass" << std::endl;
        return false;
    }
    std::cout << "[API] Render pass created: OK" << std::endl;

    if (!CreateFramebuffer()) {
        std::cerr << "[HeadlessRenderer] Failed to create framebuffer" << std::endl;
        return false;
    }
    std::cout << "[API] Framebuffer created: OK" << std::endl;

    if (!AllocateCommandBuffer()) {
        std::cerr << "[HeadlessRenderer] Failed to allocate command buffer" << std::endl;
        return false;
    }
    std::cout << "[API] Command buffer allocated: OK" << std::endl;

    m_initialized = true;
    std::cout << "[HeadlessRenderer] Initialization complete" << std::endl;

    return true;
}

void HeadlessRenderer::Shutdown()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    DestroyFramebuffer();
    DestroyRenderPass();
    DestroyStagingBuffer();
    DestroyColorImage();
    DestroyCommandPool();
    DestroyLogicalDevice();
    DestroyInstance();

    m_renderDocApi = nullptr;
    m_renderDocModule = nullptr;

    m_initialized = false;
    m_width = 0;
    m_height = 0;
}

bool HeadlessRenderer::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "HeadlessRenderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Orbiter VulkanClient";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

#ifdef _DEBUG
    if (m_enableValidation) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;

        const char* extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = extensions;
    }
#endif

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    return result == VK_SUCCESS;
}

bool HeadlessRenderer::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Find a device with graphics queue
    for (const auto& device : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_physicalDevice = device;
                m_queueFamilyIndex = i;
                return true;
            }
        }
    }

    return false;
}

bool HeadlessRenderer::CreateLogicalDevice()
{
    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;

#ifdef _DEBUG
    if (m_enableValidation) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = validationLayers;
    }
#endif

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        return false;
    }

    vkGetDeviceQueue(m_device, m_queueFamilyIndex, 0, &m_queue);
    return true;
}

bool HeadlessRenderer::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;

    return vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) == VK_SUCCESS;
}

bool HeadlessRenderer::CreateColorImage()
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_colorFormat;
    imageInfo.extent = { m_width, m_height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_colorImage) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, m_colorImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_colorMemory) != VK_SUCCESS) {
        return false;
    }

    vkBindImageMemory(m_device, m_colorImage, m_colorMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_colorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_colorFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    return vkCreateImageView(m_device, &viewInfo, nullptr, &m_colorImageView) == VK_SUCCESS;
}

bool HeadlessRenderer::CreateStagingBuffer()
{
    m_stagingSize = m_width * m_height * 4; // RGBA

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, m_stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) {
        return false;
    }

    return vkBindBufferMemory(m_device, m_stagingBuffer, m_stagingMemory, 0) == VK_SUCCESS;
}

bool HeadlessRenderer::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    return vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) == VK_SUCCESS;
}

bool HeadlessRenderer::CreateFramebuffer()
{
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &m_colorImageView;
    fbInfo.width = m_width;
    fbInfo.height = m_height;
    fbInfo.layers = 1;

    return vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffer) == VK_SUCCESS;
}

bool HeadlessRenderer::AllocateCommandBuffer()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    return vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) == VK_SUCCESS;
}

uint32_t HeadlessRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
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

void HeadlessRenderer::BeginFrame()
{
    vkResetCommandBuffer(m_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
}

void HeadlessRenderer::Clear(float r, float g, float b, float a)
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}

void HeadlessRenderer::EndFrame()
{
    VkClearValue clearValue{};
    clearValue.color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_renderPass;
    rpBegin.framebuffer = m_framebuffer;
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = { m_width, m_height };
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(m_commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(m_commandBuffer);

    vkEndCommandBuffer(m_commandBuffer);
}

void HeadlessRenderer::Submit()
{
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
}

std::vector<uint8_t> HeadlessRenderer::ReadPixels()
{
    std::vector<uint8_t> pixels(m_width * m_height * 4);

    // Record copy command
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResetCommandBuffer(m_commandBuffer, 0);
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { m_width, m_height, 1 };

    vkCmdCopyImageToBuffer(m_commandBuffer, m_colorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_stagingBuffer, 1, &region);

    vkEndCommandBuffer(m_commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);

    // Map and copy
    void* data;
    vkMapMemory(m_device, m_stagingMemory, 0, m_stagingSize, 0, &data);
    memcpy(pixels.data(), data, pixels.size());
    vkUnmapMemory(m_device, m_stagingMemory);

    return pixels;
}

bool HeadlessRenderer::LoadRenderDoc()
{
#ifdef _WIN32
    HMODULE module = GetModuleHandleA("renderdoc.dll");

    if (!module) {
        module = LoadLibraryA("renderdoc.dll");
    }

    if (!module) {
        module = LoadLibraryA("C:\\Program Files\\RenderDoc\\renderdoc.dll");
    }

    if (module) {
        pRENDERDOC_GetAPI getApi = (pRENDERDOC_GetAPI)GetProcAddress(module, "RENDERDOC_GetAPI");
        if (getApi) {
            int ret = getApi(RENDERDOC_API_VERSION_1_6_0, (void**)&m_renderDocApi);
            if (ret == 1 && m_renderDocApi) {
                m_renderDocModule = module;
                int major, minor, patch;
                m_renderDocApi->GetAPIVersion(&major, &minor, &patch);
                std::cout << "[RenderDoc] Available: YES (v" << major << "." << minor << "." << patch << ")" << std::endl;
                return true;
            }
        }
    }
#endif

    std::cout << "[RenderDoc] Available: NO" << std::endl;
    return false;
}

void HeadlessRenderer::StartCapture()
{
    if (m_renderDocApi) {
        m_renderDocApi->StartFrameCapture(nullptr, nullptr);
    }
}

void HeadlessRenderer::EndCapture()
{
    if (m_renderDocApi) {
        m_renderDocApi->EndFrameCapture(nullptr, nullptr);
    }
}

uint32_t HeadlessRenderer::GetCaptureCount() const
{
    if (m_renderDocApi) {
        return m_renderDocApi->GetNumCaptures();
    }
    return 0;
}

std::string HeadlessRenderer::GetGPUName() const
{
    if (m_physicalDevice == VK_NULL_HANDLE) {
        return "Unknown";
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    return props.deviceName;
}

void HeadlessRenderer::PrintDiagnostics()
{
    std::cout << "\n=== HEADLESS RENDERER DIAGNOSTICS ===" << std::endl;
    std::cout << "GPU: " << GetGPUName() << std::endl;
    std::cout << "Resolution: " << m_width << "x" << m_height << std::endl;
    std::cout << "Format: VK_FORMAT_R8G8B8A8_UNORM" << std::endl;
    std::cout << "RenderDoc: " << (IsRenderDocAvailable() ? "Available" : "Not available") << std::endl;
    if (IsRenderDocAvailable()) {
        std::cout << "Captures: " << GetCaptureCount() << std::endl;
    }
    std::cout << "======================================\n" << std::endl;
}

// Cleanup functions
void HeadlessRenderer::DestroyFramebuffer()
{
    if (m_framebuffer && m_device) {
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyRenderPass()
{
    if (m_renderPass && m_device) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyStagingBuffer()
{
    if (m_stagingBuffer && m_device) {
        vkDestroyBuffer(m_device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingMemory && m_device) {
        vkFreeMemory(m_device, m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyColorImage()
{
    if (m_colorImageView && m_device) {
        vkDestroyImageView(m_device, m_colorImageView, nullptr);
        m_colorImageView = VK_NULL_HANDLE;
    }
    if (m_colorImage && m_device) {
        vkDestroyImage(m_device, m_colorImage, nullptr);
        m_colorImage = VK_NULL_HANDLE;
    }
    if (m_colorMemory && m_device) {
        vkFreeMemory(m_device, m_colorMemory, nullptr);
        m_colorMemory = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyCommandPool()
{
    if (m_commandPool && m_device) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyLogicalDevice()
{
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyInstance()
{
#ifdef _DEBUG
    if (m_debugMessenger && m_instance) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}
