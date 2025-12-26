// ==============================================================
// SceneRenderer.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "SceneRenderer.h"
#include "StagingManager.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

SceneRenderer::SceneRenderer()
    : m_initialized(false)
    , m_ctx(nullptr)
    , m_staging(nullptr)
    , m_indexCount(0)
    , m_extent{0, 0}
    , m_viewMatrix(1.0f)
    , m_projMatrix(1.0f)
{
}

SceneRenderer::~SceneRenderer() {
    Shutdown();
}

bool SceneRenderer::Init(VulkanContext* ctx, VkRenderPass renderPass, VkExtent2D extent,
                          StagingManager* staging) {
    if (m_initialized) {
        return true;
    }
    if (!ctx || !ctx->IsInitialized() || renderPass == VK_NULL_HANDLE) {
        return false;
    }

    m_ctx = ctx;
    m_staging = staging;
    m_extent = extent;

    // Create pipeline
    if (!m_pipeline.Init(ctx, renderPass)) {
        return false;
    }

    // Create test scene geometry (uses device-local if staging provided)
    if (!CreateTestScene(staging)) {
        m_pipeline.Shutdown();
        return false;
    }

    // Set default camera
    SetCamera(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), 45.0f);

    m_initialized = true;
    return true;
}

void SceneRenderer::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_indexBuffer.Destroy();
    m_vertexBuffer.Destroy();
    m_pipeline.Shutdown();

    m_ctx = nullptr;
    m_staging = nullptr;
    m_initialized = false;
}

bool SceneRenderer::UsesDeviceLocalMemory() const {
    return m_initialized && m_vertexBuffer.IsDeviceLocal();
}

void SceneRenderer::SetViewport(VkExtent2D extent) {
    m_extent = extent;
    // Update projection matrix aspect ratio
    if (m_extent.height > 0) {
        float aspect = static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height);
        // Recalculate projection with current FOV (stored implicitly in m_projMatrix)
        // For now, use a default 45 degree FOV
        m_projMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        m_projMatrix[1][1] *= -1;  // Vulkan Y flip
    }
}

void SceneRenderer::SetCamera(const glm::vec3& eye, const glm::vec3& target, float fovDegrees) {
    m_viewMatrix = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));

    if (m_extent.height > 0) {
        float aspect = static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height);
        m_projMatrix = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 100.0f);
        m_projMatrix[1][1] *= -1;  // Vulkan Y flip
    }
}

void SceneRenderer::Render(VkCommandBuffer cmd) {
    if (!m_initialized) {
        return;
    }

    // Set viewport and scissor (dynamic state)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_extent.width);
    viewport.height = static_cast<float>(m_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetPipeline());

    // Calculate MVP matrix
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 mvp = m_projMatrix * m_viewMatrix * model;

    // Push constants
    vkCmdPushConstants(cmd, m_pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

    // Bind vertex buffer
    VkBuffer vertexBuffers[] = { m_vertexBuffer.GetBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

    // Bind index buffer
    vkCmdBindIndexBuffer(cmd, m_indexBuffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

    // Draw
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

bool SceneRenderer::CreateTestScene(StagingManager* staging) {
    // Simple colored triangle
    // Vertices: position (vec3) + color (vec3) = 24 bytes each
    BasicVertex vertices[] = {
        // Position              // Color (RGB)
        {{  0.0f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f }},  // Bottom - Red
        {{  0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }},  // Top right - Green
        {{ -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }},  // Top left - Blue
    };

    uint16_t indices[] = { 0, 1, 2 };
    m_indexCount = 3;

    VkDeviceSize vertexBufferSize = sizeof(vertices);
    VkDeviceSize indexBufferSize = sizeof(indices);

    // Use device-local memory with staging if available (optimal GPU performance)
    if (staging && staging->IsInitialized()) {
        // Create device-local vertex buffer with initial data
        if (!m_vertexBuffer.CreateDeviceLocal(m_ctx, staging, vertexBufferSize,
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices)) {
            return false;
        }

        // Create device-local index buffer with initial data
        if (!m_indexBuffer.CreateDeviceLocal(m_ctx, staging, indexBufferSize,
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices)) {
            m_vertexBuffer.Destroy();
            return false;
        }
    } else {
        // Fallback: host-visible memory (slower but works without staging)
        if (!m_vertexBuffer.Create(m_ctx, vertexBufferSize,
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            return false;
        }

        void* data = m_vertexBuffer.Map();
        if (!data) {
            m_vertexBuffer.Destroy();
            return false;
        }
        memcpy(data, vertices, vertexBufferSize);
        m_vertexBuffer.Unmap();

        if (!m_indexBuffer.Create(m_ctx, indexBufferSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            m_vertexBuffer.Destroy();
            return false;
        }

        data = m_indexBuffer.Map();
        if (!data) {
            m_indexBuffer.Destroy();
            m_vertexBuffer.Destroy();
            return false;
        }
        memcpy(data, indices, indexBufferSize);
        m_indexBuffer.Unmap();
    }

    return true;
}
