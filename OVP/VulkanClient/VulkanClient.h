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
#include "Core/StagingManager.h"
#include "Core/MeshPipeline.h"
#include "Core/VulkanTexture.h"
#include "Mesh/VulkanMesh.h"
#include "Resources/TextureManager.h"
#include <vector>
#include <map>
#include <memory>
#include <string>

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

    // GraphicsClient interface overrides - Vessel management
    void clbkNewVessel(OBJHANDLE hVessel) override;
    void clbkDeleteVessel(OBJHANDLE hVessel) override;

    // GraphicsClient interface overrides - Visual events
    int clbkVisEvent(OBJHANDLE hObj, VISHANDLE vis, DWORD msg, DWORD_PTR context) override;

    // Surface/Texture loading callbacks
    SURFHANDLE clbkLoadTexture(const char* fname, DWORD flags = 0) override;
    void clbkReleaseTexture(SURFHANDLE hTex) override;
    bool clbkReleaseSurface(SURFHANDLE surf) override;
    bool clbkGetSurfaceSize(SURFHANDLE surf, DWORD* w, DWORD* h) override;
    void clbkIncrSurfaceRef(SURFHANDLE surf) override;

    // ImGui interface
    void clbkImGuiNewFrame() override;
    void clbkImGuiRenderDrawData() override;
    void clbkImGuiInit() override;
    void clbkImGuiShutdown() override;
    uint64_t clbkImGuiSurfaceTexture(SURFHANDLE surf) override { return 0; }

    // Get VulkanTexture from SURFHANDLE (for TextureManager)
    VulkanTexture* GetTextureFromSurface(SURFHANDLE surf) const;

    /**
     * \brief Returns the Vulkan instance.
     */
    VkInstance GetVulkanInstance() const { return m_ctx.GetInstance(); }

private:
    // Surface/Texture wrapper - acts as SURFHANDLE for Orbiter
    struct SurfaceHandle {
        std::unique_ptr<VulkanTexture> texture;
        std::string filename;           // Source filename (for debugging/caching)
        DWORD flags;                    // Creation flags
        int refCount;                   // Reference counter
        uint32_t width;
        uint32_t height;

        SurfaceHandle() : flags(0), refCount(1), width(0), height(0) {}
    };

    // Per-mesh offset transform (translation only, like D3D9Client)
    struct MeshTransform {
        float offset[3];  // x, y, z offset
        bool hasOffset;

        MeshTransform() : hasOffset(false) { offset[0] = offset[1] = offset[2] = 0.0f; }
    };

    // Vessel visual tracking
    struct VesselVisual {
        OBJHANDLE hObj;
        std::vector<std::unique_ptr<VulkanMesh>> meshes;
        std::vector<MESHHANDLE> meshHandles;  // Parallel to meshes, for texture lookup
        std::vector<MeshTransform> meshTransforms;  // Per-mesh offset transforms
    };

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

    // First update flag for scanning existing vessels
    bool m_firstUpdate;

    // 3D scene rendering
    SceneRenderer m_sceneRenderer;
    bool m_sceneRendererInitialized;

    // Staging manager for GPU uploads
    StagingManager m_stagingManager;

    // Mesh rendering pipeline
    MeshPipeline m_meshPipeline;
    bool m_meshPipelineInitialized;

    // Texture manager for mesh textures
    TextureManager m_texManager;

    // Vessel visual tracking
    std::map<VISHANDLE, std::unique_ptr<VesselVisual>> m_vesselVisuals;
    std::map<OBJHANDLE, VISHANDLE> m_objToVisual;  // Maps vessel to its visual handle

    // Loaded surfaces/textures keyed by their pointer (acts as SURFHANDLE)
    std::map<SURFHANDLE, std::unique_ptr<SurfaceHandle>> m_surfaces;

    // Texture cache keyed by filename to avoid reloading
    std::map<std::string, SURFHANDLE> m_textureCache;
};

#endif // !VULKANCLIENT_H
