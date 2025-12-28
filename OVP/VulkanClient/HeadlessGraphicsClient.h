// ==============================================================
// HeadlessGraphicsClient.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// GraphicsClient implementation using HeadlessRenderer for
// offscreen rendering. Enables testing without a window.
// ==============================================================

#ifndef HEADLESSGRAPHICSCLIENT_H
#define HEADLESSGRAPHICSCLIENT_H

#include "GraphicsAPI.h"
#include "HeadlessRenderer.h"
#include "Mesh/VulkanMesh.h"
#include "Resources/TextureManager.h"
#include "Core/VulkanTexture.h"
#include <vector>
#include <string>
#include <map>
#include <memory>

#ifdef HEADLESSGRAPHICSCLIENT_EXPORTS
#define HEADLESSGRAPHICSCLIENT_API DLLEXPORT
#else
#define HEADLESSGRAPHICSCLIENT_API DLLIMPORT
#endif

// ======================================================================
// class HeadlessGraphicsClient
// ======================================================================
/**
 * \brief Headless graphics client for Orbiter testing.
 *
 * This GraphicsClient implementation uses HeadlessRenderer to render
 * to an offscreen buffer. It enables running full Orbiter simulation
 * with graphics rendering, but without a visible window.
 *
 * Use cases:
 * - Automated testing with pixel validation
 * - Headless rendering for CI/CD pipelines
 * - Screenshot generation
 *
 * Launch with:
 *   orbiter.exe --scenario=DG/Default --plugin=HeadlessGraphicsClient --maxframes=10
 */
class HeadlessGraphicsClient : public oapi::GraphicsClient {
public:
    HeadlessGraphicsClient(HINSTANCE hInstance);
    ~HeadlessGraphicsClient();

    // GraphicsClient interface overrides - Lifecycle
    bool clbkInitialise() override;
    HWND clbkCreateRenderWindow() override;
    void clbkDestroyRenderWindow(bool fastclose) override;

    // GraphicsClient interface overrides - Rendering
    void clbkUpdate(bool running) override;
    void clbkRenderScene() override;
    bool clbkDisplayFrame() override;

    // GraphicsClient interface overrides - Info
    bool clbkFullscreenMode() const override { return false; }
    void clbkGetViewportSize(DWORD *width, DWORD *height) const override;
    bool clbkGetRenderParam(DWORD param, DWORD *value) const override;

    // GraphicsClient interface overrides - Vessel management
    void clbkNewVessel(OBJHANDLE hVessel) override;
    void clbkDeleteVessel(OBJHANDLE hVessel) override;

    // GraphicsClient interface overrides - Visual events
    int clbkVisEvent(OBJHANDLE hObj, VISHANDLE vis, DWORD msg, DWORD_PTR context) override;

    // ImGui interface - uses HeadlessRenderer's ImGui support
    void clbkImGuiNewFrame() override;
    void clbkImGuiRenderDrawData() override;
    void clbkImGuiInit() override;
    void clbkImGuiShutdown() override;
    uint64_t clbkImGuiSurfaceTexture(SURFHANDLE surf) override { return 0; }

    // Surface/Texture loading callbacks
    SURFHANDLE clbkLoadTexture(const char* fname, DWORD flags = 0) override;
    void clbkReleaseTexture(SURFHANDLE hTex) override;
    bool clbkReleaseSurface(SURFHANDLE surf) override;
    bool clbkGetSurfaceSize(SURFHANDLE surf, DWORD* w, DWORD* h) override;
    void clbkIncrSurfaceRef(SURFHANDLE surf) override;

    // Get VulkanTexture from SURFHANDLE (for TextureManager)
    VulkanTexture* GetTextureFromSurface(SURFHANDLE surf) const;

    // ======================================================================
    // Test API - methods for automated testing
    // ======================================================================

    /**
     * \brief Get the internal HeadlessRenderer for direct access.
     */
    HeadlessRenderer* GetRenderer() { return &m_renderer; }

    /**
     * \brief Capture current frame pixels.
     * \return BGRA pixel data, width*height*4 bytes
     */
    std::vector<uint8_t> CaptureFrame();

    /**
     * \brief Save current frame to PPM file (for debugging).
     * \param path Output file path
     */
    void SaveFrameToPPM(const char* path);

    /**
     * \brief Get current frame count.
     */
    uint32_t GetFrameCount() const { return m_frameCount; }

    /**
     * \brief Set output path for automatic frame capture.
     * If set, each frame is saved to the specified directory.
     */
    void SetCaptureOutputPath(const std::string& path) { m_captureOutputPath = path; }

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

    // Vessel visual tracking
    struct VesselVisual {
        OBJHANDLE hObj;
        std::vector<std::unique_ptr<VulkanMesh>> meshes;
        std::vector<MESHHANDLE> meshHandles;  // Parallel to meshes, for texture lookup
    };

    // Create/destroy hidden window for Orbiter's render loop
    bool CreateHiddenWindow();
    void DestroyHiddenWindow();

    // Headless renderer (no window, renders to offscreen buffer)
    HeadlessRenderer m_renderer;

    // Hidden window (required for Orbiter's render loop, but not used for rendering)
    HWND m_hWnd;
    ATOM m_windowClass;

    // Viewport size
    uint32_t m_viewportWidth;
    uint32_t m_viewportHeight;

    // Frame tracking
    uint32_t m_frameCount;
    bool m_frameInProgress;

    // Optional: auto-capture frames to files
    std::string m_captureOutputPath;

    // Vessel visual tracking
    std::map<VISHANDLE, std::unique_ptr<VesselVisual>> m_vesselVisuals;
    std::map<OBJHANDLE, VISHANDLE> m_objToVisual;  // Maps vessel to its visual handle

    // ImGui Vulkan backend state
    VkDescriptorPool m_imguiDescriptorPool;
    bool m_imguiInitialized;

    // Texture manager for mesh textures
    TextureManager m_texManager;

    // Loaded surfaces/textures keyed by their pointer (acts as SURFHANDLE)
    std::map<SURFHANDLE, std::unique_ptr<SurfaceHandle>> m_surfaces;

    // Texture cache keyed by filename to avoid reloading
    std::map<std::string, SURFHANDLE> m_textureCache;
};

#endif // !HEADLESSGRAPHICSCLIENT_H
