// ==============================================================
// VulkanClient.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifndef VULKANCLIENT_H
#define VULKANCLIENT_H

#include "GraphicsAPI.h"
#include <vulkan/vulkan.h>
#include <vector>

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

    // GraphicsClient interface overrides
    bool clbkInitialise() override;
    bool clbkFullscreenMode() const override { return false; }
    void clbkGetViewportSize(DWORD *width, DWORD *height) const override;
    bool clbkGetRenderParam(DWORD param, DWORD *value) const override;
    void clbkRenderScene() override {}

    // ImGui stubs (required pure virtuals)
    void clbkImGuiNewFrame() override {}
    void clbkImGuiRenderDrawData() override {}
    void clbkImGuiInit() override {}
    void clbkImGuiShutdown() override {}
    uint64_t clbkImGuiSurfaceTexture(SURFHANDLE surf) override { return 0; }

    /**
     * \brief Returns the Vulkan instance.
     */
    VkInstance GetVulkanInstance() const { return m_instance; }

private:
    bool CreateInstance();
    void DestroyInstance();

    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();

    VkInstance m_instance;
    bool m_enableValidationLayers;

#ifdef _DEBUG
    VkDebugUtilsMessengerEXT m_debugMessenger;
    bool SetupDebugMessenger();
    void DestroyDebugMessenger();
#endif
};

#endif // !VULKANCLIENT_H
