// ==============================================================
// PatchMeshGenerator.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifndef PATCHMESHGENERATOR_H
#define PATCHMESHGENERATOR_H

#include "TileCoord.h"
#include "../Mesh/VulkanMesh.h"
#include <vector>
#include <cmath>

// =======================================================================
// PatchMeshData - Raw mesh data for a tile patch
// =======================================================================
struct PatchMeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint16_t> indices;

    // Bounding sphere for frustum culling
    float boundingSphereX, boundingSphereY, boundingSphereZ;
    float boundingSphereRadius;

    size_t GetVertexCount() const { return vertices.size(); }
    size_t GetIndexCount() const { return indices.size(); }
    size_t GetTriangleCount() const { return indices.size() / 3; }
};

// =======================================================================
// PatchMeshGenerator - Generates quadrilateral patches for planetary tiles
// =======================================================================
class PatchMeshGenerator {
public:
    // Default grid resolution (32x32 quads = 33x33 vertices)
    static constexpr int DEFAULT_GRID_RES = 32;

    // Generate a patch mesh for a given tile
    // planetRadius: radius of the planet in meters (for scaling)
    // gridRes: number of quads per side (vertices = gridRes + 1)
    static PatchMeshData Generate(const TileKey& tile,
                                   double planetRadius = 1.0,
                                   int gridRes = DEFAULT_GRID_RES);

    // Generate with custom UV range (for using parent's texture)
    static PatchMeshData Generate(const TileKey& tile,
                                   const TileCoord::TexCoordRange& uvRange,
                                   double planetRadius = 1.0,
                                   int gridRes = DEFAULT_GRID_RES);

private:
    // Internal generation with all parameters
    static PatchMeshData GenerateInternal(int level, int ilat, int ilng,
                                           const TileCoord::TexCoordRange& uvRange,
                                           double planetRadius,
                                           int gridRes);
};

// =======================================================================
// Implementation (inline for header-only)
// =======================================================================

inline PatchMeshData PatchMeshGenerator::Generate(const TileKey& tile,
                                                   double planetRadius,
                                                   int gridRes) {
    TileCoord::TexCoordRange fullRange(0.0f, 1.0f, 0.0f, 1.0f);
    return GenerateInternal(tile.level, tile.ilat, tile.ilng, fullRange, planetRadius, gridRes);
}

inline PatchMeshData PatchMeshGenerator::Generate(const TileKey& tile,
                                                   const TileCoord::TexCoordRange& uvRange,
                                                   double planetRadius,
                                                   int gridRes) {
    return GenerateInternal(tile.level, tile.ilat, tile.ilng, uvRange, planetRadius, gridRes);
}

