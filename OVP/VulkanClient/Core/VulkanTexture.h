// ==============================================================
// VulkanTexture.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Texture wrapper for Vulkan images with sampler.
// ==============================================================

#ifndef VULKANTEXTURE_H
#define VULKANTEXTURE_H

#include "VulkanContext.h"

// Forward declaration
class StagingManager;

class VulkanTexture {
public:
    VulkanTexture();
    ~VulkanTexture();

    // Lifecycle
    // Create from raw pixel data (RGBA8 format)
    bool CreateFromMemory(VulkanContext* ctx, StagingManager* staging,
                          const void* pixels, uint32_t width, uint32_t height,
                          VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    // Create from file (uses stb_image internally)
    bool CreateFromFile(VulkanContext* ctx, StagingManager* staging, const char* path);

    void Destroy();
    bool IsValid() const { return m_image != VK_NULL_HANDLE; }

    // Accessors
    VkImage GetImage() const { return m_image; }
    VkImageView GetImageView() const { return m_imageView; }
    VkSampler GetSampler() const { return m_sampler; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    VkFormat GetFormat() const { return m_format; }

    // Get descriptor info for binding to shaders
    VkDescriptorImageInfo GetDescriptorInfo() const;

private:
    bool CreateImage(uint32_t width, uint32_t height, VkFormat format);
    bool CreateImageView();
    bool CreateSampler();

    VulkanContext* m_ctx;

    // Image
    VkImage m_image;
    VmaAllocation m_allocation;
    VkImageView m_imageView;
    VkFormat m_format;
    uint32_t m_width;
    uint32_t m_height;

    // Sampler (shared for simple textures)
    VkSampler m_sampler;
};

#endif // VULKANTEXTURE_H
