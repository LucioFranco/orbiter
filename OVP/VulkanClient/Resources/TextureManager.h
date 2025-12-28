// ==============================================================
// TextureManager.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// TextureManager - Manages texture loading and caching for Vulkan.
// Converts Orbiter SURFHANDLE textures to Vulkan textures.
// ==============================================================

#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include "Core/VulkanContext.h"
#include "Core/MeshPipeline.h"
#include "Core/VulkanTexture.h"
#include <map>
#include <functional>

// Forward declarations
class StagingManager;
typedef void* SURFHANDLE;  // Orbiter's opaque surface handle

// Callback type for resolving SURFHANDLE to VulkanTexture*
// The graphics client provides this to convert its internal handles to textures
using SurfaceResolver = std::function<VulkanTexture*(SURFHANDLE)>;

// ======================================================================
// TextureManager - Texture loading and caching
// ======================================================================
/**
 * \brief Manages texture loading and caching for Vulkan rendering.
 *
 * This class handles:
 * - Converting Orbiter SURFHANDLE textures to Vulkan textures
 * - Caching textures to avoid reloading
 * - Providing a default (white) texture for untextured surfaces
 *
 * Usage:
 * 1. Initialize: textureManager.Init(meshPipeline);
 * 2. Get descriptors: VkDescriptorSet desc = textureManager.GetTextureDescriptor(hTex);
 * 3. Shutdown: textureManager.Shutdown();
 */
class TextureManager {
public:
    TextureManager();
    ~TextureManager();

    // Lifecycle
    bool Init(MeshPipeline* pipeline, VulkanContext* ctx, StagingManager* staging,
              SurfaceResolver resolver = nullptr);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Set the surface resolver (can be set after Init)
    void SetSurfaceResolver(SurfaceResolver resolver) { m_resolver = resolver; }

    /**
     * \brief Get a texture descriptor for the given SURFHANDLE.
     * \param hTex Orbiter surface handle
     * \return Vulkan descriptor set for the texture, or default texture if not found
     *
     * This method caches textures by SURFHANDLE. If the texture hasn't been loaded,
     * it will attempt to load it from the surface. If loading fails, returns the
     * default white texture.
     */
    VkDescriptorSet GetTextureDescriptor(SURFHANDLE hTex);

    /**
     * \brief Get the default (white) texture descriptor.
     * \return Vulkan descriptor set for the default texture
     */
    VkDescriptorSet GetDefaultDescriptor() const;

    /**
     * \brief Invalidate a cached texture (e.g., when surface is modified).
     * \param hTex Orbiter surface handle
     */
    void InvalidateTexture(SURFHANDLE hTex);

    /**
     * \brief Clear all cached textures.
     */
    void ClearCache();

private:
    // Load texture from SURFHANDLE into Vulkan
    VkDescriptorSet LoadTextureFromSurface(SURFHANDLE hTex);

    bool m_initialized;
    MeshPipeline* m_pipeline;
    VulkanContext* m_ctx;
    StagingManager* m_staging;
    SurfaceResolver m_resolver;

    // Texture cache: SURFHANDLE -> VkDescriptorSet
    std::map<SURFHANDLE, VkDescriptorSet> m_textureCache;

    // Cached texture resources (for cleanup)
    struct CachedTexture {
        VkImage image;
        VmaAllocation allocation;
        VkImageView view;
        VkDescriptorSet descriptor;
    };
    std::map<SURFHANDLE, CachedTexture> m_textureResources;
};

#endif // TEXTUREMANAGER_H
