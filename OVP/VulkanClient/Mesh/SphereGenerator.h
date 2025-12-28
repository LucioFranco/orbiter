// ==============================================================
// SphereGenerator.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// SphereGenerator - Procedural UV sphere mesh generation.
// Creates sphere geometry compatible with VulkanMesh for planet rendering.
// ==============================================================

#ifndef SPHEREGENERATOR_H
#define SPHEREGENERATOR_H

#include "VulkanMesh.h"
#include <vector>
#include <cstdint>

// ======================================================================
// SphereGeometry - Generated sphere mesh data
// ======================================================================
struct SphereGeometry {
    std::vector<MeshVertex> vertices;
    std::vector<uint16_t> indices;
};

// ======================================================================
// GenerateSphere - Create a UV sphere with the given resolution
// ======================================================================
/**
 * \brief Generate a UV sphere mesh centered at origin with radius 1.0.
 *
 * Creates a sphere with the specified number of horizontal rings and
 * vertical sectors. The algorithm matches D3D9Client's CreateSphere()
 * from Spherepatch.cpp.
 *
 * \param rings Number of horizontal divisions (poles to equator).
 *              Typical values: 6, 8, 12, 16 for increasing detail.
 * \param sectors Number of vertical divisions around the sphere.
 *                Typically 2x the number of rings.
 * \return SphereGeometry containing vertices and indices for the mesh.
 *
 * Vertex layout (MeshVertex, 32 bytes):
 *   - Position: normalized sphere surface point
 *   - Normal: same as position (outward facing)
 *   - UV: equirectangular mapping (u=longitude, v=latitude)
 *
 * UV mapping:
 *   - u: 0.0 at -180 degrees, 1.0 at +180 degrees
 *   - v: 0.0 at north pole, 1.0 at south pole
 *
 * Triangle count: approximately 2 * rings * sectors * 2
 */
SphereGeometry GenerateSphere(uint32_t rings, uint32_t sectors);

#endif // SPHEREGENERATOR_H
