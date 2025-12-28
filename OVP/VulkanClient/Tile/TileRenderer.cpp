// ==============================================================
// TileRenderer.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "TileRenderer.h"
#include <iostream>
#include <cstring>

TileRenderer::TileRenderer()
    : m_ctx(nullptr)
    , m_staging(nullptr)
    , m_meshPipeline(nullptr)
    , m_initialized(false)
    , m_descriptorPool(VK_NULL_HANDLE)
{
}

TileRenderer::~TileRenderer() {
    Shutdown();
}

bool TileRenderer::Init(VulkanContext* ctx, StagingManager* staging, MeshPipeline* meshPipeline) {
    if (m_initialized) {
        return true;
    }

    if (!ctx || !staging || !meshPipeline) {
        return false;
    }

    m_ctx = ctx;
    m_staging = staging;
    m_meshPipeline = meshPipeline;

    // Initialize texture loader
    if (!m_textureLoader.Init(ctx, staging)) {
        return false;
    }

    // Create descriptor pool for tile textures
    // Allow up to 1000 textures (more than enough for any LOD level)
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1000;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(m_ctx->GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        m_textureLoader.Shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

void TileRenderer::Shutdown() {
    if (!m_initialized) {
        return;
    }

    ClearTiles();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_ctx->GetDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    m_textureLoader.Shutdown();

    m_ctx = nullptr;
    m_staging = nullptr;
    m_meshPipeline = nullptr;
    m_initialized = false;
}

void TileRenderer::SetTextureRoot(const std::string& root) {
    m_textureLoader.SetTextureRoot(root);
}

int TileRenderer::LoadPlanetTiles(const std::string& planetName, int level,
                                   double planetRadius, int gridRes) {
    if (!m_initialized) {
        return 0;
    }

    // Clear existing tiles
    ClearTiles();

    int numLat = TileCoord::NumLatTiles(level);
    int numLng = TileCoord::NumLngTiles(level);
    int loaded = 0;

    for (int ilat = 0; ilat < numLat; ilat++) {
        for (int ilng = 0; ilng < numLng; ilng++) {
            TileKey key(level, ilat, ilng);

            // Generate patch mesh
            PatchMeshData patchData = PatchMeshGenerator::Generate(key, planetRadius, gridRes);

            // Create VulkanMesh
            auto mesh = std::make_unique<VulkanMesh>();
            if (!mesh->Init(m_ctx, m_staging)) {
                continue;
            }

            if (mesh->AddGroup(patchData.vertices.data(),
                               static_cast<uint32_t>(patchData.vertices.size()),
                               patchData.indices.data(),
                               static_cast<uint32_t>(patchData.indices.size())) < 0) {
                continue;
            }

            if (!mesh->Upload()) {
                continue;
            }

            // Try to load texture (may fail if not available)
            VulkanTexture* texture = m_textureLoader.LoadTileTexture(planetName, key);

            // Create descriptor set if texture is available
            VkDescriptorSet descriptor = VK_NULL_HANDLE;
            if (texture) {
                descriptor = CreateTextureDescriptor(texture);
            }

            // Create rendered tile
            auto tile = std::make_unique<RenderedTile>();
            tile->key = key;
            tile->mesh = std::move(mesh);
            tile->texture = texture;
            tile->descriptorSet = descriptor;
            tile->valid = true;
            tile->boundingSphereX = patchData.boundingSphereX;
            tile->boundingSphereY = patchData.boundingSphereY;
            tile->boundingSphereZ = patchData.boundingSphereZ;
            tile->boundingSphereRadius = patchData.boundingSphereRadius;

            m_tiles.push_back(std::move(tile));
            loaded++;
        }
    }

    return loaded;
}

void TileRenderer::Render(VkCommandBuffer cmd, const float* mvp, const float* model,
                           const float* lightDir) {
    // Default white material
    float defaultDiffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float defaultEmissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Render(cmd, mvp, model, lightDir, defaultDiffuse, defaultEmissive);
}

void TileRenderer::Render(VkCommandBuffer cmd, const float* mvp, const float* model,
                           const float* lightDir, const float* diffuseColor,
                           const float* emissiveColor) {
    if (!m_initialized || !m_meshPipeline) {
        return;
    }

    // Bind the mesh pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipeline->GetPipeline());

    // Set up push constants (same for all tiles since they share the same world transform)
    MeshPushConstants pushConstants;
    memcpy(pushConstants.mvp, mvp, sizeof(float) * 16);
    memcpy(pushConstants.model, model, sizeof(float) * 16);
    memcpy(pushConstants.lightDir, lightDir, sizeof(float) * 4);
    memcpy(pushConstants.matDiffuse, diffuseColor, sizeof(float) * 4);
    memcpy(pushConstants.matEmissive, emissiveColor, sizeof(float) * 4);

    vkCmdPushConstants(cmd, m_meshPipeline->GetLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(MeshPushConstants), &pushConstants);

    for (const auto& tile : m_tiles) {
        if (!tile->valid || !tile->mesh) {
            continue;
        }

        // Bind texture descriptor set
        VkDescriptorSet texDesc = tile->descriptorSet;
        if (texDesc == VK_NULL_HANDLE) {
            texDesc = m_meshPipeline->GetDefaultTextureDescriptor();
        }
        if (texDesc != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_meshPipeline->GetLayout(), 0, 1, &texDesc, 0, nullptr);
        }

        // Bind and draw mesh
        tile->mesh->Bind(cmd);
        tile->mesh->DrawAll(cmd);
    }
}

size_t TileRenderer::GetTotalVertexCount() const {
    size_t total = 0;
    for (const auto& tile : m_tiles) {
        if (tile->mesh) {
            total += tile->mesh->GetTotalVertexCount();
        }
    }
    return total;
}

size_t TileRenderer::GetTotalTriangleCount() const {
    size_t total = 0;
    for (const auto& tile : m_tiles) {
        if (tile->mesh) {
            total += tile->mesh->GetTotalIndexCount() / 3;
        }
    }
    return total;
}

void TileRenderer::ClearTiles() {
    // Reset descriptor pool (frees all descriptor sets)
    if (m_descriptorPool != VK_NULL_HANDLE && m_ctx) {
        vkResetDescriptorPool(m_ctx->GetDevice(), m_descriptorPool, 0);
    }

    // Clear tiles (meshes will be destroyed via unique_ptr)
    m_tiles.clear();

    // Clear texture cache
    m_textureLoader.ClearCache();
}

VkDescriptorSet TileRenderer::CreateTextureDescriptor(VulkanTexture* texture) {
    if (!texture || !m_meshPipeline) {
        return VK_NULL_HANDLE;
    }

    // Get the texture descriptor layout from the mesh pipeline
    VkDescriptorSetLayout layout = m_meshPipeline->GetDescriptorLayout();
    if (layout == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(m_ctx->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    // Write texture to descriptor
    VkDescriptorImageInfo imageInfo = texture->GetDescriptorInfo();

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_ctx->GetDevice(), 1, &descriptorWrite, 0, nullptr);

    return descriptorSet;
}
