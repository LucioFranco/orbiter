// ==============================================================
// VulkanMesh.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "VulkanMesh.h"
#include "Core/StagingManager.h"
#include <cstring>

// ======================================================================
// MeshVertex static methods
// ======================================================================

VkVertexInputBindingDescription MeshVertex::GetBindingDescription()
{
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(MeshVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::vector<VkVertexInputAttributeDescription> MeshVertex::GetAttributeDescriptions()
{
    std::vector<VkVertexInputAttributeDescription> attrs(3);

    // Position (location 0)
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(MeshVertex, x);

    // Normal (location 1)
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(MeshVertex, nx);

    // Texture coordinates (location 2)
    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(MeshVertex, tu);

    return attrs;
}

// ======================================================================
// VulkanMesh implementation
// ======================================================================

VulkanMesh::VulkanMesh()
    : m_initialized(false)
    , m_uploaded(false)
    , m_ctx(nullptr)
    , m_staging(nullptr)
    , m_totalVertices(0)
    , m_totalIndices(0)
{
}

VulkanMesh::~VulkanMesh()
{
    Shutdown();
}

bool VulkanMesh::Init(VulkanContext* ctx, StagingManager* staging)
{
    if (m_initialized) return true;
    if (!ctx || !staging) return false;

    m_ctx = ctx;
    m_staging = staging;
    m_initialized = true;
    m_uploaded = false;
    m_totalVertices = 0;
    m_totalIndices = 0;
    m_vertices.clear();
    m_indices.clear();
    m_groups.clear();

    return true;
}

void VulkanMesh::Shutdown()
{
    if (!m_initialized) return;

    m_vertexBuffer.Destroy();
    m_indexBuffer.Destroy();

    m_vertices.clear();
    m_indices.clear();
    m_groups.clear();
    m_materials.clear();

    m_initialized = false;
    m_uploaded = false;
    m_totalVertices = 0;
    m_totalIndices = 0;
    m_ctx = nullptr;
    m_staging = nullptr;
}

int VulkanMesh::AddGroup(const MeshVertex* vtx, uint32_t nVtx,
                         const uint16_t* idx, uint32_t nIdx,
                         uint32_t mtrlIdx, uint32_t texIdx,
                         uint32_t usrFlag, uint16_t zBias, uint16_t flags)
{
    if (!m_initialized || m_uploaded) return -1;
    if (!vtx || nVtx == 0 || !idx || nIdx == 0) return -1;

    // Create group record
    MeshGroup group = {};
    group.vertexOffset = m_totalVertices;
    group.indexOffset = m_totalIndices;
    group.vertexCount = nVtx;
    group.indexCount = nIdx;
    group.materialIdx = mtrlIdx;
    group.textureIdx = texIdx;
    group.usrFlag = usrFlag;
    group.zBias = zBias;
    group.flags = flags;

    // Reserve space
    m_vertices.reserve(m_vertices.size() + nVtx);
    m_indices.reserve(m_indices.size() + nIdx);

    // Copy vertices directly (caller has already converted from NTVERTEX if needed)
    for (uint32_t i = 0; i < nVtx; i++) {
        m_vertices.push_back(vtx[i]);
    }

    // Copy indices
    // Note: Indices are relative to the start of this group's vertices
    for (uint32_t i = 0; i < nIdx; i++) {
        // Validate index
        uint16_t index = idx[i];
        if (index >= nVtx) {
            index = 0;  // Clamp invalid indices (D3D9Client does this)
        }
        m_indices.push_back(index);
    }

    m_totalVertices += nVtx;
    m_totalIndices += nIdx;

    int groupIdx = static_cast<int>(m_groups.size());
    m_groups.push_back(group);

    return groupIdx;
}

bool VulkanMesh::Upload()
{
    if (!m_initialized || m_uploaded) return false;
    if (m_vertices.empty() || m_indices.empty()) return false;

    // Create vertex buffer
    VkDeviceSize vertexSize = m_vertices.size() * sizeof(MeshVertex);
    if (!m_vertexBuffer.CreateDeviceLocal(m_ctx, m_staging, vertexSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vertices.data())) {
        return false;
    }

    // Create index buffer
    VkDeviceSize indexSize = m_indices.size() * sizeof(uint16_t);
    if (!m_indexBuffer.CreateDeviceLocal(m_ctx, m_staging, indexSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_indices.data())) {
        m_vertexBuffer.Destroy();
        return false;
    }

    // Clear CPU-side data to free memory
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();

    m_uploaded = true;
    return true;
}

const MeshGroup* VulkanMesh::GetGroup(uint32_t idx) const
{
    if (idx >= m_groups.size()) return nullptr;
    return &m_groups[idx];
}

void VulkanMesh::Bind(VkCommandBuffer cmd) const
{
    if (!m_uploaded) return;

    VkBuffer vertexBuffers[] = { m_vertexBuffer.GetBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT16);
}

void VulkanMesh::DrawGroup(VkCommandBuffer cmd, uint32_t groupIdx) const
{
    if (!m_uploaded || groupIdx >= m_groups.size()) return;

    const MeshGroup& grp = m_groups[groupIdx];

    // Draw indexed with the group's offset and count
    // firstIndex: offset into index buffer
    // vertexOffset: added to each index value before indexing into vertex buffer
    vkCmdDrawIndexed(cmd, grp.indexCount, 1,
                     grp.indexOffset, static_cast<int32_t>(grp.vertexOffset), 0);
}

void VulkanMesh::DrawAll(VkCommandBuffer cmd) const
{
    if (!m_uploaded) return;

    for (uint32_t i = 0; i < m_groups.size(); i++) {
        DrawGroup(cmd, i);
    }
}

// ======================================================================
// Material management
// ======================================================================

void VulkanMesh::AddMaterial(const MeshMaterial& mat)
{
    m_materials.push_back(mat);
}

const MeshMaterial* VulkanMesh::GetMaterial(uint32_t idx) const
{
    if (idx >= m_materials.size()) return nullptr;
    return &m_materials[idx];
}
