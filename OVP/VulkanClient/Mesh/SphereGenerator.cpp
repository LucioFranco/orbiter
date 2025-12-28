// ==============================================================
// SphereGenerator.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// SphereGenerator - Procedural UV sphere mesh generation.
// Algorithm based on D3D9Client's CreateSphere() in Spherepatch.cpp.
// ==============================================================

#include "SphereGenerator.h"
#include <cmath>

#ifndef PI
#define PI 3.14159265358979323846
#endif

SphereGeometry GenerateSphere(uint32_t rings, uint32_t sectors)
{
    SphereGeometry geom;

    // Angular step sizes
    // dTheta: latitude step (0 to PI from north to south pole)
    // dPhi: longitude step (0 to 2*PI around the sphere)
    const float dTheta = static_cast<float>(PI) / static_cast<float>(rings);
    const float dPhi = static_cast<float>(2.0 * PI) / static_cast<float>(sectors * 2);

    // Number of vertices per row (sectors * 2 + 1 for wraparound)
    const uint32_t vertsPerRow = sectors * 2 + 1;

    // Generate vertices
    // Y axis is up, sphere ranges from y=1 (north pole) to y=-1 (south pole)
    for (uint32_t y = 0; y <= rings; y++) {
        float theta = y * dTheta;  // 0 at north pole, PI at south pole
        float sinTheta = sinf(theta);
        float cosTheta = cosf(theta);

        // V coordinate: 0 at north pole, 1 at south pole
        float tv = theta / static_cast<float>(PI);

        for (uint32_t x = 0; x <= sectors * 2; x++) {
            float phi = x * dPhi - static_cast<float>(PI);  // -PI to +PI

            MeshVertex v;

            // Position on unit sphere
            v.x = sinTheta * cosf(phi);
            v.y = cosTheta;
            v.z = sinTheta * sinf(phi);

            // Normal = position for unit sphere (outward facing)
            v.nx = v.x;
            v.ny = v.y;
            v.nz = v.z;

            // UV coordinates (equirectangular projection)
            // U: 0 at phi=-PI, 1 at phi=+PI
            v.tu = (phi + static_cast<float>(PI)) / (2.0f * static_cast<float>(PI));
            v.tv = tv;

            geom.vertices.push_back(v);
        }
    }

    // Generate indices for triangle strips converted to triangles
    // Each quad between (y, x) and (y+1, x+1) becomes 2 triangles
    for (uint32_t y = 0; y < rings; y++) {
        for (uint32_t x = 0; x < sectors * 2; x++) {
            uint32_t topLeft = y * vertsPerRow + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (y + 1) * vertsPerRow + x;
            uint32_t bottomRight = bottomLeft + 1;

            // First triangle (top-left, bottom-left, top-right)
            geom.indices.push_back(static_cast<uint16_t>(topLeft));
            geom.indices.push_back(static_cast<uint16_t>(bottomLeft));
            geom.indices.push_back(static_cast<uint16_t>(topRight));

            // Second triangle (top-right, bottom-left, bottom-right)
            geom.indices.push_back(static_cast<uint16_t>(topRight));
            geom.indices.push_back(static_cast<uint16_t>(bottomLeft));
            geom.indices.push_back(static_cast<uint16_t>(bottomRight));
        }
    }

    return geom;
}
