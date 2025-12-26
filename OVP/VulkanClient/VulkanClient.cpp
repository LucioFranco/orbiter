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

// ImGui includes
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_win32.h>

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
    , m_surface(VK_NULL_HANDLE)
    , m_viewportWidth(1920)
    , m_viewportHeight(1080)
    , m_imguiDescriptorPool(VK_NULL_HANDLE)
    , m_imguiInitialized(false)
    , m_frameInProgress(false)
{
    for (uint32_t i = 0; i < VulkanSwapchain::MAX_FRAMES_IN_FLIGHT; i++) {
        m_commandBuffers[i] = VK_NULL_HANDLE;
    }

    // Cornflower blue - a nice visible color
    m_clearColor[0] = 0.392f;
    m_clearColor[1] = 0.584f;
    m_clearColor[2] = 0.929f;
    m_clearColor[3] = 1.0f;
}

VulkanClient::~VulkanClient()
{
    m_swapchain.Shutdown();

    if (m_surface != VK_NULL_HANDLE && m_ctx.GetInstance() != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_ctx.GetInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    m_ctx.Shutdown();
}

bool VulkanClient::clbkInitialise()
{
    // Call base class initialization
    if (!GraphicsClient::clbkInitialise()) {
        return false;
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Initializing (launchpad phase)..."));

    // Note: We defer Vulkan context creation until clbkCreateRenderWindow()
    // because we need a window surface to properly select a GPU with present support.

    oapiWriteLog(const_cast<char*>("VulkanClient: Initialization complete"));
    return true;
}

HWND VulkanClient::clbkCreateRenderWindow()
{
    oapiWriteLog(const_cast<char*>("VulkanClient: Creating render window..."));

    // Call base class to create the window
    HWND hWnd = GraphicsClient::clbkCreateRenderWindow();
    if (!hWnd) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to create window"));
        return nullptr;
    }

    // Set window title to indicate Vulkan is working
    SetWindowText(hWnd, "[VulkanClient]");

    // Get window size
    RECT rect;
    GetClientRect(hWnd, &rect);
    m_viewportWidth = rect.right - rect.left;
    m_viewportHeight = rect.bottom - rect.top;

    // Fill window with black to avoid white flash (like D3D9Client does)
    HDC hDC = GetDC(hWnd);
    HBRUSH hBr = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hDC, &rect, hBr);
    DeleteObject(hBr);
    ReleaseDC(hWnd, hDC);
    ValidateRect(hWnd, NULL);

    // Ensure minimum window size for Vulkan
    if (m_viewportWidth == 0) m_viewportWidth = 800;
    if (m_viewportHeight == 0) m_viewportHeight = 600;

    char buf[256];
    sprintf_s(buf, "VulkanClient: Window created: %dx%d", m_viewportWidth, m_viewportHeight);
    oapiWriteLog(buf);

    // Initialize Vulkan context with surface extensions enabled
    VulkanContextCreateInfo ctxInfo{};
    ctxInfo.appName = "Orbiter Space Flight Simulator";
    ctxInfo.enableValidation = false;  // Disabled for debugging crash
    ctxInfo.enableSurface = true;
    ctxInfo.surfaceFactory = [this, hWnd](VkInstance instance) -> VkSurfaceKHR {
        VkWin32SurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hwnd = hWnd;
        surfaceInfo.hinstance = GetModuleHandle(nullptr);

        if (vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &m_surface) != VK_SUCCESS) {
            m_surface = VK_NULL_HANDLE;
        }
        return m_surface;
    };

    if (!m_ctx.Init(ctxInfo) || m_surface == VK_NULL_HANDLE) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to initialize Vulkan context"));
        return nullptr;
    }

    sprintf_s(buf, "VulkanClient: Using GPU: %s", m_ctx.GetGPUName().c_str());
    oapiWriteLog(buf);

    // Initialize swapchain
    if (!m_swapchain.Init(&m_ctx, m_surface, m_viewportWidth, m_viewportHeight)) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to initialize swapchain"));
        return nullptr;
    }
    oapiWriteLog(const_cast<char*>("VulkanClient: Swapchain initialized"));

    // Allocate command buffers
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_ctx.GetCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = VulkanSwapchain::MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(m_ctx.GetDevice(), &allocInfo, m_commandBuffers) != VK_SUCCESS) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to allocate command buffers"));
        return nullptr;
    }
    oapiWriteLog(const_cast<char*>("VulkanClient: Command buffers allocated"));

    oapiWriteLog(const_cast<char*>("VulkanClient: Render window ready"));
    return hWnd;
}

