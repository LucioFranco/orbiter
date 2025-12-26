// ==============================================================
// VulkanBuffer.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "VulkanBuffer.h"
#include "StagingManager.h"

VulkanBuffer::VulkanBuffer()
    : m_ctx(nullptr)
    , m_buffer(VK_NULL_HANDLE)
    , m_allocation(VK_NULL_HANDLE)
    , m_size(0)
    , m_mapped(nullptr)
    , m_deviceLocal(false)
{
}

VulkanBuffer::~VulkanBuffer() {
    Destroy();
}

bool VulkanBuffer::Create(VulkanContext* ctx, VkDeviceSize size,
                          VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    if (m_buffer != VK_NULL_HANDLE) {
        return false;  // Already created
    }
    if (!ctx || !ctx->IsInitialized() || size == 0) {
        return false;
    }

    m_ctx = ctx;
    m_size = size;

    // Buffer create info
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VMA allocation info
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = properties;

    // For host-visible memory, specify how we'll access it
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    // Create buffer and allocate memory in one call
    VkResult result = vmaCreateBuffer(ctx->GetAllocator(), &bufferInfo, &allocInfo,
                                       &m_buffer, &m_allocation, nullptr);

    if (result == VK_SUCCESS) {
        m_deviceLocal = (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    }

    return result == VK_SUCCESS;
}

bool VulkanBuffer::CreateDeviceLocal(VulkanContext* ctx, StagingManager* staging,
                                      VkDeviceSize size, VkBufferUsageFlags usage,
                                      const void* data) {
    if (m_buffer != VK_NULL_HANDLE) {
        return false;  // Already created
    }
    if (!ctx || !ctx->IsInitialized() || !staging || !staging->IsInitialized() || size == 0) {
        return false;
    }

    m_ctx = ctx;
    m_size = size;

    // Buffer create info - add TRANSFER_DST for staging uploads
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VMA allocation info - prefer device-local memory
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // Create buffer and allocate memory
    VkResult result = vmaCreateBuffer(ctx->GetAllocator(), &bufferInfo, &allocInfo,
                                       &m_buffer, &m_allocation, nullptr);

    if (result != VK_SUCCESS) {
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_size = 0;
        m_ctx = nullptr;
        return false;
    }

    m_deviceLocal = true;

    // Upload initial data via staging buffer
    if (data) {
        if (!staging->UploadBuffer(m_buffer, data, size, 0)) {
            Destroy();
            return false;
        }
        staging->Flush();  // Wait for upload to complete
    }

    return true;
}

void VulkanBuffer::Destroy() {
    if (!m_ctx || m_buffer == VK_NULL_HANDLE) {
        return;
    }

    if (m_mapped) {
        Unmap();
    }

    // VMA destroys buffer and frees memory in one call
    vmaDestroyBuffer(m_ctx->GetAllocator(), m_buffer, m_allocation);

    m_buffer = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
    m_size = 0;
    m_ctx = nullptr;
    m_deviceLocal = false;
}

void* VulkanBuffer::Map() {
    if (!m_ctx || m_buffer == VK_NULL_HANDLE || m_mapped) {
        return m_mapped;
    }

    if (vmaMapMemory(m_ctx->GetAllocator(), m_allocation, &m_mapped) != VK_SUCCESS) {
        m_mapped = nullptr;
    }

    return m_mapped;
}

void VulkanBuffer::Unmap() {
    if (!m_ctx || !m_mapped) {
        return;
    }

    vmaUnmapMemory(m_ctx->GetAllocator(), m_allocation);
    m_mapped = nullptr;
}
