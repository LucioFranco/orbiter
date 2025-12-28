// ==============================================================
// StagingManager.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "StagingManager.h"
#include <cstring>
#include <algorithm>

StagingManager::StagingManager()
    : m_initialized(false)
    , m_ctx(nullptr)
    , m_stagingBuffer(VK_NULL_HANDLE)
    , m_stagingAllocation(VK_NULL_HANDLE)
    , m_stagingSize(0)
    , m_stagingOffset(0)
    , m_mappedData(nullptr)
    , m_commandBuffer(VK_NULL_HANDLE)
    , m_transferFence(VK_NULL_HANDLE)
    , m_transferInProgress(false)
{
}

StagingManager::~StagingManager()
{
    Shutdown();
}

bool StagingManager::Init(VulkanContext* ctx, VkDeviceSize stagingSize)
{
    if (m_initialized) {
        return false;  // Already initialized
    }
    if (!ctx || !ctx->IsInitialized()) {
        return false;
    }

    m_ctx = ctx;
    m_stagingSize = stagingSize;

    // Create staging buffer (HOST_VISIBLE for CPU writes)
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateBuffer(ctx->GetAllocator(), &bufferInfo, &allocInfo,
                        &m_stagingBuffer, &m_stagingAllocation, &allocationInfo) != VK_SUCCESS) {
        return false;
    }

    // Get persistently mapped pointer
    m_mappedData = allocationInfo.pMappedData;
    if (!m_mappedData) {
        vmaDestroyBuffer(ctx->GetAllocator(), m_stagingBuffer, m_stagingAllocation);
        m_stagingBuffer = VK_NULL_HANDLE;
        return false;
    }

    // Allocate command buffer from context's command pool
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = ctx->GetCommandPool();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(ctx->GetDevice(), &cmdAllocInfo, &m_commandBuffer) != VK_SUCCESS) {
        vmaDestroyBuffer(ctx->GetAllocator(), m_stagingBuffer, m_stagingAllocation);
        m_stagingBuffer = VK_NULL_HANDLE;
        return false;
    }

    // Create fence for synchronization
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;  // Start unsignaled

    if (vkCreateFence(ctx->GetDevice(), &fenceInfo, nullptr, &m_transferFence) != VK_SUCCESS) {
        vkFreeCommandBuffers(ctx->GetDevice(), ctx->GetCommandPool(), 1, &m_commandBuffer);
        vmaDestroyBuffer(ctx->GetAllocator(), m_stagingBuffer, m_stagingAllocation);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_commandBuffer = VK_NULL_HANDLE;
        return false;
    }

    m_initialized = true;
    return true;
}

void StagingManager::Shutdown()
{
    if (!m_initialized || !m_ctx) {
        return;
    }

    // Wait for any pending transfers
    if (m_transferInProgress) {
        Flush();
    }

    VkDevice device = m_ctx->GetDevice();

    // Destroy fence
    if (m_transferFence != VK_NULL_HANDLE) {
        vkDestroyFence(device, m_transferFence, nullptr);
        m_transferFence = VK_NULL_HANDLE;
    }

    // Free command buffer
    if (m_commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, m_ctx->GetCommandPool(), 1, &m_commandBuffer);
        m_commandBuffer = VK_NULL_HANDLE;
    }

    // Destroy staging buffer (VMA handles unmapping)
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_ctx->GetAllocator(), m_stagingBuffer, m_stagingAllocation);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingAllocation = VK_NULL_HANDLE;
    }

    m_mappedData = nullptr;
    m_stagingSize = 0;
    m_stagingOffset = 0;
    m_ctx = nullptr;
    m_initialized = false;
}

