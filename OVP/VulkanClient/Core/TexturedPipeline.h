// ==============================================================
// TexturedPipeline.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Graphics pipeline for textured mesh rendering.
// ==============================================================

#ifndef TEXTUREDPIPELINE_H
#define TEXTUREDPIPELINE_H

#include "VulkanContext.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class TexturedPipeline {
public:
    TexturedPipeline();
    ~TexturedPipeline();

    // Lifecycle
    bool Init(VulkanContext* ctx, VkRenderPass renderPass);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Accessors
    VkPipeline GetPipeline() const { return m_pipeline; }
    VkPipelineLayout GetLayout() const { return m_layout; }
    VkDescriptorSetLayout GetDescriptorLayout() const { return m_descriptorLayout; }

private:
    // Helpers
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);
    std::vector<uint32_t> LoadShaderFile(const std::string& filename);

    // State
    bool m_initialized;
    VulkanContext* m_ctx;

    // Pipeline objects
    VkDescriptorSetLayout m_descriptorLayout;
    VkPipelineLayout m_layout;
    VkPipeline m_pipeline;
};

#endif // TEXTUREDPIPELINE_H
