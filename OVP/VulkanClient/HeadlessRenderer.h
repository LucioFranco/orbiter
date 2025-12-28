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
#include "Core/StagingManager.h"
#include "Core/SceneRenderer.h"
#include "Core/MeshPipeline.h"
#include "Mesh/VulkanMesh.h"
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

    // Rendering
    void BeginFrame();
    void Clear(float r, float g, float b, float a);
    void BeginRenderPass();  // Start render pass (for external rendering)
    void RenderScene();  // Renders 3D scene using SceneRenderer
    void EndFrame();
    void Submit();

    // Textured rendering (for testing texture pipeline)
    bool InitTexturedRendering();
    bool CreateTestTexture(const uint8_t* data, uint32_t width, uint32_t height);
    void RenderTexturedQuad();

    // Mesh rendering
    bool InitMeshRendering();
    bool IsMeshRenderingInitialized() const { return m_meshPipelineInitialized; }
    MeshPipeline* GetMeshPipeline() { return &m_meshPipeline; }
    VkCommandBuffer GetCurrentCommandBuffer() const { return m_commandBuffer; }
    VkRenderPass GetRenderPass() const { return m_renderPass; }
    // Render mesh with material support (diffuse RGBA, emissive RGB)
    void RenderMesh(VulkanMesh* mesh, const float* mvp, const float* model, const float* lightDir,
                    const float* matDiffuse = nullptr, const float* matEmissive = nullptr);
    // Render a single mesh group with material (for per-group material rendering)
    void RenderMeshGroup(VulkanMesh* mesh, uint32_t groupIdx,
                         const float* mvp, const float* model, const float* lightDir,
                         const float* matDiffuse = nullptr, const float* matEmissive = nullptr);
    // Render a single mesh group with custom texture (for per-group textured rendering)
    void RenderMeshGroupTextured(VulkanMesh* mesh, uint32_t groupIdx,
                                 const float* mvp, const float* model, const float* lightDir,
                                 VkDescriptorSet textureDescriptor,
                                 const float* matDiffuse = nullptr, const float* matEmissive = nullptr);
    // Render mesh with custom texture descriptor
    void RenderMeshTextured(VulkanMesh* mesh, const float* mvp, const float* model, const float* lightDir,
                            VkDescriptorSet textureDescriptor, const float* matDiffuse = nullptr);
    // Create a test texture and return its descriptor set (caller owns the texture memory)
    VkDescriptorSet CreateMeshTestTexture(const uint8_t* rgba, uint32_t width, uint32_t height,
                                          VkImage* outImage, VmaAllocation* outAlloc,
                                          VkImageView* outView, VkSampler* outSampler);

    // Readback
    std::vector<uint8_t> ReadPixels();

    // RenderDoc integration
    bool IsRenderDocAvailable() const { return m_renderDocApi != nullptr; }
    void SetCaptureFilePath(const char* pathTemplate);  // Set path for captures (e.g., "captures/test")
    void StartCapture();
    void EndCapture();
    uint32_t GetCaptureCount() const;

    // Diagnostics
    void PrintDiagnostics();
    std::string GetGPUName() const { return m_ctx.GetGPUName(); }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

    // Access to internal components (for advanced usage)
    VulkanContext* GetVulkanContext() { return &m_ctx; }
    StagingManager* GetStagingManager() { return &m_stagingManager; }
    bool UsesDeviceLocalBuffers() const;

    // ImGui integration (for testing)
    bool InitImGui();
    void ShutdownImGui();
    bool IsImGuiInitialized() const { return m_imguiInitialized; }
    void ImGuiNewFrame();
    void ImGuiRender();  // Call between BeginFrame() and EndFrame()

private:
    // Initialization helpers (renderer-specific resources)
    bool CreateColorImage();
    bool CreateDepthImage();
    bool CreateStagingBuffer();
    bool CreateFramebuffer();
    bool AllocateCommandBuffer();
    bool LoadRenderDoc();
    VkFormat ChooseDepthFormat();

    // Cleanup helpers
    void DestroyFramebuffer();
    void DestroyRenderPass();
    void DestroyStagingBuffer();
    void DestroyColorImage();
    void DestroyDepthImage();

    // State
    bool m_initialized;
    uint32_t m_width;
    uint32_t m_height;

    // Shared Vulkan context
    VulkanContext m_ctx;

    // Staging manager for CPU->GPU uploads
    StagingManager m_stagingManager;

    // Command buffer (allocated from context's command pool)
    VkCommandBuffer m_commandBuffer;

    // Render target
    VkImage m_colorImage;
    VmaAllocation m_colorAllocation;
    VkImageView m_colorImageView;
    VkFormat m_colorFormat;

    // Depth buffer
    VkImage m_depthImage;
    VmaAllocation m_depthAllocation;
    VkImageView m_depthImageView;
    VkFormat m_depthFormat;

    // Render pass
    VkRenderPass m_renderPass;
    VkFramebuffer m_framebuffer;

    // Staging buffer for readback
    VkBuffer m_stagingBuffer;
    VmaAllocation m_stagingAllocation;
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

    // 3D scene rendering
    SceneRenderer m_sceneRenderer;
    bool m_sceneRendererInitialized;

    // Mesh rendering
    MeshPipeline m_meshPipeline;
    bool m_meshPipelineInitialized;
};

#endif // HEADLESSRENDERER_H
