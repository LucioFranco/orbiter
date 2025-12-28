// ==============================================================
// DDSLoader.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "DDSLoader.h"
#include <fstream>
#include <cstring>
#include <algorithm>

// DDS magic number "DDS "
static const uint32_t DDS_MAGIC = 0x20534444;

bool DDSLoader::Load(const char* path, DDSImage& out)
{
    if (!path) return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        return false;
    }

    return LoadFromMemory(buffer.data(), buffer.size(), out);
}

bool DDSLoader::LoadFromMemory(const uint8_t* data, size_t size, DDSImage& out)
{
    if (!data || size < 4 + sizeof(DDSHeader)) {
        return false;
    }

    // Check magic number
    uint32_t magic;
    memcpy(&magic, data, sizeof(uint32_t));
    if (magic != DDS_MAGIC) {
        return false;
    }
    data += 4;
    size -= 4;

    // Read header
    DDSHeader header;
    memcpy(&header, data, sizeof(DDSHeader));
    data += sizeof(DDSHeader);
    size -= sizeof(DDSHeader);

    // Validate header size
    if (header.size != sizeof(DDSHeader)) {
        return false;
    }

    out.width = header.width;
    out.height = header.height;
    out.mipLevels = (header.flags & DDSD_MIPMAPCOUNT) ? header.mipMapCount : 1;
    if (out.mipLevels == 0) out.mipLevels = 1;
    out.hasAlpha = false;

    // Determine format from pixel format
    if (header.pixelFormat.flags & DDPF_FOURCC) {
        switch (header.pixelFormat.fourCC) {
            case FOURCC_DXT1:
                out.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                out.hasAlpha = false;  // DXT1 has 1-bit alpha but we treat as opaque
                break;
            case FOURCC_DXT2:
            case FOURCC_DXT3:
                out.format = VK_FORMAT_BC2_UNORM_BLOCK;
                out.hasAlpha = true;
                break;
            case FOURCC_DXT4:
            case FOURCC_DXT5:
                out.format = VK_FORMAT_BC3_UNORM_BLOCK;
                out.hasAlpha = true;
                break;
            default:
                // Unknown FourCC - not supported
                return false;
        }

        // Calculate data size for BC format (mip level 0 only for now)
        size_t dataSize = CalculateImageSize(out.width, out.height, out.format);

        if (size < dataSize) {
            return false;
        }

        out.data.resize(dataSize);
        memcpy(out.data.data(), data, dataSize);

    } else if (header.pixelFormat.flags & DDPF_RGB) {
        // Uncompressed RGB/RGBA format
        uint32_t bpp = header.pixelFormat.rgbBitCount;

        if (bpp == 32) {
            // Check for BGRA vs RGBA based on masks
            if (header.pixelFormat.rBitMask == 0x00FF0000 &&
                header.pixelFormat.gBitMask == 0x0000FF00 &&
                header.pixelFormat.bBitMask == 0x000000FF) {
                out.format = VK_FORMAT_B8G8R8A8_UNORM;
            } else {
                out.format = VK_FORMAT_R8G8B8A8_UNORM;
            }
            out.hasAlpha = (header.pixelFormat.flags & DDPF_ALPHAPIXELS) != 0;
        } else if (bpp == 24) {
            // 24-bit RGB - need to convert to 32-bit
            // For simplicity, we'll return false and let stb_image handle it
            return false;
        } else {
            return false;
        }

        size_t dataSize = out.width * out.height * (bpp / 8);
        if (size < dataSize) {
            return false;
        }

        out.data.resize(dataSize);
        memcpy(out.data.data(), data, dataSize);

    } else if (header.pixelFormat.flags & DDPF_LUMINANCE) {
        // Grayscale - not commonly used, skip for now
        return false;
    } else {
        return false;
    }

    return true;
}

bool DDSLoader::IsDDSFile(const char* path)
{
    if (!path) return false;

    size_t len = strlen(path);
    if (len < 4) return false;

    const char* ext = path + len - 4;
    return (_stricmp(ext, ".dds") == 0);
}

uint32_t DDSLoader::GetBytesPerBlock(VkFormat format)
{
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            return 8;  // 8 bytes per 4x4 block
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
            return 16; // 16 bytes per 4x4 block
        default:
            return 0;
    }
}

size_t DDSLoader::CalculateImageSize(uint32_t width, uint32_t height, VkFormat format)
{
    uint32_t bytesPerBlock = GetBytesPerBlock(format);
    if (bytesPerBlock == 0) {
        // Not a BC format - assume 4 bytes per pixel (RGBA)
        return static_cast<size_t>(width) * height * 4;
    }

    // BC formats: round up to 4x4 blocks
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;

    return static_cast<size_t>(blocksX) * blocksY * bytesPerBlock;
}

const char* DDSLoader::GetFormatName(VkFormat format)
{
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:  return "BC1_RGB (DXT1)";
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return "BC1_RGBA (DXT1)";
        case VK_FORMAT_BC2_UNORM_BLOCK:      return "BC2 (DXT3)";
        case VK_FORMAT_BC3_UNORM_BLOCK:      return "BC3 (DXT5)";
        case VK_FORMAT_R8G8B8A8_UNORM:       return "RGBA8";
        case VK_FORMAT_B8G8R8A8_UNORM:       return "BGRA8";
        default:                             return "Unknown";
    }
}