void VulkanClient::clbkDestroyRenderWindow(bool fastclose)
{
    oapiWriteLog(const_cast<char*>("VulkanClient: Destroying render window..."));

    m_ctx.WaitIdle();

    // Command buffers are freed when command pool is destroyed
    for (uint32_t i = 0; i < VulkanSwapchain::MAX_FRAMES_IN_FLIGHT; i++) {
        m_commandBuffers[i] = VK_NULL_HANDLE;
    }

    m_swapchain.Shutdown();

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_ctx.GetInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    GraphicsClient::clbkDestroyRenderWindow(fastclose);
}

void VulkanClient::clbkRenderScene()
{
    static int frameCount = 0;
    frameCount++;

    // Log first 5 calls unconditionally to debug
    if (frameCount <= 5) {
        char buf[128];
        sprintf(buf, "VulkanClient: clbkRenderScene called (frame %d, swapchain=%d)",
            frameCount, m_swapchain.IsInitialized());
        oapiWriteLog(buf);
    }

    if (!m_swapchain.IsInitialized()) {
        return;
    }

    // Acquire next swapchain image
    if (!m_swapchain.AcquireNextImage()) {
        // Swapchain needs recreation (window resized, etc.)
        HWND hWnd = GetRenderWindow();
        if (hWnd) {
            RECT rect;
            GetClientRect(hWnd, &rect);
            uint32_t width = rect.right - rect.left;
            uint32_t height = rect.bottom - rect.top;

            if (width > 0 && height > 0) {
                oapiWriteLog(const_cast<char*>("VulkanClient: Recreating swapchain"));
                m_swapchain.Recreate(width, height);
                m_viewportWidth = width;
                m_viewportHeight = height;
            }
        }
        return;
    }

    if (frameCount == 1) {
        oapiWriteLog(const_cast<char*>("VulkanClient: First frame rendering"));
    }

    uint32_t frameIndex = m_swapchain.GetCurrentFrame();
    VkCommandBuffer cmd = m_commandBuffers[frameIndex];

    // Begin command buffer recording
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin render pass with clear color
    VkClearValue clearValue{};
    clearValue.color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};

    VkExtent2D extent = m_swapchain.GetExtent();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_swapchain.GetRenderPass();
    rpBegin.framebuffer = m_swapchain.GetCurrentFramebuffer();
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Mark frame as in progress - clbkDisplayFrame will check this
    m_frameInProgress = true;

    // Note: Render pass is left open for ImGui to render into
    // It will be closed and submitted in clbkDisplayFrame()
}

bool VulkanClient::clbkDisplayFrame()
{
    static int displayCount = 0;
    displayCount++;

    // Log first 5 calls unconditionally to debug
    if (displayCount <= 5) {
        char buf[128];
        sprintf(buf, "VulkanClient: clbkDisplayFrame called (frame %d, init=%d, inProgress=%d)",
            displayCount, m_swapchain.IsInitialized(), m_frameInProgress);
        oapiWriteLog(buf);
    }

    if (!m_swapchain.IsInitialized() || !m_frameInProgress) {
        return true;  // Always return true - D3D9Client does this too
    }

    m_frameInProgress = false;

    uint32_t frameIndex = m_swapchain.GetCurrentFrame();
    VkCommandBuffer cmd = m_commandBuffers[frameIndex];

    // End render pass and command buffer
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit command buffer
    VkSemaphore waitSemaphores[] = { m_swapchain.GetImageAvailableSemaphore() };
    VkSemaphore signalSemaphores[] = { m_swapchain.GetRenderFinishedSemaphore() };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(m_ctx.GetGraphicsQueue(), 1, &submitInfo, m_swapchain.GetInFlightFence());

    // Present
    if (!m_swapchain.Present()) {
        // Swapchain will be recreated on next frame
        HWND hWnd = GetRenderWindow();
        if (hWnd) {
            RECT rect;
            GetClientRect(hWnd, &rect);
            uint32_t width = rect.right - rect.left;
            uint32_t height = rect.bottom - rect.top;
            if (width > 0 && height > 0) {
                oapiWriteLog(const_cast<char*>("VulkanClient: Recreating swapchain after present"));
                m_swapchain.Recreate(width, height);
                m_viewportWidth = width;
                m_viewportHeight = height;
            }
        }
    } else {
        m_swapchain.AdvanceFrame();
    }

    return true;
}

