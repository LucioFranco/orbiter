// ==============================================================
// TileTextureLoader.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifndef TILETEXTURELOADER_H
#define TILETEXTURELOADER_H

#include "TileCoord.h"
#include "TreeArchive.h"
#include "../Core/DDSLoader.h"
#include "../Core/VulkanTexture.h"
#include "../Core/VulkanContext.h"
#include "../Core/StagingManager.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

// =======================================================================
// LoadedTileTexture - Result of loading a tile texture
// =======================================================================
struct LoadedTileTexture {
    std::unique_ptr<VulkanTexture> texture;
    uint32_t width;
    uint32_t height;
    VkFormat format;
    bool valid;

    LoadedTileTexture() : width(0), height(0), format(VK_FORMAT_UNDEFINED), valid(false) {}
};

// =======================================================================
// TileTextureLoader - Loads tile textures from Orbiter texture directories
// =======================================================================
class TileTextureLoader {
public:
    TileTextureLoader();
    ~TileTextureLoader();

    // Initialize the loader with Vulkan context
    bool Init(VulkanContext* ctx, StagingManager* staging);

    // Shutdown and release all cached textures
    void Shutdown();

    // Set the texture root directory (e.g., "C:\Orbiter\Textures")
    void SetTextureRoot(const std::string& root);

    // Get current texture root
    const std::string& GetTextureRoot() const { return m_textureRoot; }

    // Load a tile texture (returns nullptr if not found)
    // The returned texture is owned by the loader (do not delete)
    VulkanTexture* LoadTileTexture(const std::string& planetName,
                                    int level, int ilat, int ilng);

    // Load a tile texture by key (convenience)
    VulkanTexture* LoadTileTexture(const std::string& planetName, const TileKey& tile) {
        return LoadTileTexture(planetName, tile.level, tile.ilat, tile.ilng);
    }

    // Check if a tile texture exists on disk
    bool TileTextureExists(const std::string& planetName,
                           int level, int ilat, int ilng) const;

    // Clear the texture cache (frees all cached textures)
    void ClearCache();

    // Get cache statistics
    size_t GetCacheSize() const { return m_textureCache.size(); }

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

private:
    // Generate cache key from tile info
    std::string MakeCacheKey(const std::string& planetName, int level, int ilat, int ilng) const;

    // Archive support
    TreeArchive* GetOrOpenArchive(const std::string& planetName);
    std::vector<uint8_t> LoadFromArchive(const std::string& planetName,
                                          int level, int ilat, int ilng);

    VulkanContext* m_ctx;
    StagingManager* m_staging;
    std::string m_textureRoot;
    bool m_initialized;

    // Texture cache: key -> loaded texture
    std::unordered_map<std::string, std::unique_ptr<VulkanTexture>> m_textureCache;

    // Archive cache: planetName -> archive
    std::unordered_map<std::string, std::unique_ptr<TreeArchive>> m_archives;
};

#endif // TILETEXTURELOADER_H
