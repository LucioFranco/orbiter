// ==============================================================
// HeadlessRenderer.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Headless Vulkan renderer for testing and tools.
// Renders to an offscreen image without requiring a window.
// ==============================================================

#ifndef HEADLESSRENDERER_H
#define HEADLESSRENDERER_H

#include "Core/VulkanContext.h"
#include <vector>
#include <string>
#include <cstdint>

// Forward declaration for RenderDoc API
struct RENDERDOC_API_1_6_0;

class HeadlessRenderer {
public:
    HeadlessRenderer();
    ~HeadlessRenderer();

    // Lifecycle
    bool Init(uint32_t width, uint32_t height);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Rendering - Phase 1
    void BeginFrame();
    void Clear(float r, float g, float b, float a);
    void EndFrame();
    void Submit();

    // Readback
    std::vector<uint8_t> ReadPixels();

    // RenderDoc integration
    bool IsRenderDocAvailable() const { return m_renderDocApi != nullptr; }
    void StartCapture();
    void EndCapture();
    uint32_t GetCaptureCount() const;

    // Diagnostics
    void PrintDiagnostics();
    std::string GetGPUName() const { return m_ctx.GetGPUName(); }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

    // ImGui integration (for testing)
    bool InitImGui();
    void ShutdownImGui();
    bool IsImGuiInitialized() const { return m_imguiInitialized; }
    void ImGuiNewFrame();
    void ImGuiRender();  // Call between BeginFrame() and EndFrame()

private:
    // Initialization helpers (renderer-specific resources)
    bool CreateColorImage();
    bool CreateStagingBuffer();
    bool CreateRenderPass();
    bool CreateFramebuffer();
    bool AllocateCommandBuffer();
    bool LoadRenderDoc();

    // Cleanup helpers
    void DestroyFramebuffer();
    void DestroyRenderPass();
    void DestroyStagingBuffer();
    void DestroyColorImage();

    // State
    bool m_initialized;
    uint32_t m_width;
    uint32_t m_height;

    // Shared Vulkan context
    VulkanContext m_ctx;

    // Command buffer (allocated from context's command pool)
    VkCommandBuffer m_commandBuffer;

    // Render target
    VkImage m_colorImage;
    VkDeviceMemory m_colorMemory;
    VkImageView m_colorImageView;
    VkFormat m_colorFormat;

    // Render pass
    VkRenderPass m_renderPass;
    VkFramebuffer m_framebuffer;

    // Staging buffer for readback
    VkBuffer m_stagingBuffer;
    VkDeviceMemory m_stagingMemory;
    VkDeviceSize m_stagingSize;

    // Clear color (set by Clear(), used in EndFrame())
    float m_clearColor[4];

    // Render pass state
    bool m_renderPassStarted;

    // RenderDoc
    void* m_renderDocModule;
    RENDERDOC_API_1_6_0* m_renderDocApi;

    // ImGui state
    VkDescriptorPool m_imguiDescriptorPool;
    bool m_imguiInitialized;
};

#endif // HEADLESSRENDERER_H
