// ==============================================================
// TileRenderer.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifndef TILERENDERER_H
#define TILERENDERER_H

#include "TileCoord.h"
#include "PatchMeshGenerator.h"
#include "TileTextureLoader.h"
#include "../Core/VulkanContext.h"
#include "../Core/StagingManager.h"
#include "../Core/MeshPipeline.h"
#include "../Mesh/VulkanMesh.h"
#include <vector>
#include <memory>
#include <string>

// =======================================================================
// RenderedTile - A tile ready for rendering
// =======================================================================
struct RenderedTile {
    TileKey key;
    std::unique_ptr<VulkanMesh> mesh;
    VulkanTexture* texture;  // Owned by TileTextureLoader
    VkDescriptorSet descriptorSet;
    bool valid;

    // Bounding sphere for frustum culling
    float boundingSphereX, boundingSphereY, boundingSphereZ;
    float boundingSphereRadius;

    RenderedTile() : texture(nullptr), descriptorSet(VK_NULL_HANDLE), valid(false),
                     boundingSphereX(0), boundingSphereY(0), boundingSphereZ(0),
                     boundingSphereRadius(0) {}
};

// =======================================================================
// TileRenderer - Renders planet tiles at a fixed LOD level
// =======================================================================
class TileRenderer {
public:
    TileRenderer();
    ~TileRenderer();

    // Initialize the renderer
    bool Init(VulkanContext* ctx, StagingManager* staging, MeshPipeline* meshPipeline);

    // Shutdown and release resources
    void Shutdown();

    // Set texture root directory
    void SetTextureRoot(const std::string& root);

    // Load all tiles for a planet at a fixed level
    // Returns number of tiles successfully loaded
    int LoadPlanetTiles(const std::string& planetName, int level,
                         double planetRadius = 1.0, int gridRes = 32);

    // Render all loaded tiles
    // mvp: combined model-view-projection matrix (column-major)
    // model: model matrix (column-major)
    // lightDir: light direction (x, y, z, shininess)
    void Render(VkCommandBuffer cmd, const float* mvp, const float* model,
                const float* lightDir);

    // Render with material colors
    void Render(VkCommandBuffer cmd, const float* mvp, const float* model,
                const float* lightDir, const float* diffuseColor, const float* emissiveColor);

    // Get number of loaded tiles
    size_t GetTileCount() const { return m_tiles.size(); }

    // Get total vertex count across all tiles
    size_t GetTotalVertexCount() const;

    // Get total triangle count across all tiles
    size_t GetTotalTriangleCount() const;

    // Check if initialized
    bool IsInitialized() const { return m_initialized; }

    // Clear all loaded tiles
    void ClearTiles();

private:
    // Create descriptor set for a tile texture
    VkDescriptorSet CreateTextureDescriptor(VulkanTexture* texture);

    VulkanContext* m_ctx;
    StagingManager* m_staging;
    MeshPipeline* m_meshPipeline;
    TileTextureLoader m_textureLoader;
    std::vector<std::unique_ptr<RenderedTile>> m_tiles;
    bool m_initialized;

    // Descriptor pool for tile textures
    VkDescriptorPool m_descriptorPool;
};

#endif // TILERENDERER_H