void StagingManager::BeginTransfer()
{
    // Reset command buffer and begin recording
    vkResetCommandBuffer(m_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    // Reset staging offset for new batch of uploads
    m_stagingOffset = 0;
}

void StagingManager::EndTransfer()
{
    vkEndCommandBuffer(m_commandBuffer);

    // Submit to graphics queue (which supports transfer operations)
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    vkQueueSubmit(m_ctx->GetGraphicsQueue(), 1, &submitInfo, m_transferFence);
    m_transferInProgress = true;
}

bool StagingManager::UploadBuffer(VkBuffer dstBuffer, const void* data, VkDeviceSize size, VkDeviceSize dstOffset)
{
    if (!m_initialized || !data || size == 0) {
        return false;
    }

    // Check if data fits in remaining staging space
    if (m_stagingOffset + size > m_stagingSize) {
        // Flush current batch and start new one
        if (m_transferInProgress) {
            Flush();
        }
        BeginTransfer();

        // If single upload is larger than staging buffer, fail
        if (size > m_stagingSize) {
            return false;
        }
    }

    // Start recording if this is the first upload in a batch
    if (!m_transferInProgress && m_stagingOffset == 0) {
        BeginTransfer();
    }

    // Copy data to staging buffer
    memcpy(static_cast<char*>(m_mappedData) + m_stagingOffset, data, size);

    // Record copy command
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = m_stagingOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;

    vkCmdCopyBuffer(m_commandBuffer, m_stagingBuffer, dstBuffer, 1, &copyRegion);

    m_stagingOffset += size;

    // Align to 16 bytes for next upload
    m_stagingOffset = (m_stagingOffset + 15) & ~15;

    return true;
}

bool StagingManager::UploadImage(VkImage dstImage, const void* data, uint32_t width, uint32_t height,
                                  VkFormat format, VkImageLayout finalLayout)
{
    if (!m_initialized || !data || width == 0 || height == 0) {
        return false;
    }

    // Calculate size based on format
    VkDeviceSize imageSize = 0;

    // BC (block compressed) formats: size is based on 4x4 blocks
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: {
            // BC1: 8 bytes per 4x4 block (0.5 bytes per pixel)
            uint32_t blocksX = (width + 3) / 4;
            uint32_t blocksY = (height + 3) / 4;
            imageSize = static_cast<VkDeviceSize>(blocksX) * blocksY * 8;
            break;
        }
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK: {
            // BC2/BC3: 16 bytes per 4x4 block (1 byte per pixel)
            uint32_t blocksX = (width + 3) / 4;
            uint32_t blocksY = (height + 3) / 4;
            imageSize = static_cast<VkDeviceSize>(blocksX) * blocksY * 16;
            break;
        }
        default: {
            // Uncompressed formats: size based on bytes per pixel
            VkDeviceSize bytesPerPixel = 4;  // Default: RGBA8
            if (format == VK_FORMAT_R8_UNORM) {
                bytesPerPixel = 1;
            } else if (format == VK_FORMAT_R8G8_UNORM) {
                bytesPerPixel = 2;
            } else if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
                bytesPerPixel = 8;
            } else if (format == VK_FORMAT_R32G32B32A32_SFLOAT) {
                bytesPerPixel = 16;
            }
            imageSize = width * height * bytesPerPixel;
            break;
        }
    }

    // Check if data fits in remaining staging space
    if (m_stagingOffset + imageSize > m_stagingSize) {
        if (m_transferInProgress) {
            Flush();
        }
        BeginTransfer();

        if (imageSize > m_stagingSize) {
            return false;
        }
    }

    // Start recording if needed
    if (!m_transferInProgress && m_stagingOffset == 0) {
        BeginTransfer();
    }

    // Copy pixel data to staging buffer
    memcpy(static_cast<char*>(m_mappedData) + m_stagingOffset, data, imageSize);

    // Transition image to TRANSFER_DST_OPTIMAL and copy
    // NOTE: Disabled sync2 path - vkCmdPipelineBarrier2 crashes on some systems
    // even when the extension is reported as supported. Using legacy barrier path.
    if (false && m_ctx->HasSynchronization2()) {
        // Pre-copy barrier: undefined -> transfer dst
        VkImageMemoryBarrier2 barrier2{};
        barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier2.srcStageMask = VK_PIPELINE_STAGE_2_NONE;  // No prior work
        barrier2.srcAccessMask = VK_ACCESS_2_NONE;
        barrier2.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier2.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier2.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.image = dstImage;
        barrier2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier2.subresourceRange.baseMipLevel = 0;
        barrier2.subresourceRange.levelCount = 1;
        barrier2.subresourceRange.baseArrayLayer = 0;
        barrier2.subresourceRange.layerCount = 1;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier2;

        vkCmdPipelineBarrier2(m_commandBuffer, &depInfo);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = m_stagingOffset;
        region.bufferRowLength = 0;    // Tightly packed
        region.bufferImageHeight = 0;  // Tightly packed
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(m_commandBuffer, m_stagingBuffer, dstImage,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Post-copy barrier: transfer dst -> final layout
        barrier2.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier2.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier2.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier2.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;  // More specific
        barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier2.newLayout = finalLayout;

        vkCmdPipelineBarrier2(m_commandBuffer, &depInfo);
    } else {
        // Legacy barrier path
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = dstImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(m_commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = m_stagingOffset;
        region.bufferRowLength = 0;    // Tightly packed
        region.bufferImageHeight = 0;  // Tightly packed
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(m_commandBuffer, m_stagingBuffer, dstImage,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition to final layout
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = finalLayout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(m_commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    m_stagingOffset += imageSize;
    m_stagingOffset = (m_stagingOffset + 15) & ~15;  // Align

    return true;
}

void StagingManager::Flush()
{
    if (!m_initialized) {
        return;
    }

    // If we have pending commands but haven't submitted yet
    if (m_stagingOffset > 0 && !m_transferInProgress) {
        EndTransfer();
    }

    // Wait for transfer to complete
    if (m_transferInProgress) {
        vkWaitForFences(m_ctx->GetDevice(), 1, &m_transferFence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_ctx->GetDevice(), 1, &m_transferFence);
        m_transferInProgress = false;
    }

    m_stagingOffset = 0;
}
