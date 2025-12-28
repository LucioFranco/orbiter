// ==============================================================
// TextureManager.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "TextureManager.h"
#include "Core/StagingManager.h"

TextureManager::TextureManager()
    : m_initialized(false)
    , m_pipeline(nullptr)
    , m_ctx(nullptr)
    , m_staging(nullptr)
    , m_resolver(nullptr)
{
}

TextureManager::~TextureManager()
{
    Shutdown();
}

bool TextureManager::Init(MeshPipeline* pipeline, VulkanContext* ctx, StagingManager* staging,
                          SurfaceResolver resolver)
{
    if (m_initialized) return true;
    if (!pipeline || !ctx) return false;

    m_pipeline = pipeline;
    m_ctx = ctx;
    m_staging = staging;
    m_resolver = resolver;
    m_initialized = true;

    return true;
}

void TextureManager::Shutdown()
{
    if (!m_initialized) return;

    // Clean up cached texture resources
    ClearCache();

    m_pipeline = nullptr;
    m_ctx = nullptr;
    m_staging = nullptr;
    m_initialized = false;
}

VkDescriptorSet TextureManager::GetTextureDescriptor(SURFHANDLE hTex)
{
    if (!m_initialized || !hTex) {
        return GetDefaultDescriptor();
    }

    // Check cache first
    auto it = m_textureCache.find(hTex);
    if (it != m_textureCache.end()) {
        return it->second;
    }

    // Try to load texture
    VkDescriptorSet descriptor = LoadTextureFromSurface(hTex);
    if (descriptor != VK_NULL_HANDLE) {
        m_textureCache[hTex] = descriptor;
        return descriptor;
    }

    // Fall back to default texture
    return GetDefaultDescriptor();
}

VkDescriptorSet TextureManager::GetDefaultDescriptor() const
{
    if (!m_initialized || !m_pipeline) {
        return VK_NULL_HANDLE;
    }
    return m_pipeline->GetDefaultTextureDescriptor();
}

void TextureManager::InvalidateTexture(SURFHANDLE hTex)
{
    if (!m_initialized) return;

    auto cacheIt = m_textureCache.find(hTex);
    if (cacheIt != m_textureCache.end()) {
        m_textureCache.erase(cacheIt);
    }

    auto resIt = m_textureResources.find(hTex);
    if (resIt != m_textureResources.end()) {
        // Clean up Vulkan resources
        VkDevice device = m_ctx->GetDevice();
        VmaAllocator allocator = m_ctx->GetAllocator();
        CachedTexture& tex = resIt->second;

        if (tex.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, tex.view, nullptr);
        }
        if (tex.image != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, tex.image, tex.allocation);
        }
        // Note: Descriptor set is freed when descriptor pool is destroyed

        m_textureResources.erase(resIt);
    }
}

void TextureManager::ClearCache()
{
    if (!m_initialized) return;

    VkDevice device = m_ctx->GetDevice();
    VmaAllocator allocator = m_ctx->GetAllocator();

    for (auto& pair : m_textureResources) {
        CachedTexture& tex = pair.second;
        if (tex.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, tex.view, nullptr);
        }
        if (tex.image != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, tex.image, tex.allocation);
        }
    }

    m_textureResources.clear();
    m_textureCache.clear();
}

VkDescriptorSet TextureManager::LoadTextureFromSurface(SURFHANDLE hTex)
{
    // Use the resolver to get the VulkanTexture from the SURFHANDLE
    if (!m_resolver) {
        return VK_NULL_HANDLE;
    }

    VulkanTexture* texture = m_resolver(hTex);
    if (!texture || !texture->IsValid()) {
        return VK_NULL_HANDLE;
    }

    // Allocate and update descriptor set for this texture
    VkDescriptorSet descriptorSet = m_pipeline->AllocateTextureDescriptor(
        texture->GetImageView(), texture->GetSampler());

    if (descriptorSet == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // Store in resource map for cleanup (descriptor set only, image owned by client)
    CachedTexture cached{};
    cached.image = VK_NULL_HANDLE;      // Owned by VulkanTexture in graphics client
    cached.allocation = VK_NULL_HANDLE;
    cached.view = VK_NULL_HANDLE;
    cached.descriptor = descriptorSet;
    m_textureResources[hTex] = cached;

    return descriptorSet;
}