void VulkanClient::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex)
{
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearValue{};
    clearValue.color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};

    VkExtent2D extent = m_swapchain.GetExtent();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_swapchain.GetRenderPass();
    rpBegin.framebuffer = m_swapchain.GetCurrentFramebuffer();
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Future: Draw calls go here

    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);
}

void VulkanClient::clbkGetViewportSize(DWORD *width, DWORD *height) const
{
    *width = m_viewportWidth;
    *height = m_viewportHeight;
}

bool VulkanClient::clbkGetRenderParam(DWORD param, DWORD *value) const
{
    switch (param) {
        case RP_COLOURDEPTH:
            *value = 32;  // BGRA8
            return true;
        case RP_ZBUFFERDEPTH:
            *value = 24;  // D24S8
            return true;
        case RP_STENCILDEPTH:
            *value = 8;   // D24S8
            return true;
        case RP_MAXLIGHTS:
            *value = 8;   // Reasonable default
            return true;
        case RP_ISTLDEVICE:
            *value = 1;   // Yes, we support T&L
            return true;
        case RP_REQUIRETEXPOW2:
            *value = 0;   // No, Vulkan doesn't require power-of-2 textures
            return true;
        default:
            *value = 0;
            return false;
    }
}

// ======================================================================
// ImGui implementation
// ======================================================================

// Callback for ImGui multi-viewport support - creates VkSurface for secondary windows
// This is needed because imgui_impl_win32.cpp doesn't include Vulkan headers
static int ImGui_ImplWin32_CreateVkSurface(ImGuiViewport* viewport, ImU64 vk_instance, const void* vk_allocator, ImU64* out_vk_surface)
{
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)viewport->PlatformHandleRaw;
    createInfo.hinstance = ::GetModuleHandle(nullptr);
    return (int)vkCreateWin32SurfaceKHR((VkInstance)vk_instance, &createInfo, (VkAllocationCallbacks*)vk_allocator, (VkSurfaceKHR*)out_vk_surface);
}

void VulkanClient::clbkImGuiInit()
{
    if (m_imguiInitialized) {
        return;
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Initializing ImGui..."));

    // Create descriptor pool for ImGui (match backend example sizes)
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
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to create ImGui descriptor pool"));
        return;
    }

    // Note: Orbiter's DialogManager handles ImGui_ImplWin32_Init() before calling us
    // We only need to initialize the Vulkan backend

    // Set up the callback for creating VkSurface objects for multi-viewport support
    // This must be done after ImGui_ImplWin32_Init and before ImGui_ImplVulkan_Init
    ImGui::GetPlatformIO().Platform_CreateVkSurface = ImGui_ImplWin32_CreateVkSurface;

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
    initInfo.ImageCount = m_swapchain.GetImageCount();
    initInfo.PipelineInfoMain.RenderPass = m_swapchain.GetRenderPass();
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to initialize ImGui Vulkan"));
        return;
    }

    m_imguiInitialized = true;
    oapiWriteLog(const_cast<char*>("VulkanClient: ImGui initialized successfully"));
}

void VulkanClient::clbkImGuiShutdown()
{
    if (!m_imguiInitialized) {
        return;
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Shutting down ImGui..."));

    m_ctx.WaitIdle();

    // Note: Orbiter's DialogManager handles ImGui_ImplWin32_Shutdown() after calling us
    ImGui_ImplVulkan_Shutdown();

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_ctx.GetDevice(), m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }

    m_imguiInitialized = false;
    oapiWriteLog(const_cast<char*>("VulkanClient: ImGui shutdown complete"));
}

void VulkanClient::clbkImGuiNewFrame()
{
    if (!m_imguiInitialized) {
        return;
    }

    // Note: Orbiter's DialogManager handles ImGui_ImplWin32_NewFrame() after calling us
    ImGui_ImplVulkan_NewFrame();
}

void VulkanClient::clbkImGuiRenderDrawData()
{
    if (!m_imguiInitialized || !m_frameInProgress) {
        return;
    }

    ImGui::Render();

    uint32_t frameIndex = m_swapchain.GetCurrentFrame();
    VkCommandBuffer cmd = m_commandBuffers[frameIndex];

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    // Update and Render additional Platform Windows (required when ViewportsEnable is set)
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}
