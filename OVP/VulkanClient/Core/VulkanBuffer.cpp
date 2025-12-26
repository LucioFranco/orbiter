// ==============================================================
// VulkanBuffer.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "VulkanBuffer.h"

VulkanBuffer::VulkanBuffer()
    : m_ctx(nullptr)
    , m_buffer(VK_NULL_HANDLE)
    , m_memory(VK_NULL_HANDLE)
    , m_size(0)
    , m_mapped(nullptr)
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
    VkDevice device = ctx->GetDevice();

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
        return false;
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, m_buffer, &memRequirements);

    // Find suitable memory type
    uint32_t memoryTypeIndex = ctx->FindMemoryType(memRequirements.memoryTypeBits, properties);
    if (memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
        return false;
    }

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
        return false;
    }

    // Bind buffer to memory
    if (vkBindBufferMemory(device, m_buffer, m_memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, m_memory, nullptr);
        vkDestroyBuffer(device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
        m_memory = VK_NULL_HANDLE;
        return false;
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

    VkDevice device = m_ctx->GetDevice();

    vkDestroyBuffer(device, m_buffer, nullptr);
    vkFreeMemory(device, m_memory, nullptr);

    m_buffer = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    m_size = 0;
    m_ctx = nullptr;
}

void* VulkanBuffer::Map() {
    if (!m_ctx || m_buffer == VK_NULL_HANDLE || m_mapped) {
        return m_mapped;
    }

    if (vkMapMemory(m_ctx->GetDevice(), m_memory, 0, m_size, 0, &m_mapped) != VK_SUCCESS) {
        m_mapped = nullptr;
    }

    return m_mapped;
}

void VulkanBuffer::Unmap() {
    if (!m_ctx || !m_mapped) {
        return;
    }

    vkUnmapMemory(m_ctx->GetDevice(), m_memory);
    m_mapped = nullptr;
}
