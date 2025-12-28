// ==============================================================
// TileTextureLoader.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "TileTextureLoader.h"
#include <fstream>
#include <sstream>

TileTextureLoader::TileTextureLoader()
    : m_ctx(nullptr)
    , m_staging(nullptr)
    , m_initialized(false)
{
}

TileTextureLoader::~TileTextureLoader() {
    Shutdown();
}

bool TileTextureLoader::Init(VulkanContext* ctx, StagingManager* staging) {
    if (m_initialized) {
        return true;  // Already initialized
    }

    if (!ctx || !staging) {
        return false;
    }

    m_ctx = ctx;
    m_staging = staging;
    m_initialized = true;

    return true;
}

void TileTextureLoader::Shutdown() {
    ClearCache();
    m_archives.clear();  // Close all archives
    m_ctx = nullptr;
    m_staging = nullptr;
    m_initialized = false;
}

void TileTextureLoader::SetTextureRoot(const std::string& root) {
    m_textureRoot = root;
}

VulkanTexture* TileTextureLoader::LoadTileTexture(const std::string& planetName,
                                                   int level, int ilat, int ilng) {
    if (!m_initialized) {
        return nullptr;
    }

    // Check cache first
    std::string cacheKey = MakeCacheKey(planetName, level, ilat, ilng);
    auto it = m_textureCache.find(cacheKey);
    if (it != m_textureCache.end()) {
        return it->second.get();
    }

    // Generate path and try loading from DDS file first
    std::string path = TileCoord::GetSurfacePath(m_textureRoot, planetName, level, ilat, ilng);

    DDSImage ddsImage;
    if (!DDSLoader::Load(path.c_str(), ddsImage)) {
        // DDS file not found, try archive fallback
        auto archiveData = LoadFromArchive(planetName, level, ilat, ilng);
        if (archiveData.empty()) {
            return nullptr;  // Not in archive either
        }
        // Parse DDS from archive data
        if (!DDSLoader::LoadFromMemory(archiveData.data(), archiveData.size(), ddsImage)) {
            return nullptr;
        }
    }

    // Create VulkanTexture
    auto texture = std::make_unique<VulkanTexture>();
    if (!texture->CreateFromMemory(m_ctx, m_staging, ddsImage.data.data(),
                                    ddsImage.width, ddsImage.height, ddsImage.format)) {
        return nullptr;
    }

    // Store in cache
    VulkanTexture* result = texture.get();
    m_textureCache[cacheKey] = std::move(texture);

    return result;
}

bool TileTextureLoader::TileTextureExists(const std::string& planetName,
                                           int level, int ilat, int ilng) const {
    std::string path = TileCoord::GetSurfacePath(m_textureRoot, planetName, level, ilat, ilng);

    std::ifstream file(path, std::ios::binary);
    return file.good();
}

void TileTextureLoader::ClearCache() {
    // Textures will be destroyed when unique_ptrs are cleared
    m_textureCache.clear();
}

std::string TileTextureLoader::MakeCacheKey(const std::string& planetName,
                                             int level, int ilat, int ilng) const {
    std::ostringstream oss;
    oss << planetName << "_" << level << "_" << ilat << "_" << ilng;
    return oss.str();
}

TreeArchive* TileTextureLoader::GetOrOpenArchive(const std::string& planetName) {
    // Check if already opened
    auto it = m_archives.find(planetName);
    if (it != m_archives.end()) {
        return it->second.get();
    }

    // Try to open archive: {textureRoot}/{planetName}/Archive/Surf.tree
    std::string archivePath = m_textureRoot + "/" + planetName + "/Archive/Surf.tree";
    auto archive = std::make_unique<TreeArchive>();
    if (!archive->Open(archivePath)) {
        return nullptr;
    }

    TreeArchive* result = archive.get();
    m_archives[planetName] = std::move(archive);
    return result;
}

std::vector<uint8_t> TileTextureLoader::LoadFromArchive(const std::string& planetName,
                                                         int level, int ilat, int ilng) {
    TreeArchive* archive = GetOrOpenArchive(planetName);
    if (!archive) {
        return {};
    }
    return archive->ReadTile(level, ilat, ilng);
}
