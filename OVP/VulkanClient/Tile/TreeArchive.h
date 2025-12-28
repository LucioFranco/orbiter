// ==============================================================
// TreeArchive.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#ifndef TREEARCHIVE_H
#define TREEARCHIVE_H

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

// =======================================================================
// TreeNode - Table of contents entry for a single tile
// =======================================================================
struct TreeNode {
    int64_t pos;        // File offset of compressed data (relative to dataOfs)
    uint32_t size;      // Uncompressed size in bytes
    uint32_t child[4];  // Child indices [BL, BR, TL, TR], 0xFFFFFFFF = no child

    TreeNode() : pos(0), size(0) {
        child[0] = child[1] = child[2] = child[3] = 0xFFFFFFFF;
    }
};

// =======================================================================
// TreeArchive - Parser for Orbiter .tree archive files
// =======================================================================
class TreeArchive {
public:
    TreeArchive();
    ~TreeArchive();

    // Open a .tree archive file
    // Returns true on success, false on failure
    bool Open(const std::string& archivePath);

    // Close the archive and release resources
    void Close();

    // Check if archive is currently open
    bool IsOpen() const { return m_file != nullptr; }

    // Get the number of nodes in the archive
    uint32_t GetNodeCount() const { return m_nodeCount; }

    // Check if a tile exists at the given level and coordinates
    bool HasTile(int level, int ilat, int ilng);

    // Read and decompress a tile
    // Returns the decompressed DDS data, or empty vector if not found
    std::vector<uint8_t> ReadTile(int level, int ilat, int ilng);

    // Get the internal index for a tile (for testing)
    // Returns 0xFFFFFFFF if tile doesn't exist
    uint32_t GetTileIndex(int level, int ilat, int ilng);

private:
    // Read the file header
    bool ReadHeader();

    // Read the table of contents
    bool ReadTOC();

    // Get compressed size of a node
    uint32_t GetCompressedSize(uint32_t idx) const;

    // Decompress zlib data
    std::vector<uint8_t> Decompress(const std::vector<uint8_t>& compressed, uint32_t uncompressedSize);

    FILE* m_file;               // File handle
    uint32_t m_nodeCount;       // Number of nodes in TOC
    uint32_t m_dataOffset;      // Offset to compressed data block
    int64_t m_dataLength;       // Total compressed data size
    uint32_t m_rootPos1;        // Level-1 root index
    uint32_t m_rootPos2;        // Level-2 root index
    uint32_t m_rootPos3;        // Level-3 root index
    uint32_t m_rootPos4[2];     // Level-4 roots (2 hemispheres)
    std::vector<TreeNode> m_toc; // Table of contents
};

#endif // TREEARCHIVE_H
