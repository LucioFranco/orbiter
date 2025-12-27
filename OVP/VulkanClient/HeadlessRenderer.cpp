// ==============================================================
// HeadlessRenderer.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "HeadlessRenderer.h"
#include "Core/RenderPassFactory.h"
#include <cstring>
#include <iostream>
#include <array>

// ImGui includes
// HeadlessRenderer uses its own copy of ImGui (not Orbiter's shared context)
// We need to define GImGui here because imconfig.h prevents imgui.cpp from defining it
struct ImGuiContext;
#ifdef EXPORT_IMGUI_CONTEXT
__declspec(dllexport) ImGuiContext* GImGui = nullptr;
#else
ImGuiContext* GImGui = nullptr;
#endif

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

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

HeadlessRenderer::HeadlessRenderer()
    : m_initialized(false)
    , m_width(0)
    , m_height(0)
    , m_commandBuffer(VK_NULL_HANDLE)
    , m_colorImage(VK_NULL_HANDLE)
    , m_colorAllocation(VK_NULL_HANDLE)
    , m_colorImageView(VK_NULL_HANDLE)
    , m_colorFormat(VK_FORMAT_B8G8R8A8_UNORM)
    , m_depthImage(VK_NULL_HANDLE)
    , m_depthAllocation(VK_NULL_HANDLE)
    , m_depthImageView(VK_NULL_HANDLE)
    , m_depthFormat(VK_FORMAT_D32_SFLOAT)
    , m_renderPass(VK_NULL_HANDLE)
    , m_framebuffer(VK_NULL_HANDLE)
    , m_stagingBuffer(VK_NULL_HANDLE)
    , m_stagingAllocation(VK_NULL_HANDLE)
    , m_stagingSize(0)
    , m_renderDocModule(nullptr)
    , m_renderDocApi(nullptr)
    , m_imguiDescriptorPool(VK_NULL_HANDLE)
    , m_imguiInitialized(false)
    , m_renderPassStarted(false)
    , m_sceneRendererInitialized(false)
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

    // Initialize shared Vulkan context (headless mode - no surface)
    VulkanContextCreateInfo ctxInfo{};
    ctxInfo.appName = "HeadlessRenderer";
    ctxInfo.enableValidation = true;
    ctxInfo.enableSurface = false;

    if (!m_ctx.Init(ctxInfo)) {
        std::cerr << "[HeadlessRenderer] Failed to initialize Vulkan context" << std::endl;
        return false;
    }

    // Create renderer-specific resources
    if (!CreateColorImage()) {
        std::cerr << "[HeadlessRenderer] Failed to create color image" << std::endl;
        return false;
    }
    std::cout << "[API] Color image created: OK" << std::endl;

    if (!CreateDepthImage()) {
        std::cerr << "[HeadlessRenderer] Failed to create depth image" << std::endl;
        return false;
    }
    std::cout << "[API] Depth image created: OK" << std::endl;

    if (!CreateStagingBuffer()) {
        std::cerr << "[HeadlessRenderer] Failed to create staging buffer" << std::endl;
        return false;
    }
    std::cout << "[API] Staging buffer created: OK" << std::endl;

    // Create render pass using shared factory (same config as window renderer)
    m_renderPass = RenderPassFactory::CreateMainRenderPass(
        m_ctx.GetDevice(),
        m_colorFormat,
        m_depthFormat,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL  // For readback
    );
    if (m_renderPass == VK_NULL_HANDLE) {
        std::cerr << "[HeadlessRenderer] Failed to create render pass" << std::endl;
        return false;
    }
    std::cout << "[API] Render pass created: OK (shared factory)" << std::endl;

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

    // Initialize StagingManager for device-local buffer uploads
    if (m_stagingManager.Init(&m_ctx)) {
        std::cout << "[API] StagingManager initialized: OK (device-local buffers enabled)" << std::endl;
    } else {
        std::cout << "[API] StagingManager initialization: SKIPPED (using host-visible fallback)" << std::endl;
    }

    m_initialized = true;

    // Initialize SceneRenderer (after render pass is created)
    // Pass staging manager if available for device-local buffers
    VkExtent2D extent = { m_width, m_height };
    StagingManager* staging = m_stagingManager.IsInitialized() ? &m_stagingManager : nullptr;
    if (m_sceneRenderer.Init(&m_ctx, m_renderPass, extent, staging)) {
        m_sceneRendererInitialized = true;
        std::cout << "[API] SceneRenderer initialized: OK";
        if (m_sceneRenderer.UsesDeviceLocalMemory()) {
            std::cout << " (device-local buffers)";
        }
        std::cout << std::endl;
    } else {
        std::cout << "[API] SceneRenderer initialization: SKIPPED (optional)" << std::endl;
    }

    std::cout << "[HeadlessRenderer] Initialization complete" << std::endl;

    return true;
}

