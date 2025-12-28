// ==============================================================
// TileCoord.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifndef TILECOORD_H
#define TILECOORD_H

#include <cstdint>
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Tile resolution constant (matches D3D9Client's TILE_FILERES)
static constexpr int TILE_FILERES = 256;

// =======================================================================
// TileKey - Unique identifier for a tile
// =======================================================================
struct TileKey {
    int level;  // LOD level (1-18, where 1 is lowest resolution)
    int ilat;   // Latitude index (0 to nlat-1)
    int ilng;   // Longitude index (0 to nlng-1)

    TileKey() : level(0), ilat(0), ilng(0) {}
    TileKey(int lvl, int lat, int lng) : level(lvl), ilat(lat), ilng(lng) {}

    bool operator==(const TileKey& other) const {
        return level == other.level && ilat == other.ilat && ilng == other.ilng;
    }

    bool operator<(const TileKey& other) const {
        if (level != other.level) return level < other.level;
        if (ilat != other.ilat) return ilat < other.ilat;
        return ilng < other.ilng;
    }
};

// Hash function for TileKey (for use with unordered_map)
struct TileKeyHash {
    size_t operator()(const TileKey& key) const {
        // Combine level, ilat, ilng into a single hash
        return ((size_t)key.level << 20) ^ ((size_t)key.ilat << 10) ^ (size_t)key.ilng;
    }
};

// =======================================================================
// TileBounds - Geographic bounds of a tile in radians
// =======================================================================
struct TileBounds {
    double minLat;  // South edge (radians)
    double maxLat;  // North edge (radians)
    double minLng;  // West edge (radians)
    double maxLng;  // East edge (radians)

    TileBounds() : minLat(0), maxLat(0), minLng(0), maxLng(0) {}
    TileBounds(double minLa, double maxLa, double minLo, double maxLo)
        : minLat(minLa), maxLat(maxLa), minLng(minLo), maxLng(maxLo) {}

    double LatExtent() const { return maxLat - minLat; }
    double LngExtent() const { return maxLng - minLng; }
    double CenterLat() const { return (minLat + maxLat) * 0.5; }
    double CenterLng() const { return (minLng + maxLng) * 0.5; }
};

// =======================================================================
// TileCoord - Static utility functions for tile coordinate calculations
// =======================================================================
class TileCoord {
public:
    // Number of tiles at a given level
    // nlat = 2^(level-1), nlng = 2^level (for level >= 1)
    static int NumLatTiles(int level) {
        return (level > 0) ? (1 << (level - 1)) : 1;
    }

    static int NumLngTiles(int level) {
        return (level > 0) ? (1 << level) : 1;
    }

    // Calculate geographic bounds for a tile
    static TileBounds GetTileBounds(int level, int ilat, int ilng) {
        int nlat = NumLatTiles(level);
        int nlng = NumLngTiles(level);

        // Each tile spans PI/nlat in latitude and 2*PI/nlng in longitude
        double latExtent = M_PI / nlat;
        double lngExtent = 2.0 * M_PI / nlng;

        TileBounds bounds;
        // Latitude: -PI/2 (south pole) to +PI/2 (north pole)
        bounds.minLat = -M_PI / 2.0 + ilat * latExtent;
        bounds.maxLat = bounds.minLat + latExtent;

        // Longitude: -PI to +PI (or 0 to 2*PI depending on convention)
        // D3D9Client uses -PI to +PI
        bounds.minLng = -M_PI + ilng * lngExtent;
        bounds.maxLng = bounds.minLng + lngExtent;

        return bounds;
    }

    // Calculate tile center direction (unit vector from planet center)
    static void GetTileCenter(int level, int ilat, int ilng, float& x, float& y, float& z) {
        TileBounds bounds = GetTileBounds(level, ilat, ilng);
        double lat = bounds.CenterLat();
        double lng = bounds.CenterLng();

        // Convert spherical to Cartesian (right-handed, Y up)
        double cosLat = cos(lat);
        x = static_cast<float>(cosLat * cos(lng));
        y = static_cast<float>(sin(lat));
        z = static_cast<float>(cosLat * sin(lng));
    }

    // Get child tile indices (quadtree subdivision)
    // Children are indexed 0-3: SW=0, SE=1, NW=2, NE=3
    static TileKey GetChildKey(const TileKey& parent, int childIdx) {
        TileKey child;
        child.level = parent.level + 1;

        // Each parent subdivides into 2x2 children
        int latOffset = (childIdx & 2) ? 1 : 0;  // bits: 0,1 -> 0, 2,3 -> 1
        int lngOffset = (childIdx & 1) ? 1 : 0;  // bits: 0,2 -> 0, 1,3 -> 1

        child.ilat = parent.ilat * 2 + latOffset;
        child.ilng = parent.ilng * 2 + lngOffset;

        return child;
    }

    // Get parent tile key
    static TileKey GetParentKey(const TileKey& child) {
        if (child.level <= 1) {
            return TileKey(0, 0, 0);  // Root level
        }
        TileKey parent;
        parent.level = child.level - 1;
        parent.ilat = child.ilat / 2;
        parent.ilng = child.ilng / 2;
        return parent;
    }

    // Get which child index this tile is within its parent
    static int GetChildIndex(const TileKey& tile) {
        int latBit = (tile.ilat & 1) ? 2 : 0;
        int lngBit = (tile.ilng & 1) ? 1 : 0;
        return latBit | lngBit;
    }

    // =======================================================================
    // Path generation for texture files
    // Matches D3D9Client's path format:
    //   Surface: {root}\{planet}\Surf\{lvl+4:02d}\{ilat:06d}\{ilng:06d}.dds
    //   Mask:    {root}\{planet}\Mask\{lvl+4:02d}\{ilat:06d}\{ilng:06d}.dds
    //   Elev:    {root}\{planet}\Elev\{lvl:02d}\{ilat:06d}\{ilng:06d}.elv
    // =======================================================================

    // Generate surface texture path
    static std::string GetSurfacePath(const std::string& textureRoot,
                                       const std::string& planetName,
                                       int level, int ilat, int ilng) {
        char path[512];
        // Note: Surface textures use level+4 in the path (D3D9Client convention)
        snprintf(path, sizeof(path), "%s\\%s\\Surf\\%02d\\%06d\\%06d.dds",
                 textureRoot.c_str(), planetName.c_str(), level + 4, ilat, ilng);
        return std::string(path);
    }

    // Generate mask texture path (specular/night lights)
    static std::string GetMaskPath(const std::string& textureRoot,
                                    const std::string& planetName,
                                    int level, int ilat, int ilng) {
        char path[512];
        snprintf(path, sizeof(path), "%s\\%s\\Mask\\%02d\\%06d\\%06d.dds",
                 textureRoot.c_str(), planetName.c_str(), level + 4, ilat, ilng);
        return std::string(path);
    }

    // Generate elevation data path
    static std::string GetElevationPath(const std::string& textureRoot,
                                         const std::string& planetName,
                                         int level, int ilat, int ilng) {
        char path[512];
        // Note: Elevation uses level directly (no +4 offset)
        snprintf(path, sizeof(path), "%s\\%s\\Elev\\%02d\\%06d\\%06d.elv",
                 textureRoot.c_str(), planetName.c_str(), level, ilat, ilng);
        return std::string(path);
    }

    // =======================================================================
    // Texture coordinate ranges (for using parent tile's texture)
    // =======================================================================

    struct TexCoordRange {
        float uMin, uMax;
        float vMin, vMax;

        TexCoordRange() : uMin(0), uMax(1), vMin(0), vMax(1) {}
        TexCoordRange(float u0, float u1, float v0, float v1)
            : uMin(u0), uMax(u1), vMin(v0), vMax(v1) {}
    };

    // Get texture coordinate range for a child tile within parent's texture
    static TexCoordRange GetChildTexRange(int childIdx) {
        // Child tiles use a quarter of parent's texture
        float u0 = (childIdx & 1) ? 0.5f : 0.0f;  // SE, NE use right half
        float v0 = (childIdx & 2) ? 0.5f : 0.0f;  // NW, NE use top half

        return TexCoordRange(u0, u0 + 0.5f, v0, v0 + 0.5f);
    }

    // Get texture range for accessing a subtile's portion of an ancestor texture
    static TexCoordRange GetSubTexRange(const TileKey& tile, const TileKey& ancestor) {
        if (tile.level <= ancestor.level) {
            return TexCoordRange();  // Full range
        }

        int levelDiff = tile.level - ancestor.level;
        int scale = 1 << levelDiff;

        // Calculate which portion of ancestor this tile covers
        int localIlat = tile.ilat - (ancestor.ilat * scale);
        int localIlng = tile.ilng - (ancestor.ilng * scale);

        float tileSize = 1.0f / scale;
        float uMin = localIlng * tileSize;
        float vMin = localIlat * tileSize;

        return TexCoordRange(uMin, uMin + tileSize, vMin, vMin + tileSize);
    }
};

#endif // TILECOORD_H
