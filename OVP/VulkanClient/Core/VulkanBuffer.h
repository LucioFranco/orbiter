// ==============================================================
// VulkanBuffer.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Simple buffer helper for vertex and index buffers.
// ==============================================================

#ifndef VULKANBUFFER_H
#define VULKANBUFFER_H

#include "VulkanContext.h"

// Forward declaration
class StagingManager;

class VulkanBuffer {
public:
    VulkanBuffer();
    ~VulkanBuffer();

    // Lifecycle
    bool Create(VulkanContext* ctx, VkDeviceSize size,
                VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);

    // Create device-local buffer with initial data (uploads via staging)
    bool CreateDeviceLocal(VulkanContext* ctx, StagingManager* staging,
                           VkDeviceSize size, VkBufferUsageFlags usage,
                           const void* data);

    void Destroy();
    bool IsCreated() const { return m_buffer != VK_NULL_HANDLE; }
    bool IsDeviceLocal() const { return m_deviceLocal; }

    // Memory access (only for host-visible buffers)
    void* Map();
    void Unmap();

    // Accessors
    VkBuffer GetBuffer() const { return m_buffer; }
    VmaAllocation GetAllocation() const { return m_allocation; }
    VkDeviceSize GetSize() const { return m_size; }

private:
    VulkanContext* m_ctx;
    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    VkDeviceSize m_size;
    void* m_mapped;
    bool m_deviceLocal;
};

#endif // VULKANBUFFER_H