void HeadlessRenderer::Shutdown()
{
    m_ctx.WaitIdle();

    // Shutdown SceneRenderer first (uses pipeline and buffers)
    if (m_sceneRendererInitialized) {
        m_sceneRenderer.Shutdown();
        m_sceneRendererInitialized = false;
    }

    // Shutdown ImGui (before destroying Vulkan resources)
    ShutdownImGui();

    // Shutdown staging manager
    m_stagingManager.Shutdown();

    DestroyFramebuffer();
    DestroyRenderPass();
    DestroyStagingBuffer();
    DestroyDepthImage();
    DestroyColorImage();

    // Command buffer is freed when command pool is destroyed
    m_commandBuffer = VK_NULL_HANDLE;

    // Shutdown shared context (destroys command pool, device, instance)
    m_ctx.Shutdown();

    m_renderDocApi = nullptr;
    m_renderDocModule = nullptr;

    m_initialized = false;
    m_width = 0;
    m_height = 0;
}

bool HeadlessRenderer::CreateColorImage()
{
    VkDevice device = m_ctx.GetDevice();
    VmaAllocator allocator = m_ctx.GetAllocator();

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

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(allocator, &imageInfo, &allocCreateInfo,
                        &m_colorImage, &m_colorAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

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

    return vkCreateImageView(device, &viewInfo, nullptr, &m_colorImageView) == VK_SUCCESS;
}

VkFormat HeadlessRenderer::ChooseDepthFormat()
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    VkPhysicalDevice physicalDevice = m_ctx.GetPhysicalDevice();

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    return VK_FORMAT_D32_SFLOAT;  // Fallback
}

