// ==============================================================
// TreeArchive.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "TreeArchive.h"
#include <zlib.h>
#include <cstring>

// Magic number for .tree files: 'TX' followed by version 1.0
static constexpr uint32_t TREE_MAGIC = 0x00015854;  // MAKEFOURCC('T','X',1,0)
static constexpr uint32_t TREE_HEADER_SIZE = 48;
static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

// =======================================================================
// TreeArchive implementation
// =======================================================================

TreeArchive::TreeArchive()
    : m_file(nullptr)
    , m_nodeCount(0)
    , m_dataOffset(0)
    , m_dataLength(0)
    , m_rootPos1(INVALID_INDEX)
    , m_rootPos2(INVALID_INDEX)
    , m_rootPos3(INVALID_INDEX)
{
    m_rootPos4[0] = m_rootPos4[1] = INVALID_INDEX;
}

TreeArchive::~TreeArchive()
{
    Close();
}

bool TreeArchive::Open(const std::string& archivePath)
{
    // Close any previously opened archive
    Close();

    // Open file in binary read mode
#ifdef _WIN32
    if (fopen_s(&m_file, archivePath.c_str(), "rb") != 0) {
        m_file = nullptr;
    }
#else
    m_file = fopen(archivePath.c_str(), "rb");
#endif
    if (m_file == nullptr) {
        return false;
    }

    // Read and validate header
    if (!ReadHeader()) {
        Close();
        return false;
    }

    // Read table of contents
    if (!ReadTOC()) {
        Close();
        return false;
    }

    return true;
}

void TreeArchive::Close()
{
    if (m_file != nullptr) {
        fclose(m_file);
        m_file = nullptr;
    }

    m_nodeCount = 0;
    m_dataOffset = 0;
    m_dataLength = 0;
    m_rootPos1 = INVALID_INDEX;
    m_rootPos2 = INVALID_INDEX;
    m_rootPos3 = INVALID_INDEX;
    m_rootPos4[0] = m_rootPos4[1] = INVALID_INDEX;
    m_toc.clear();
}

bool TreeArchive::ReadHeader()
{
    // Read magic number
    uint32_t magic;
    if (fread(&magic, sizeof(magic), 1, m_file) != 1 || magic != TREE_MAGIC) {
        return false;
    }

    // Read header size
    uint32_t headerSize;
    if (fread(&headerSize, sizeof(headerSize), 1, m_file) != 1 || headerSize != TREE_HEADER_SIZE) {
        return false;
    }

    // Read flags (reserved, ignored)
    uint32_t flags;
    if (fread(&flags, sizeof(flags), 1, m_file) != 1) {
        return false;
    }

    // Read data offset
    if (fread(&m_dataOffset, sizeof(m_dataOffset), 1, m_file) != 1) {
        return false;
    }

    // Read data length (64-bit)
    if (fread(&m_dataLength, sizeof(m_dataLength), 1, m_file) != 1) {
        return false;
    }

    // Read node count
    if (fread(&m_nodeCount, sizeof(m_nodeCount), 1, m_file) != 1) {
        return false;
    }

    // Read root positions
    if (fread(&m_rootPos1, sizeof(m_rootPos1), 1, m_file) != 1) {
        return false;
    }
    if (fread(&m_rootPos2, sizeof(m_rootPos2), 1, m_file) != 1) {
        return false;
    }
    if (fread(&m_rootPos3, sizeof(m_rootPos3), 1, m_file) != 1) {
        return false;
    }
    if (fread(m_rootPos4, sizeof(uint32_t), 2, m_file) != 2) {
        return false;
    }

    return true;
}

bool TreeArchive::ReadTOC()
{
    if (m_nodeCount == 0) {
        return true;  // Empty TOC is valid
    }

    m_toc.resize(m_nodeCount);

    // Read all nodes at once (each node is 24 bytes)
    if (fread(m_toc.data(), sizeof(TreeNode), m_nodeCount, m_file) != m_nodeCount) {
        m_toc.clear();
        return false;
    }

    return true;
}

