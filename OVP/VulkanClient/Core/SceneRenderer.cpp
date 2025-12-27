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
    , m_texturedInitialized(false)
    , m_textureDescriptorSet(VK_NULL_HANDLE)
    , m_quadIndexCount(0)
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

    ShutdownTextured();

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

void SceneRenderer::SetCameraFromOrbiter(const double* rotMatrix, double apertureRad,
                                          double nearPlane, double farPlane) {
    // Build view matrix from Orbiter's camera rotation matrix
    // Orbiter's MATRIX3 is row-major: m11, m12, m13, m21, m22, m23, m31, m32, m33
    // GLM matrices are column-major, so we need to transpose
    //
    // The rotation matrix from oapiCameraRotationMatrix represents the camera's
    // orientation in global space. For the view matrix, we need to transform
    // from world space to camera space, which is the transpose of the camera's rotation.

    m_viewMatrix = glm::mat4(1.0f);

    // Orbiter uses: X=right, Y=up, Z=forward (left-handed or right-handed depending on context)
    // Vulkan uses: X=right, Y=down, Z=into screen (right-handed, Y-flip in projection)
    //
    // The rotation matrix rows are the camera's basis vectors in world space:
    // Row 0 (m11, m12, m13) = camera X axis (right)
    // Row 1 (m21, m22, m23) = camera Y axis (up)
    // Row 2 (m31, m32, m33) = camera Z axis (forward)

    // For view matrix, we need the transpose (columns become rows)
    m_viewMatrix[0][0] = static_cast<float>(rotMatrix[0]); // m11
    m_viewMatrix[1][0] = static_cast<float>(rotMatrix[1]); // m12
    m_viewMatrix[2][0] = static_cast<float>(rotMatrix[2]); // m13

    m_viewMatrix[0][1] = static_cast<float>(rotMatrix[3]); // m21
    m_viewMatrix[1][1] = static_cast<float>(rotMatrix[4]); // m22
    m_viewMatrix[2][1] = static_cast<float>(rotMatrix[5]); // m23

    m_viewMatrix[0][2] = static_cast<float>(rotMatrix[6]); // m31
    m_viewMatrix[1][2] = static_cast<float>(rotMatrix[7]); // m32
    m_viewMatrix[2][2] = static_cast<float>(rotMatrix[8]); // m33

    // No translation in view matrix - Orbiter renders camera-centric
    // (objects are positioned relative to camera, camera is at origin)
    m_viewMatrix[3][0] = 0.0f;
    m_viewMatrix[3][1] = 0.0f;
    m_viewMatrix[3][2] = 0.0f;
    m_viewMatrix[3][3] = 1.0f;

    // Build projection matrix
    // apertureRad is half the vertical FOV in radians
    // Full vertical FOV = 2 * apertureRad
    if (m_extent.height > 0) {
        float aspect = static_cast<float>(m_extent.width) / static_cast<float>(m_extent.height);
        float fovY = static_cast<float>(apertureRad * 2.0);  // Full vertical FOV

        m_projMatrix = glm::perspective(fovY, aspect,
                                        static_cast<float>(nearPlane),
                                        static_cast<float>(farPlane));
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
    // Place test triangle 5 units in front of camera (-Z is forward in Vulkan)
    // This allows us to verify the camera rotation is working
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f));
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

// ============================================================
// Textured Rendering
// ============================================================

bool SceneRenderer::InitTextured(VkRenderPass renderPass, StagingManager* staging) {
    if (m_texturedInitialized) {
        return true;
    }
    if (!m_ctx || !m_ctx->IsInitialized() || renderPass == VK_NULL_HANDLE) {
        return false;
    }

    // Initialize descriptor pool
    if (!m_descriptorPool.Init(m_ctx, 10)) {
        return false;
    }

    // Initialize textured pipeline
    if (!m_texturedPipeline.Init(m_ctx, renderPass)) {
        m_descriptorPool.Shutdown();
        return false;
    }

    // Create default checkerboard texture if none exists
    if (!m_testTexture.IsValid()) {
        // 4x4 red/white checkerboard pattern
        uint8_t checkerboard[4 * 4 * 4];  // 4x4 RGBA
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                int idx = (y * 4 + x) * 4;
                bool isWhite = ((x + y) % 2) == 0;
                checkerboard[idx + 0] = isWhite ? 255 : 255;  // R
                checkerboard[idx + 1] = isWhite ? 255 : 0;    // G
                checkerboard[idx + 2] = isWhite ? 255 : 0;    // B
                checkerboard[idx + 3] = 255;                   // A
            }
        }

        if (!m_testTexture.CreateFromMemory(m_ctx, staging, checkerboard, 4, 4)) {
            m_texturedPipeline.Shutdown();
            m_descriptorPool.Shutdown();
            return false;
        }
    }

    // Allocate descriptor set
    m_textureDescriptorSet = m_descriptorPool.AllocateSet(m_texturedPipeline.GetDescriptorLayout());
    if (m_textureDescriptorSet == VK_NULL_HANDLE) {
        m_testTexture.Destroy();
        m_texturedPipeline.Shutdown();
        m_descriptorPool.Shutdown();
        return false;
    }

    // Write texture to descriptor set
    VulkanDescriptorWriter writer;
    writer.WriteCombinedImageSampler(0,
        m_testTexture.GetSampler(),
        m_testTexture.GetImageView());
    writer.Update(m_ctx->GetDevice(), m_textureDescriptorSet);

    // Create quad geometry
    if (!CreateTexturedQuad(staging)) {
        m_testTexture.Destroy();
        m_texturedPipeline.Shutdown();
        m_descriptorPool.Shutdown();
        return false;
    }

    m_texturedInitialized = true;
    return true;
}