bool HeadlessRenderer::CreateDepthImage()
{
    VkDevice device = m_ctx.GetDevice();
    VmaAllocator allocator = m_ctx.GetAllocator();
    m_depthFormat = ChooseDepthFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_depthFormat;
    imageInfo.extent = { m_width, m_height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(allocator, &imageInfo, &allocCreateInfo,
                        &m_depthImage, &m_depthAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        vmaDestroyImage(allocator, m_depthImage, m_depthAllocation);
        m_depthImage = VK_NULL_HANDLE;
        m_depthAllocation = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool HeadlessRenderer::CreateStagingBuffer()
{
    VmaAllocator allocator = m_ctx.GetAllocator();

    m_stagingSize = m_width * m_height * 4; // RGBA

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                            VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    return vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo,
                            &m_stagingBuffer, &m_stagingAllocation, nullptr) == VK_SUCCESS;
}

bool HeadlessRenderer::CreateFramebuffer()
{
    std::array<VkImageView, 2> attachments = { m_colorImageView, m_depthImageView };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_renderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = m_width;
    fbInfo.height = m_height;
    fbInfo.layers = 1;

    return vkCreateFramebuffer(m_ctx.GetDevice(), &fbInfo, nullptr, &m_framebuffer) == VK_SUCCESS;
}

bool HeadlessRenderer::AllocateCommandBuffer()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_ctx.GetCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    return vkAllocateCommandBuffers(m_ctx.GetDevice(), &allocInfo, &m_commandBuffer) == VK_SUCCESS;
}

void HeadlessRenderer::BeginFrame()
{
    vkResetCommandBuffer(m_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    m_renderPassStarted = false;
}

void HeadlessRenderer::Clear(float r, float g, float b, float a)
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}

void HeadlessRenderer::RenderScene()
{
    if (!m_sceneRendererInitialized) {
        return;
    }

    // Start render pass if not already started
    if (!m_renderPassStarted) {
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_renderPass;
        rpBegin.framebuffer = m_framebuffer;
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = { m_width, m_height };
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_renderPassStarted = true;
    }

    // Render the 3D scene
    m_sceneRenderer.Render(m_commandBuffer);
}

void HeadlessRenderer::EndFrame()
{
    // Start render pass if not already started (e.g., by ImGui or RenderScene)
    if (!m_renderPassStarted) {
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_renderPass;
        rpBegin.framebuffer = m_framebuffer;
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = { m_width, m_height };
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_renderPassStarted = true;
    }

    vkCmdEndRenderPass(m_commandBuffer);
    m_renderPassStarted = false;

    vkEndCommandBuffer(m_commandBuffer);
}

void HeadlessRenderer::Submit()
{
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    vkQueueSubmit(m_ctx.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_ctx.GetGraphicsQueue());
}

std::vector<uint8_t> HeadlessRenderer::ReadPixels()
{
    std::vector<uint8_t> pixels(m_width * m_height * 4);
    VmaAllocator allocator = m_ctx.GetAllocator();

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

    vkQueueSubmit(m_ctx.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_ctx.GetGraphicsQueue());

    // Map and copy using VMA
    void* data;
    vmaMapMemory(allocator, m_stagingAllocation, &data);
    memcpy(pixels.data(), data, pixels.size());
    vmaUnmapMemory(allocator, m_stagingAllocation);

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

void HeadlessRenderer::SetCaptureFilePath(const char* pathTemplate)
{
    if (m_renderDocApi) {
        m_renderDocApi->SetCaptureFilePathTemplate(pathTemplate);
    }
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

bool HeadlessRenderer::UsesDeviceLocalBuffers() const
{
    return m_sceneRendererInitialized && m_sceneRenderer.UsesDeviceLocalMemory();
}

void HeadlessRenderer::PrintDiagnostics()
{
    std::cout << "\n=== HEADLESS RENDERER DIAGNOSTICS ===" << std::endl;
    std::cout << "GPU: " << GetGPUName() << std::endl;
    std::cout << "Resolution: " << m_width << "x" << m_height << std::endl;
    std::cout << "Format: VK_FORMAT_B8G8R8A8_UNORM" << std::endl;
    std::cout << "Device-local buffers: " << (UsesDeviceLocalBuffers() ? "Yes" : "No") << std::endl;
    std::cout << "RenderDoc: " << (IsRenderDocAvailable() ? "Available" : "Not available") << std::endl;
    if (IsRenderDocAvailable()) {
        std::cout << "Captures: " << GetCaptureCount() << std::endl;
    }
    std::cout << "======================================\n" << std::endl;
}

// Cleanup functions
void HeadlessRenderer::DestroyFramebuffer()
{
    VkDevice device = m_ctx.GetDevice();
    if (m_framebuffer != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyRenderPass()
{
    VkDevice device = m_ctx.GetDevice();
    if (m_renderPass != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyStagingBuffer()
{
    VmaAllocator allocator = m_ctx.GetAllocator();
    if (m_stagingBuffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, m_stagingBuffer, m_stagingAllocation);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingAllocation = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyColorImage()
{
    VkDevice device = m_ctx.GetDevice();
    VmaAllocator allocator = m_ctx.GetAllocator();

    if (m_colorImageView != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_colorImageView, nullptr);
        m_colorImageView = VK_NULL_HANDLE;
    }
    if (m_colorImage != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_colorImage, m_colorAllocation);
        m_colorImage = VK_NULL_HANDLE;
        m_colorAllocation = VK_NULL_HANDLE;
    }
}

void HeadlessRenderer::DestroyDepthImage()
{
    VkDevice device = m_ctx.GetDevice();
    VmaAllocator allocator = m_ctx.GetAllocator();

    if (m_depthImageView != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_depthImage, m_depthAllocation);
        m_depthImage = VK_NULL_HANDLE;
        m_depthAllocation = VK_NULL_HANDLE;
    }
}

// ======================================================================
// ImGui integration
// ======================================================================

bool HeadlessRenderer::InitImGui()
{
    if (m_imguiInitialized) {
        return true;
    }

    if (!m_initialized) {
        std::cerr << "[HeadlessRenderer] Cannot init ImGui - renderer not initialized" << std::endl;
        return false;
    }

    std::cout << "[HeadlessRenderer] Initializing ImGui..." << std::endl;

    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Set display size for headless rendering
    io.DisplaySize = ImVec2(static_cast<float>(m_width), static_cast<float>(m_height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // Initialize style
    ImGui::StyleColorsDark();

    // Create descriptor pool for ImGui (follow backend example sizes)
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    const uint32_t poolCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.maxSets = 100 * poolCount;
    poolInfo.poolSizeCount = poolCount;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_ctx.GetDevice(), &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[HeadlessRenderer] Failed to create ImGui descriptor pool" << std::endl;
        ImGui::DestroyContext();
        return false;
    }

    // Initialize ImGui for Vulkan
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = m_ctx.GetInstance();
    initInfo.PhysicalDevice = m_ctx.GetPhysicalDevice();
    initInfo.Device = m_ctx.GetDevice();
    initInfo.QueueFamily = m_ctx.GetGraphicsQueueFamily();
    initInfo.Queue = m_ctx.GetGraphicsQueue();
    initInfo.DescriptorPool = m_imguiDescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = 2;
    initInfo.PipelineInfoMain.RenderPass = m_renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        std::cerr << "[HeadlessRenderer] Failed to initialize ImGui Vulkan backend" << std::endl;
        vkDestroyDescriptorPool(m_ctx.GetDevice(), m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
        ImGui::DestroyContext();
        return false;
    }

    m_imguiInitialized = true;
    std::cout << "[HeadlessRenderer] ImGui initialized successfully" << std::endl;

    return true;
}

void HeadlessRenderer::ShutdownImGui()
{
    if (!m_imguiInitialized) {
        return;
    }

    m_ctx.WaitIdle();

    ImGui_ImplVulkan_Shutdown();

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_ctx.GetDevice(), m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }

    ImGui::DestroyContext();

    m_imguiInitialized = false;
    std::cout << "[HeadlessRenderer] ImGui shutdown complete" << std::endl;
}

void HeadlessRenderer::ImGuiNewFrame()
{
    if (!m_imguiInitialized) {
        return;
    }

    ImGui_ImplVulkan_NewFrame();

    // For headless, we simulate the platform NewFrame
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;  // Assume 60 FPS

    ImGui::NewFrame();
}

void HeadlessRenderer::ImGuiRender()
{
    if (!m_imguiInitialized) {
        return;
    }

    // Start render pass if not already started
    if (!m_renderPassStarted) {
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_renderPass;
        rpBegin.framebuffer = m_framebuffer;
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = { m_width, m_height };
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_renderPassStarted = true;
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_commandBuffer);
}

// ======================================================================
// Textured rendering
// ======================================================================

bool HeadlessRenderer::InitTexturedRendering()
{
    if (!m_initialized) {
        std::cerr << "[HeadlessRenderer] Cannot init textured rendering - renderer not initialized" << std::endl;
        return false;
    }

    if (!m_sceneRendererInitialized) {
        std::cerr << "[HeadlessRenderer] Cannot init textured rendering - scene renderer not initialized" << std::endl;
        return false;
    }

    StagingManager* staging = m_stagingManager.IsInitialized() ? &m_stagingManager : nullptr;
    if (!m_sceneRenderer.InitTextured(m_renderPass, staging)) {
        std::cerr << "[HeadlessRenderer] Failed to initialize textured rendering" << std::endl;
        return false;
    }

    std::cout << "[HeadlessRenderer] Textured rendering initialized successfully" << std::endl;
    return true;
}

bool HeadlessRenderer::CreateTestTexture(const uint8_t* data, uint32_t width, uint32_t height)
{
    if (!m_sceneRendererInitialized) {
        return false;
    }
    return m_sceneRenderer.CreateTestTexture(data, width, height);
}

void HeadlessRenderer::RenderTexturedQuad()
{
    if (!m_sceneRenderer.IsTexturedInitialized()) {
        return;
    }

    // Start render pass if not already started
    if (!m_renderPassStarted) {
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_renderPass;
        rpBegin.framebuffer = m_framebuffer;
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = { m_width, m_height };
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(m_commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_renderPassStarted = true;
    }

    // Render the textured quad
    m_sceneRenderer.RenderTexturedQuad(m_commandBuffer);
}