inline PatchMeshData PatchMeshGenerator::GenerateInternal(int level, int ilat, int ilng,
                                                           const TileCoord::TexCoordRange& uvRange,
                                                           double planetRadius,
                                                           int gridRes) {
    PatchMeshData mesh;

    // Get tile bounds in radians
    TileBounds bounds = TileCoord::GetTileBounds(level, ilat, ilng);

    // Number of vertices: (gridRes+1) x (gridRes+1)
    int nvtxLat = gridRes + 1;
    int nvtxLng = gridRes + 1;
    int totalVtx = nvtxLat * nvtxLng;

    mesh.vertices.resize(totalVtx);

    // UV range (may be sub-range if using parent texture)
    float uMin = uvRange.uMin;
    float uMax = uvRange.uMax;
    float vMin = uvRange.vMin;
    float vMax = uvRange.vMax;

    // Bounding sphere calculation
    double sumX = 0, sumY = 0, sumZ = 0;
    float minX = 1e10f, minY = 1e10f, minZ = 1e10f;
    float maxX = -1e10f, maxY = -1e10f, maxZ = -1e10f;

    // Generate vertices
    int idx = 0;
    for (int i = 0; i < nvtxLat; i++) {
        // Latitude interpolation (from south to north within tile)
        double t_lat = static_cast<double>(i) / static_cast<double>(gridRes);
        double lat = bounds.minLat + t_lat * (bounds.maxLat - bounds.minLat);
        double sinLat = sin(lat);
        double cosLat = cos(lat);

        for (int j = 0; j < nvtxLng; j++) {
            // Longitude interpolation (from west to east within tile)
            double t_lng = static_cast<double>(j) / static_cast<double>(gridRes);
            double lng = bounds.minLng + t_lng * (bounds.maxLng - bounds.minLng);
            double sinLng = sin(lng);
            double cosLng = cos(lng);

            // Position on unit sphere (Y-up coordinate system)
            // x = cos(lat) * cos(lng)
            // y = sin(lat)
            // z = cos(lat) * sin(lng)
            float x = static_cast<float>(cosLat * cosLng * planetRadius);
            float y = static_cast<float>(sinLat * planetRadius);
            float z = static_cast<float>(cosLat * sinLng * planetRadius);

            // Normal = normalized position (for sphere)
            float nx = static_cast<float>(cosLat * cosLng);
            float ny = static_cast<float>(sinLat);
            float nz = static_cast<float>(cosLat * sinLng);

            // Texture coordinates
            // U maps to longitude (0 = west edge, 1 = east edge)
            // V maps to latitude (0 = south edge, 1 = north edge)
            float u = uMin + static_cast<float>(t_lng) * (uMax - uMin);
            float v = vMin + static_cast<float>(t_lat) * (vMax - vMin);

            mesh.vertices[idx] = { x, y, z, nx, ny, nz, u, v };

            // Update bounds for bounding sphere
            sumX += x; sumY += y; sumZ += z;
            if (x < minX) minX = x; if (x > maxX) maxX = x;
            if (y < minY) minY = y; if (y > maxY) maxY = y;
            if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;

            idx++;
        }
    }

    // Bounding sphere: center is centroid, radius is max distance from center
    mesh.boundingSphereX = static_cast<float>(sumX / totalVtx);
    mesh.boundingSphereY = static_cast<float>(sumY / totalVtx);
    mesh.boundingSphereZ = static_cast<float>(sumZ / totalVtx);

    // Calculate max distance from center
    float maxDistSq = 0.0f;
    for (const auto& vtx : mesh.vertices) {
        float dx = vtx.x - mesh.boundingSphereX;
        float dy = vtx.y - mesh.boundingSphereY;
        float dz = vtx.z - mesh.boundingSphereZ;
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > maxDistSq) maxDistSq = distSq;
    }
    mesh.boundingSphereRadius = std::sqrt(maxDistSq);

    // Generate indices for triangles
    // Each quad is 2 triangles (6 indices)
    int numQuads = gridRes * gridRes;
    mesh.indices.resize(numQuads * 6);

    int triIdx = 0;
    for (int i = 0; i < gridRes; i++) {
        for (int j = 0; j < gridRes; j++) {
            // Quad corners:
            // (i,j)     (i,j+1)
            //   +--------+
            //   |      / |
            //   |    /   |
            //   |  /     |
            //   +--------+
            // (i+1,j)   (i+1,j+1)

            uint16_t v00 = static_cast<uint16_t>(i * nvtxLng + j);         // top-left
            uint16_t v01 = static_cast<uint16_t>(i * nvtxLng + j + 1);     // top-right
            uint16_t v10 = static_cast<uint16_t>((i + 1) * nvtxLng + j);   // bottom-left
            uint16_t v11 = static_cast<uint16_t>((i + 1) * nvtxLng + j + 1); // bottom-right

            // Two triangles per quad (CCW winding for front-facing in Vulkan)
            // Triangle 1: v00, v10, v01 (top-left, bottom-left, top-right)
            mesh.indices[triIdx++] = v00;
            mesh.indices[triIdx++] = v10;
            mesh.indices[triIdx++] = v01;

            // Triangle 2: v01, v10, v11 (top-right, bottom-left, bottom-right)
            mesh.indices[triIdx++] = v01;
            mesh.indices[triIdx++] = v10;
            mesh.indices[triIdx++] = v11;
        }
    }

    return mesh;
}

#endif // PATCHMESHGENERATOR_H
