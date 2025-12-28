// ==============================================================
// VulkanMesh.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// VulkanMesh - GPU mesh representation for Orbiter meshes.
// Converts NTVERTEX data from Orbiter's MESHGROUPEX to GPU buffers.
// ==============================================================

#ifndef VULKANMESH_H
#define VULKANMESH_H

#include "Core/VulkanContext.h"
#include "Core/VulkanBuffer.h"
#include <vector>
#include <cstdint>

// Forward declarations
class StagingManager;

// ======================================================================
// MeshVertex - GPU vertex format matching NTVERTEX layout
// ======================================================================
// This matches Orbiter's NTVERTEX structure exactly (32 bytes):
//   float x, y, z;     // position
//   float nx, ny, nz;  // normal
//   float tu, tv;      // texture coordinates
//
struct MeshVertex {
    float x, y, z;      // Position
    float nx, ny, nz;   // Normal
    float tu, tv;       // Texture coordinates

    // Get Vulkan vertex input binding description
    static VkVertexInputBindingDescription GetBindingDescription();

    // Get Vulkan vertex input attribute descriptions
    static std::vector<VkVertexInputAttributeDescription> GetAttributeDescriptions();
};

// Ensure layout matches NTVERTEX exactly
static_assert(sizeof(MeshVertex) == 32, "MeshVertex must be 32 bytes to match NTVERTEX");

// ======================================================================
// MeshMaterial - Material properties from Orbiter's MATERIAL struct
// ======================================================================
struct MeshMaterial {
    float diffuse[4];   // RGBA diffuse color
    float ambient[4];   // RGBA ambient color
    float specular[4];  // RGBA specular color
    float emissive[4];  // RGBA emissive color
    float power;        // Specular power
};

// ======================================================================
// MeshGroup - Per-group rendering data
// ======================================================================
struct MeshGroup {
    uint32_t vertexOffset;  // Offset into vertex buffer
    uint32_t indexOffset;   // Offset into index buffer
    uint32_t vertexCount;   // Number of vertices in this group
    uint32_t indexCount;    // Number of indices (triangles * 3)
    uint32_t materialIdx;   // Material index (0 = none)
    uint32_t textureIdx;    // Texture index (0 = none)
    uint32_t usrFlag;       // User-defined flag
    uint16_t zBias;         // Z bias for rendering
    uint16_t flags;         // Internal flags
};

// ======================================================================
// VulkanMesh - GPU mesh containing one or more groups
// ======================================================================
/**
 * \brief Vulkan mesh class for Orbiter mesh rendering.
 *
 * This class manages GPU buffers for mesh vertex and index data.
 * It converts NTVERTEX arrays from Orbiter's MESHGROUPEX into
 * device-local Vulkan buffers for efficient rendering.
 *
 * Usage:
 * 1. Create mesh: VulkanMesh mesh;
 * 2. Initialize: mesh.Init(ctx, stagingManager);
 * 3. Add groups: mesh.AddGroup(vtx, nVtx, idx, nIdx, mtrlIdx, texIdx);
 * 4. Finalize: mesh.Upload(); // Uploads all data to GPU
 * 5. Render: mesh.Draw(cmd, groupIdx);
 */
class VulkanMesh {
public:
    VulkanMesh();
    ~VulkanMesh();

    // Lifecycle
    bool Init(VulkanContext* ctx, StagingManager* staging);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Building the mesh (call before Upload)
    // Adds a mesh group from MeshVertex data
    // Returns group index on success, -1 on failure
    int AddGroup(const MeshVertex* vtx, uint32_t nVtx,
                 const uint16_t* idx, uint32_t nIdx,
                 uint32_t mtrlIdx = 0, uint32_t texIdx = 0,
                 uint32_t usrFlag = 0, uint16_t zBias = 0, uint16_t flags = 0);

    // Upload all accumulated data to GPU
    // Call this after adding all groups
    bool Upload();

    // Query
    bool IsUploaded() const { return m_uploaded; }
    uint32_t GetGroupCount() const { return static_cast<uint32_t>(m_groups.size()); }
    const MeshGroup* GetGroup(uint32_t idx) const;
    uint32_t GetTotalVertexCount() const { return m_totalVertices; }
    uint32_t GetTotalIndexCount() const { return m_totalIndices; }

    // Material management
    void AddMaterial(const MeshMaterial& mat);
    uint32_t GetMaterialCount() const { return static_cast<uint32_t>(m_materials.size()); }
    const MeshMaterial* GetMaterial(uint32_t idx) const;

    // Rendering
    void Bind(VkCommandBuffer cmd) const;
    void DrawGroup(VkCommandBuffer cmd, uint32_t groupIdx) const;
    void DrawAll(VkCommandBuffer cmd) const;

    // Accessors
    VkBuffer GetVertexBuffer() const { return m_vertexBuffer.GetBuffer(); }
    VkBuffer GetIndexBuffer() const { return m_indexBuffer.GetBuffer(); }

private:
    bool m_initialized;
    bool m_uploaded;
    VulkanContext* m_ctx;
    StagingManager* m_staging;

    // GPU buffers
    VulkanBuffer m_vertexBuffer;
    VulkanBuffer m_indexBuffer;

    // CPU-side data (cleared after upload)
    std::vector<MeshVertex> m_vertices;
    std::vector<uint16_t> m_indices;

    // Group definitions
    std::vector<MeshGroup> m_groups;

    // Material definitions
    std::vector<MeshMaterial> m_materials;

    // Totals
    uint32_t m_totalVertices;
    uint32_t m_totalIndices;
};

#endif // VULKANMESH_H
