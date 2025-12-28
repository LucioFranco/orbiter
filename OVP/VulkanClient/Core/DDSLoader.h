// ==============================================================
// DDSLoader.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// DDS texture file loader for Vulkan.
// Parses DDS header and returns data for BC compressed formats.
// ==============================================================

#ifndef DDSLOADER_H
#define DDSLOADER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

// DDS file format structures (matches Microsoft DDS spec)
#pragma pack(push, 1)

struct DDSPixelFormat {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDSHeader {
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat pixelFormat;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

#pragma pack(pop)

// DDS pixel format flags
#define DDPF_ALPHAPIXELS  0x00000001
#define DDPF_ALPHA        0x00000002
#define DDPF_FOURCC       0x00000004
#define DDPF_RGB          0x00000040
#define DDPF_LUMINANCE    0x00020000

// DDS header flags
#define DDSD_CAPS         0x00000001
#define DDSD_HEIGHT       0x00000002
#define DDSD_WIDTH        0x00000004
#define DDSD_PITCH        0x00000008
#define DDSD_PIXELFORMAT  0x00001000
#define DDSD_MIPMAPCOUNT  0x00020000
#define DDSD_LINEARSIZE   0x00080000
#define DDSD_DEPTH        0x00800000

// FourCC codes
#define FOURCC_DXT1 0x31545844  // "DXT1"
#define FOURCC_DXT2 0x32545844  // "DXT2"
#define FOURCC_DXT3 0x33545844  // "DXT3"
#define FOURCC_DXT4 0x34545844  // "DXT4"
#define FOURCC_DXT5 0x35545844  // "DXT5"

// Result of loading a DDS file
struct DDSImage {
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    VkFormat format;
    std::vector<uint8_t> data;  // Raw block data (BC compressed or uncompressed)
    bool hasAlpha;
};

class DDSLoader {
public:
    // Load DDS from file path
    static bool Load(const char* path, DDSImage& out);

    // Load DDS from memory buffer
    static bool LoadFromMemory(const uint8_t* data, size_t size, DDSImage& out);

    // Check if file is a DDS file by extension
    static bool IsDDSFile(const char* path);

    // Get bytes per block for a BC format
    static uint32_t GetBytesPerBlock(VkFormat format);

    // Calculate image size in bytes for BC format
    static size_t CalculateImageSize(uint32_t width, uint32_t height, VkFormat format);

    // Get human-readable format name for debugging
    static const char* GetFormatName(VkFormat format);
};

#endif // DDSLOADER_H
