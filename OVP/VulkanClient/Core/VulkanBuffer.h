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

class VulkanBuffer {
public:
    VulkanBuffer();
    ~VulkanBuffer();

    // Lifecycle
    bool Create(VulkanContext* ctx, VkDeviceSize size,
                VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void Destroy();
    bool IsCreated() const { return m_buffer != VK_NULL_HANDLE; }

    // Memory access
    void* Map();
    void Unmap();

    // Accessors
    VkBuffer GetBuffer() const { return m_buffer; }
    VkDeviceMemory GetMemory() const { return m_memory; }
    VkDeviceSize GetSize() const { return m_size; }

private:
    VulkanContext* m_ctx;
    VkBuffer m_buffer;
    VkDeviceMemory m_memory;
    VkDeviceSize m_size;
    void* m_mapped;
};

#endif // VULKANBUFFER_H
