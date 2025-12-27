// ==============================================================
// VulkanClient.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifndef VULKANCLIENT_H
#define VULKANCLIENT_H

#include "GraphicsAPI.h"
#include "Core/VulkanContext.h"
#include "Core/VulkanSwapchain.h"
#include "Core/SceneRenderer.h"
#include <vector>

// Forward declare ImGui types
struct ImGui_ImplVulkan_InitInfo;

#ifdef VULKANCLIENT_EXPORTS
#define VULKANCLIENT_API DLLEXPORT
#else
#define VULKANCLIENT_API DLLIMPORT
#endif

// ======================================================================
// class VulkanClient
// ======================================================================
/**
 * \brief Vulkan-based graphics client for Orbiter.
 *
 * This is a minimal implementation to establish the Vulkan rendering
 * framework for Orbiter.
 */
class VulkanClient : public oapi::GraphicsClient {
public:
    VulkanClient(HINSTANCE hInstance);
    ~VulkanClient();

    // GraphicsClient interface overrides - Lifecycle
    bool clbkInitialise() override;
    HWND clbkCreateRenderWindow() override;
    void clbkDestroyRenderWindow(bool fastclose) override;

    // GraphicsClient interface overrides - Rendering
    void clbkUpdate(bool running) override;
    void clbkRenderScene() override;
    bool clbkDisplayFrame() override;

    // Module interface overrides - Input
    bool clbkProcessKeyboardImmediate(char kstate[256], bool simRunning) override;

    // GraphicsClient interface overrides - Info
    bool clbkFullscreenMode() const override { return false; }
    void clbkGetViewportSize(DWORD *width, DWORD *height) const override;
    bool clbkGetRenderParam(DWORD param, DWORD *value) const override;

    // ImGui interface
    void clbkImGuiNewFrame() override;
    void clbkImGuiRenderDrawData() override;
    void clbkImGuiInit() override;
    void clbkImGuiShutdown() override;
    uint64_t clbkImGuiSurfaceTexture(SURFHANDLE surf) override { return 0; }

    /**
     * \brief Returns the Vulkan instance.
     */
    VkInstance GetVulkanInstance() const { return m_ctx.GetInstance(); }

private:
    // Rendering helpers
    void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

    // Shared Vulkan context
    VulkanContext m_ctx;

    // Window surface
    VkSurfaceKHR m_surface;

    // Swapchain
    VulkanSwapchain m_swapchain;

    // Command buffers (one per frame in flight)
    VkCommandBuffer m_commandBuffers[VulkanSwapchain::MAX_FRAMES_IN_FLIGHT];

    // Clear color (cornflower blue)
    float m_clearColor[4];

    // Viewport size cache
    uint32_t m_viewportWidth;
    uint32_t m_viewportHeight;

    // ImGui state
    VkDescriptorPool m_imguiDescriptorPool;
    bool m_imguiInitialized;

    // Frame state - tracks if clbkRenderScene started a frame successfully
    bool m_frameInProgress;

    // 3D scene rendering
    SceneRenderer m_sceneRenderer;
    bool m_sceneRendererInitialized;
};

#endif // !VULKANCLIENT_H
