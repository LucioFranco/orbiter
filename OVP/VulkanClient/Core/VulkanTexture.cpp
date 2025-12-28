// ==============================================================
// VulkanTexture.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "VulkanTexture.h"
#include "StagingManager.h"
#include "DDSLoader.h"

// stb_image for file loading (implementation in a single compilation unit)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

VulkanTexture::VulkanTexture()
    : m_ctx(nullptr)
    , m_image(VK_NULL_HANDLE)
    , m_allocation(VK_NULL_HANDLE)
    , m_imageView(VK_NULL_HANDLE)
    , m_format(VK_FORMAT_R8G8B8A8_UNORM)
    , m_width(0)
    , m_height(0)
    , m_sampler(VK_NULL_HANDLE)
{
}

VulkanTexture::~VulkanTexture()
{
    Destroy();
}

bool VulkanTexture::CreateFromMemory(VulkanContext* ctx, StagingManager* staging,
                                      const void* pixels, uint32_t width, uint32_t height,
                                      VkFormat format)
{
    if (m_image != VK_NULL_HANDLE) {
        return false;  // Already created
    }
    if (!ctx || !ctx->IsInitialized() || !staging || !staging->IsInitialized()) {
        return false;
    }
    if (!pixels || width == 0 || height == 0) {
        return false;
    }

    m_ctx = ctx;
    m_width = width;
    m_height = height;
    m_format = format;

    // Create image
    if (!CreateImage(width, height, format)) {
        return false;
    }

    // Upload pixel data via staging
    if (!staging->UploadImage(m_image, pixels, width, height, format,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        vmaDestroyImage(m_ctx->GetAllocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        return false;
    }
    staging->Flush();

    // Create image view
    if (!CreateImageView()) {
        vmaDestroyImage(m_ctx->GetAllocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        return false;
    }

    // Create sampler
    if (!CreateSampler()) {
        vkDestroyImageView(m_ctx->GetDevice(), m_imageView, nullptr);
        vmaDestroyImage(m_ctx->GetAllocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_imageView = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanTexture::CreateFromFile(VulkanContext* ctx, StagingManager* staging, const char* path)
{
    if (!path) {
        return false;
    }

    // Check if it's a DDS file
    if (DDSLoader::IsDDSFile(path)) {
        DDSImage dds;
        if (!DDSLoader::Load(path, dds)) {
            return false;
        }

        // Create texture from DDS data
        return CreateFromMemory(ctx, staging, dds.data.data(),
                                dds.width, dds.height, dds.format);
    }

    // Load image using stb_image (PNG, JPG, BMP, etc.)
    int width, height, channels;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        return false;
    }

    // Create texture from the loaded pixels
    bool result = CreateFromMemory(ctx, staging, pixels,
                                    static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height),
                                    VK_FORMAT_R8G8B8A8_UNORM);

    stbi_image_free(pixels);
    return result;
}

void VulkanTexture::Destroy()
{
    if (!m_ctx) {
        return;
    }

    VkDevice device = m_ctx->GetDevice();
    VmaAllocator allocator = m_ctx->GetAllocator();

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }

    if (m_image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    m_ctx = nullptr;
    m_width = 0;
    m_height = 0;
}

bool VulkanTexture::CreateImage(uint32_t width, uint32_t height, VkFormat format)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return vmaCreateImage(m_ctx->GetAllocator(), &imageInfo, &allocInfo,
                          &m_image, &m_allocation, nullptr) == VK_SUCCESS;
}

bool VulkanTexture::CreateImageView()
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    return vkCreateImageView(m_ctx->GetDevice(), &viewInfo, nullptr, &m_imageView) == VK_SUCCESS;
}

bool VulkanTexture::CreateSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    return vkCreateSampler(m_ctx->GetDevice(), &samplerInfo, nullptr, &m_sampler) == VK_SUCCESS;
}

VkDescriptorImageInfo VulkanTexture::GetDescriptorInfo() const
{
    VkDescriptorImageInfo info{};
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView = m_imageView;
    info.sampler = m_sampler;
    return info;
}
