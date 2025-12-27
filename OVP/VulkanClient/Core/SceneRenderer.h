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
#include "TexturedPipeline.h"
#include "VulkanTexture.h"
#include "VulkanDescriptors.h"

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

    // Set camera from Orbiter's camera system
    // rotMatrix: 3x3 rotation matrix from oapiCameraRotationMatrix (row-major, doubles)
    // apertureRad: half-angle vertical FOV in radians from oapiCameraAperture
    // nearPlane/farPlane: clipping planes
    void SetCameraFromOrbiter(const double* rotMatrix, double apertureRad,
                               double nearPlane = 1.0, double farPlane = 1e8);

    // Get current view/projection matrices (for external use)
    const glm::mat4& GetViewMatrix() const { return m_viewMatrix; }
    const glm::mat4& GetProjectionMatrix() const { return m_projMatrix; }

    // Record draw commands into command buffer
    // Must be called between vkCmdBeginRenderPass and vkCmdEndRenderPass
    void Render(VkCommandBuffer cmd);

    // Textured rendering support
    bool InitTextured(VkRenderPass renderPass, StagingManager* staging);
    void ShutdownTextured();
    bool IsTexturedInitialized() const { return m_texturedInitialized; }

    // Create a test texture from raw RGBA data
    bool CreateTestTexture(const uint8_t* data, uint32_t width, uint32_t height);

    // Render a textured quad (must call InitTextured first)
    void RenderTexturedQuad(VkCommandBuffer cmd);

private:
    bool CreateTestScene(StagingManager* staging);
    bool CreateTexturedQuad(StagingManager* staging);

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

    // Textured rendering state
    bool m_texturedInitialized;
    TexturedPipeline m_texturedPipeline;
    VulkanDescriptorPool m_descriptorPool;
    VkDescriptorSet m_textureDescriptorSet;
    VulkanTexture m_testTexture;
    VulkanBuffer m_quadVertexBuffer;
    VulkanBuffer m_quadIndexBuffer;
    uint32_t m_quadIndexCount;
};

#endif // SCENERENDERER_H