uint32_t TreeArchive::GetTileIndex(int level, int ilat, int ilng)
{
    if (level <= 0) {
        return INVALID_INDEX;
    }

    if (level <= 4) {
        // Root level tiles
        switch (level) {
            case 1: return m_rootPos1;
            case 2: return m_rootPos2;
            case 3: return m_rootPos3;
            case 4: return (ilng >= 0 && ilng < 2) ? m_rootPos4[ilng] : INVALID_INDEX;
            default: return INVALID_INDEX;
        }
    }

    // Navigate quadtree from parent
    int parentLevel = level - 1;
    int parentIlat = ilat / 2;
    int parentIlng = ilng / 2;

    uint32_t parentIdx = GetTileIndex(parentLevel, parentIlat, parentIlng);
    if (parentIdx == INVALID_INDEX || parentIdx >= m_nodeCount) {
        return INVALID_INDEX;
    }

    // Determine child quadrant
    // quadrant = (lat_bit * 2) + lng_bit
    // where lat_bit is the LSB of ilat, lng_bit is the LSB of ilng
    int quadrant = ((ilat & 1) << 1) | (ilng & 1);

    return m_toc[parentIdx].child[quadrant];
}

bool TreeArchive::HasTile(int level, int ilat, int ilng)
{
    uint32_t idx = GetTileIndex(level, ilat, ilng);
    if (idx == INVALID_INDEX || idx >= m_nodeCount) {
        return false;
    }

    // A tile "exists" if it has data (non-zero size)
    return m_toc[idx].size > 0;
}

uint32_t TreeArchive::GetCompressedSize(uint32_t idx) const
{
    if (idx >= m_nodeCount) {
        return 0;
    }

    // Compressed size = next node's position - this node's position
    // For the last node, use total data length
    if (idx == m_nodeCount - 1) {
        return static_cast<uint32_t>(m_dataLength - m_toc[idx].pos);
    }

    return static_cast<uint32_t>(m_toc[idx + 1].pos - m_toc[idx].pos);
}

std::vector<uint8_t> TreeArchive::ReadTile(int level, int ilat, int ilng)
{
    if (!IsOpen()) {
        return {};
    }

    uint32_t idx = GetTileIndex(level, ilat, ilng);
    if (idx == INVALID_INDEX || idx >= m_nodeCount) {
        return {};
    }

    uint32_t uncompressedSize = m_toc[idx].size;
    if (uncompressedSize == 0) {
        // Node exists but has no data (only has children)
        return {};
    }

    uint32_t compressedSize = GetCompressedSize(idx);
    if (compressedSize == 0) {
        return {};
    }

    // Seek to data position
    int64_t dataPos = m_dataOffset + m_toc[idx].pos;
#ifdef _WIN32
    if (_fseeki64(m_file, dataPos, SEEK_SET) != 0) {
#else
    if (fseeko(m_file, dataPos, SEEK_SET) != 0) {
#endif
        return {};
    }

    // Read compressed data
    std::vector<uint8_t> compressed(compressedSize);
    if (fread(compressed.data(), 1, compressedSize, m_file) != compressedSize) {
        return {};
    }

    // Decompress
    return Decompress(compressed, uncompressedSize);
}

std::vector<uint8_t> TreeArchive::Decompress(const std::vector<uint8_t>& compressed, uint32_t uncompressedSize)
{
    if (compressed.empty() || uncompressedSize == 0) {
        return {};
    }

    std::vector<uint8_t> decompressed(uncompressedSize);

    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    stream.next_in = const_cast<Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = decompressed.data();
    stream.avail_out = uncompressedSize;

    // Initialize for zlib format decompression (with zlib header)
    if (inflateInit(&stream) != Z_OK) {
        return {};
    }

    int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (result != Z_STREAM_END) {
        return {};
    }

    return decompressed;
}
