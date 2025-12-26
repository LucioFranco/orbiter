// ==============================================================
// SceneRenderer.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Shared scene rendering logic for HeadlessRenderer and VulkanClient.
// ==============================================================

#ifndef SCENERENDERER_H
#define SCENERENDERER_H

#include "VulkanContext.h"
#include "VulkanPipeline.h"
#include "VulkanBuffer.h"

#include <glm/glm.hpp>

class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    // Lifecycle
    bool Init(VulkanContext* ctx, VkRenderPass renderPass, VkExtent2D extent);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Frame setup
    void SetViewport(VkExtent2D extent);
    void SetCamera(const glm::vec3& eye, const glm::vec3& target, float fovDegrees);

    // Record draw commands into command buffer
    // Must be called between vkCmdBeginRenderPass and vkCmdEndRenderPass
    void Render(VkCommandBuffer cmd);

private:
    bool CreateTestScene();

    // State
    bool m_initialized;
    VulkanContext* m_ctx;

    // Pipeline
    VulkanPipeline m_pipeline;

    // Buffers
    VulkanBuffer m_vertexBuffer;
    VulkanBuffer m_indexBuffer;
    uint32_t m_indexCount;

    // Camera/Transform state
    VkExtent2D m_extent;
    glm::mat4 m_viewMatrix;
    glm::mat4 m_projMatrix;
};

#endif // SCENERENDERER_H