void SceneRenderer::ShutdownTextured() {
    if (!m_texturedInitialized) {
        return;
    }

    m_quadIndexBuffer.Destroy();
    m_quadVertexBuffer.Destroy();
    m_testTexture.Destroy();
    m_texturedPipeline.Shutdown();
    m_descriptorPool.Shutdown();

    m_textureDescriptorSet = VK_NULL_HANDLE;
    m_quadIndexCount = 0;
    m_texturedInitialized = false;
}

bool SceneRenderer::CreateTestTexture(const uint8_t* data, uint32_t width, uint32_t height) {
    if (!m_ctx || !m_ctx->IsInitialized()) {
        return false;
    }

    // Destroy existing texture if any
    m_testTexture.Destroy();

    // Create new texture
    return m_testTexture.CreateFromMemory(m_ctx, m_staging, data, width, height);
}

bool SceneRenderer::CreateTexturedQuad(StagingManager* staging) {
    // Quad vertices with texture coordinates
    // UV origin (0,0) is top-left, (1,1) is bottom-right
    TexturedVertex vertices[] = {
        // Position              // TexCoord
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},  // Bottom-left
        {{ 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},  // Bottom-right
        {{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},  // Top-right
        {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f}},  // Top-left
    };

    uint16_t indices[] = { 0, 1, 2, 2, 3, 0 };
    m_quadIndexCount = 6;

    VkDeviceSize vertexBufferSize = sizeof(vertices);
    VkDeviceSize indexBufferSize = sizeof(indices);

    // Use device-local memory with staging if available
    if (staging && staging->IsInitialized()) {
        if (!m_quadVertexBuffer.CreateDeviceLocal(m_ctx, staging, vertexBufferSize,
                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices)) {
            return false;
        }

        if (!m_quadIndexBuffer.CreateDeviceLocal(m_ctx, staging, indexBufferSize,
                                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices)) {
            m_quadVertexBuffer.Destroy();
            return false;
        }
    } else {
        // Fallback: host-visible memory
        if (!m_quadVertexBuffer.Create(m_ctx, vertexBufferSize,
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            return false;
        }

        void* data = m_quadVertexBuffer.Map();
        if (!data) {
            m_quadVertexBuffer.Destroy();
            return false;
        }
        memcpy(data, vertices, vertexBufferSize);
        m_quadVertexBuffer.Unmap();

        if (!m_quadIndexBuffer.Create(m_ctx, indexBufferSize,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            m_quadVertexBuffer.Destroy();
            return false;
        }

        data = m_quadIndexBuffer.Map();
        if (!data) {
            m_quadIndexBuffer.Destroy();
            m_quadVertexBuffer.Destroy();
            return false;
        }
        memcpy(data, indices, indexBufferSize);
        m_quadIndexBuffer.Unmap();
    }

    return true;
}

void SceneRenderer::RenderTexturedQuad(VkCommandBuffer cmd) {
    if (!m_texturedInitialized) {
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

    // Bind textured pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_texturedPipeline.GetPipeline());

    // Bind descriptor set (texture)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           m_texturedPipeline.GetLayout(),
                           0, 1, &m_textureDescriptorSet, 0, nullptr);

    // Calculate MVP matrix
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 mvp = m_projMatrix * m_viewMatrix * model;

    // Push constants
    vkCmdPushConstants(cmd, m_texturedPipeline.GetLayout(),
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

    // Bind vertex buffer
    VkBuffer vertexBuffers[] = { m_quadVertexBuffer.GetBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

    // Bind index buffer
    vkCmdBindIndexBuffer(cmd, m_quadIndexBuffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

    // Draw
    vkCmdDrawIndexed(cmd, m_quadIndexCount, 1, 0, 0, 0);
}
