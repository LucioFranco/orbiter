// ==============================================================
// VulkanPipeline.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Graphics pipeline wrapper for basic 3D rendering.
// ==============================================================

#ifndef VULKANPIPELINE_H
#define VULKANPIPELINE_H

#include "VulkanContext.h"
#include <vector>
#include <string>

// Vertex format for basic 3D rendering
struct BasicVertex {
    float position[3];
    float color[3];

    static VkVertexInputBindingDescription GetBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 2> GetAttributeDescriptions();
};

class VulkanPipeline {
public:
    VulkanPipeline();
    ~VulkanPipeline();

    // Lifecycle
    bool Init(VulkanContext* ctx, VkRenderPass renderPass);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Accessors
    VkPipeline GetPipeline() const { return m_pipeline; }
    VkPipelineLayout GetLayout() const { return m_layout; }

private:
    // Helpers
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);
    std::vector<uint32_t> LoadShaderFile(const std::string& filename);

    // State
    bool m_initialized;
    VulkanContext* m_ctx;

    // Pipeline objects
    VkPipelineLayout m_layout;
    VkPipeline m_pipeline;
};

#endif // VULKANPIPELINE_H
