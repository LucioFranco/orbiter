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

// Forward declaration
class StagingManager;

class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    // Lifecycle
    // If staging is provided, uses device-local memory for optimal GPU performance
    bool Init(VulkanContext* ctx, VkRenderPass renderPass, VkExtent2D extent,
              StagingManager* staging = nullptr);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }
    bool UsesDeviceLocalMemory() const;

    // Frame setup
    void SetViewport(VkExtent2D extent);
    void SetCamera(const glm::vec3& eye, const glm::vec3& target, float fovDegrees);

    // Record draw commands into command buffer
    // Must be called between vkCmdBeginRenderPass and vkCmdEndRenderPass
    void Render(VkCommandBuffer cmd);

private:
    bool CreateTestScene(StagingManager* staging);

    // State
    bool m_initialized;
    VulkanContext* m_ctx;
    StagingManager* m_staging;

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
