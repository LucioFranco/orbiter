// ==============================================================
// VulkanSwapchain.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Swapchain management for window presentation.
// ==============================================================

#ifndef VULKANSWAPCHAIN_H
#define VULKANSWAPCHAIN_H

#include "VulkanContext.h"
#include <vector>

class VulkanSwapchain {
public:
    static const uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    VulkanSwapchain();
    ~VulkanSwapchain();

    // Lifecycle
    bool Init(VulkanContext* ctx, VkSurfaceKHR surface, uint32_t width, uint32_t height);
    void Shutdown();
    bool Recreate(uint32_t width, uint32_t height);
    bool IsInitialized() const { return m_initialized; }

    // Frame operations
    bool AcquireNextImage();  // Returns false if swapchain needs recreation
    void Present();           // Present the current image

    // Accessors
    VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
    VkRenderPass GetRenderPass() const { return m_renderPass; }
    VkFramebuffer GetCurrentFramebuffer() const;
    VkExtent2D GetExtent() const { return m_extent; }
    VkFormat GetFormat() const { return m_format; }
    uint32_t GetImageCount() const { return static_cast<uint32_t>(m_images.size()); }
    uint32_t GetCurrentImageIndex() const { return m_currentImageIndex; }
    uint32_t GetCurrentFrame() const { return m_currentFrame; }

    // Sync objects for current frame
    VkSemaphore GetImageAvailableSemaphore() const;
    VkSemaphore GetRenderFinishedSemaphore() const;
    VkFence GetInFlightFence() const;

    // Advance to next frame (call after present)
    void AdvanceFrame();

private:
    // Creation helpers
    bool CreateSwapchain();
    bool CreateImageViews();
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateSyncObjects();

    // Query helpers
    VkSurfaceFormatKHR ChooseSurfaceFormat();
    VkPresentModeKHR ChoosePresentMode();
    VkExtent2D ChooseExtent(uint32_t width, uint32_t height);

    // Cleanup helpers
    void CleanupSwapchain();
    void DestroySyncObjects();

    // State
    bool m_initialized;
    VulkanContext* m_ctx;
    VkSurfaceKHR m_surface;

    // Swapchain
    VkSwapchainKHR m_swapchain;
    VkFormat m_format;
    VkExtent2D m_extent;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;

    // Render pass and framebuffers
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer> m_framebuffers;

    // Frame synchronization
    uint32_t m_currentImageIndex;
    uint32_t m_currentFrame;
    VkSemaphore m_imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore m_renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence m_inFlightFences[MAX_FRAMES_IN_FLIGHT];
};

#endif // VULKANSWAPCHAIN_H
