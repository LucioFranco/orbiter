// ==============================================================
// StagingManager.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Manages staging buffer for GPU uploads.
// Enables DEVICE_LOCAL memory for optimal GPU performance.
// ==============================================================

#ifndef STAGINGMANAGER_H
#define STAGINGMANAGER_H

#include "VulkanContext.h"

class StagingManager {
public:
    StagingManager();
    ~StagingManager();

    // Lifecycle
    bool Init(VulkanContext* ctx, VkDeviceSize stagingSize = 64 * 1024 * 1024);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Upload operations - copy data to device-local buffers/images via staging
    bool UploadBuffer(VkBuffer dstBuffer, const void* data, VkDeviceSize size, VkDeviceSize dstOffset = 0);
    bool UploadImage(VkImage dstImage, const void* data, uint32_t width, uint32_t height,
                     VkFormat format, VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Flush pending uploads (blocks until GPU completes)
    void Flush();

    // Accessors
    VulkanContext* GetContext() const { return m_ctx; }

private:
    // Begin recording transfer commands (resets command buffer)
    void BeginTransfer();
    // End recording and submit (does not wait)
    void EndTransfer();

    // State
    bool m_initialized;
    VulkanContext* m_ctx;

    // Staging buffer (HOST_VISIBLE, used for CPU->GPU transfers)
    VkBuffer m_stagingBuffer;
    VmaAllocation m_stagingAllocation;
    VkDeviceSize m_stagingSize;
    VkDeviceSize m_stagingOffset;  // Current write position
    void* m_mappedData;            // Persistently mapped

    // Transfer command buffer and synchronization
    VkCommandBuffer m_commandBuffer;
    VkFence m_transferFence;
    bool m_transferInProgress;
};

#endif // STAGINGMANAGER_H
