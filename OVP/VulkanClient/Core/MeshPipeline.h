// ==============================================================
// MeshPipeline.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Graphics pipeline for mesh rendering with normals and textures.
// Uses MeshVertex format: position, normal, texcoord.
// ==============================================================

#ifndef MESHPIPELINE_H
#define MESHPIPELINE_H

#include "VulkanContext.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

// Forward declaration
class StagingManager;

// Push constant data for mesh rendering
// Total: 176 bytes (MVP=64 + Model=64 + LightDir=16 + Material=32)
struct MeshPushConstants {
    float mvp[16];       // Model-View-Projection matrix (64 bytes)
    float model[16];     // Model matrix for normal transformation (64 bytes)
    float lightDir[4];   // Light direction xyz, specular power in w (16 bytes)
    float matDiffuse[4]; // Material diffuse color RGBA (16 bytes)
    float matEmissive[4];// Material emissive RGB, hasTexture flag in w (16 bytes)
};

class MeshPipeline {
public:
    MeshPipeline();
    ~MeshPipeline();

    // Lifecycle
    bool Init(VulkanContext* ctx, VkRenderPass renderPass, StagingManager* staging = nullptr);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Accessors
    VkPipeline GetPipeline() const { return m_pipeline; }
    VkPipelineLayout GetLayout() const { return m_layout; }
    VkDescriptorSetLayout GetDescriptorLayout() const { return m_descriptorLayout; }
    VkDescriptorSet GetDefaultTextureDescriptor() const { return m_defaultTextureDescriptor; }

    // Allocate descriptor set for a texture (caller owns the set)
    VkDescriptorSet AllocateTextureDescriptor(VkImageView imageView, VkSampler sampler);

private:
    // Helpers
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);
    std::vector<uint32_t> LoadShaderFile(const std::string& filename);
    bool CreateDefaultTexture(StagingManager* staging);

    // State
    bool m_initialized;
    VulkanContext* m_ctx;

    // Pipeline objects
    VkDescriptorSetLayout m_descriptorLayout;
    VkDescriptorPool m_descriptorPool;
    VkPipelineLayout m_layout;
    VkPipeline m_pipeline;

    // Default white texture for untextured meshes
    VkImage m_defaultTexture;
    VmaAllocation m_defaultTextureAlloc;
    VkImageView m_defaultTextureView;
    VkSampler m_defaultSampler;
    VkDescriptorSet m_defaultTextureDescriptor;
};

#endif // MESHPIPELINE_H
