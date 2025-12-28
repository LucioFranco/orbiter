// ==============================================================
// VulkanClient.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#define OAPI_IMPLEMENTATION

#include "VulkanClient.h"
#include "OrbiterAPI.h"
#include "VesselAPI.h"
#include <vulkan/vulkan_win32.h>
#include <array>
#include <cmath>
#include <dinput.h>  // For DIK_* key codes

// ImGui includes
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_win32.h>

// ======================================================================
// Module interface
// ======================================================================

static VulkanClient* g_client = nullptr;

DLLCLBK void InitModule(HINSTANCE hDLL)
{
    g_client = new VulkanClient(hDLL);
    if (!oapiRegisterGraphicsClient(g_client)) {
        delete g_client;
        g_client = nullptr;
    }
}

DLLCLBK void ExitModule(HINSTANCE hDLL)
{
    if (g_client) {
        oapiUnregisterGraphicsClient(g_client);
        delete g_client;
        g_client = nullptr;
    }
}

// ======================================================================
// class VulkanClient
// ======================================================================

VulkanClient::VulkanClient(HINSTANCE hInstance)
    : GraphicsClient(hInstance)
    , m_surface(VK_NULL_HANDLE)
    , m_viewportWidth(1920)
    , m_viewportHeight(1080)
    , m_imguiDescriptorPool(VK_NULL_HANDLE)
    , m_imguiInitialized(false)
    , m_frameInProgress(false)
    , m_firstUpdate(true)
    , m_sceneRendererInitialized(false)
    , m_meshPipelineInitialized(false)
{
    for (uint32_t i = 0; i < VulkanSwapchain::MAX_FRAMES_IN_FLIGHT; i++) {
        m_commandBuffers[i] = VK_NULL_HANDLE;
    }

    // Space black - more appropriate for space simulation
    m_clearColor[0] = 0.0f;
    m_clearColor[1] = 0.0f;
    m_clearColor[2] = 0.05f;
    m_clearColor[3] = 1.0f;
}

VulkanClient::~VulkanClient()
{
    // Wait for GPU before cleanup
    m_ctx.WaitIdle();

    // Clear vessel visuals (will clean up VulkanMeshes)
    m_vesselVisuals.clear();
    m_objToVisual.clear();

    // Clear surfaces/textures
    m_surfaces.clear();
    m_textureCache.clear();

    // Shutdown texture manager
    m_texManager.Shutdown();

    // Shutdown mesh pipeline
    if (m_meshPipelineInitialized) {
        m_meshPipeline.Shutdown();
        m_meshPipelineInitialized = false;
    }

    // Shutdown staging manager
    m_stagingManager.Shutdown();

    m_swapchain.Shutdown();

    if (m_surface != VK_NULL_HANDLE && m_ctx.GetInstance() != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_ctx.GetInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    m_ctx.Shutdown();
}

bool VulkanClient::clbkInitialise()
{
    // Call base class initialization
    if (!GraphicsClient::clbkInitialise()) {
        return false;
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Initializing (launchpad phase)..."));

    // Note: We defer Vulkan context creation until clbkCreateRenderWindow()
    // because we need a window surface to properly select a GPU with present support.

    oapiWriteLog(const_cast<char*>("VulkanClient: Initialization complete"));
    return true;
}

HWND VulkanClient::clbkCreateRenderWindow()
{
    oapiWriteLog(const_cast<char*>("VulkanClient: Creating render window..."));

    // Call base class to create the window
    HWND hWnd = GraphicsClient::clbkCreateRenderWindow();
    if (!hWnd) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to create window"));
        return nullptr;
    }

    // Set window title to indicate Vulkan is working
    SetWindowText(hWnd, "[VulkanClient]");

    // Use window size from base class (same as D3D9Client)
    RECT rect;
    GetClientRect(hWnd, &rect);
    m_viewportWidth = rect.right - rect.left;
    m_viewportHeight = rect.bottom - rect.top;

    // Fill window with black to avoid white flash (like D3D9Client does)
    HDC hDC = GetDC(hWnd);
    HBRUSH hBr = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hDC, &rect, hBr);
    DeleteObject(hBr);
    ReleaseDC(hWnd, hDC);
    ValidateRect(hWnd, NULL);

    // Ensure minimum window size for Vulkan
    if (m_viewportWidth == 0) m_viewportWidth = 800;
    if (m_viewportHeight == 0) m_viewportHeight = 600;

    char buf[256];
    sprintf_s(buf, "VulkanClient: Window created: %dx%d", m_viewportWidth, m_viewportHeight);
    oapiWriteLog(buf);

    // Initialize Vulkan context with surface extensions enabled
    VulkanContextCreateInfo ctxInfo{};
    ctxInfo.appName = "Orbiter Space Flight Simulator";
    ctxInfo.enableValidation = false;  // Disabled for debugging crash
    ctxInfo.enableSurface = true;
    ctxInfo.surfaceFactory = [this, hWnd](VkInstance instance) -> VkSurfaceKHR {
        VkWin32SurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hwnd = hWnd;
        surfaceInfo.hinstance = GetModuleHandle(nullptr);

        if (vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &m_surface) != VK_SUCCESS) {
            m_surface = VK_NULL_HANDLE;
        }
        return m_surface;
    };

    if (!m_ctx.Init(ctxInfo) || m_surface == VK_NULL_HANDLE) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to initialize Vulkan context"));
        return nullptr;
    }

    sprintf_s(buf, "VulkanClient: Using GPU: %s", m_ctx.GetGPUName().c_str());
    oapiWriteLog(buf);

    // Initialize swapchain
    if (!m_swapchain.Init(&m_ctx, m_surface, m_viewportWidth, m_viewportHeight)) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to initialize swapchain"));
        return nullptr;
    }
    oapiWriteLog(const_cast<char*>("VulkanClient: Swapchain initialized"));

    // Allocate command buffers
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_ctx.GetCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = VulkanSwapchain::MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(m_ctx.GetDevice(), &allocInfo, m_commandBuffers) != VK_SUCCESS) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to allocate command buffers"));
        return nullptr;
    }
    oapiWriteLog(const_cast<char*>("VulkanClient: Command buffers allocated"));

    // Initialize staging manager for GPU uploads
    if (!m_stagingManager.Init(&m_ctx)) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to initialize staging manager"));
        return nullptr;
    }
    oapiWriteLog(const_cast<char*>("VulkanClient: Staging manager initialized"));

    // Initialize SceneRenderer for 3D rendering
    VkExtent2D extent = m_swapchain.GetExtent();
    if (m_sceneRenderer.Init(&m_ctx, m_swapchain.GetRenderPass(), extent, &m_stagingManager)) {
        m_sceneRendererInitialized = true;
        oapiWriteLog(const_cast<char*>("VulkanClient: SceneRenderer initialized"));
    } else {
        oapiWriteLog(const_cast<char*>("VulkanClient: SceneRenderer initialization failed (optional)"));
    }

    // Initialize mesh pipeline for vessel rendering
    if (m_meshPipeline.Init(&m_ctx, m_swapchain.GetRenderPass(), &m_stagingManager)) {
        m_meshPipelineInitialized = true;
        oapiWriteLog(const_cast<char*>("VulkanClient: Mesh pipeline initialized"));

        // Initialize texture manager (requires mesh pipeline)
        SurfaceResolver resolver = [this](SURFHANDLE surf) -> VulkanTexture* {
            return this->GetTextureFromSurface(surf);
        };
        if (m_texManager.Init(&m_meshPipeline, &m_ctx, &m_stagingManager, resolver)) {
            oapiWriteLog(const_cast<char*>("VulkanClient: Texture manager initialized"));
        }
    } else {
        oapiWriteLog(const_cast<char*>("VulkanClient: Mesh pipeline initialization failed"));
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Render window ready"));

    // Ensure window has focus after all initialization (important for DirectInput)
    SetForegroundWindow(hWnd);
    SetFocus(hWnd);

    return hWnd;
}

void VulkanClient::clbkDestroyRenderWindow(bool fastclose)
{
    oapiWriteLog(const_cast<char*>("VulkanClient: Destroying render window..."));

    m_ctx.WaitIdle();

    // Clear vessel visuals first (before mesh pipeline shutdown)
    m_vesselVisuals.clear();
    m_objToVisual.clear();

    // Clear surfaces/textures
    m_surfaces.clear();
    m_textureCache.clear();

    // Shutdown texture manager
    m_texManager.Shutdown();

    // Shutdown mesh pipeline
    if (m_meshPipelineInitialized) {
        m_meshPipeline.Shutdown();
        m_meshPipelineInitialized = false;
    }

    // Shutdown SceneRenderer (uses pipeline and buffers)
    if (m_sceneRendererInitialized) {
        m_sceneRenderer.Shutdown();
        m_sceneRendererInitialized = false;
    }

    // Shutdown staging manager
    m_stagingManager.Shutdown();

    // Command buffers are freed when command pool is destroyed
    for (uint32_t i = 0; i < VulkanSwapchain::MAX_FRAMES_IN_FLIGHT; i++) {
        m_commandBuffers[i] = VK_NULL_HANDLE;
    }

    m_swapchain.Shutdown();

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_ctx.GetInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    GraphicsClient::clbkDestroyRenderWindow(fastclose);
}

void VulkanClient::clbkUpdate(bool running)
{
    // On first update, scan for existing objects (vessels, planets, etc.)
    // This is needed because clbkNewVessel is only called for vessels created
    // AFTER the graphics client starts - not for vessels loaded with the scenario
    if (m_firstUpdate && m_meshPipelineInitialized) {
        m_firstUpdate = false;

        DWORD nobj = oapiGetObjectCount();
        char buf[256];
        sprintf_s(buf, "VulkanClient: First update - scanning %lu objects", nobj);
        oapiWriteLog(buf);

        for (DWORD i = 0; i < nobj; i++) {
            OBJHANDLE hObj = oapiGetObjectByIndex(i);
            if (!hObj) continue;

            // Only create visuals for vessels (for now)
            if (oapiGetObjectType(hObj) == OBJTP_VESSEL) {
                clbkNewVessel(hObj);
            }
        }
    }
}

bool VulkanClient::clbkProcessKeyboardImmediate(char kstate[256], bool simRunning)
{
    // Workaround: DirectInput doesn't work with VulkanClient window, so we poll
    // keyboard state using GetAsyncKeyState and inject directly into kstate.
    // This callback is called DURING UserInput() so we can modify kstate directly.
    bool anyKey = false;

    // Poll common keys and convert to DirectInput format (0x80 = pressed)
    // Arrow keys for camera - directly rotate camera for faster response
    // Rotation speed in radians per frame (0.05 rad ≈ 3 degrees)
    const double rotSpeed = 0.05;

    if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
        kstate[DIK_LEFT] |= 0x80;
        oapiCameraRotAzimuth(-rotSpeed);  // Rotate left
        anyKey = true;
    }
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
        kstate[DIK_RIGHT] |= 0x80;
        oapiCameraRotAzimuth(rotSpeed);   // Rotate right
        anyKey = true;
    }
    if (GetAsyncKeyState(VK_UP) & 0x8000) {
        kstate[DIK_UP] |= 0x80;
        oapiCameraRotPolar(-rotSpeed);    // Rotate up
        anyKey = true;
    }
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
        kstate[DIK_DOWN] |= 0x80;
        oapiCameraRotPolar(rotSpeed);     // Rotate down
        anyKey = true;
    }

    // Modifier keys (always check, don't set anyKey)
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)   { kstate[DIK_LSHIFT] |= 0x80; }
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) { kstate[DIK_LCONTROL] |= 0x80; }
    if (GetAsyncKeyState(VK_MENU) & 0x8000)    { kstate[DIK_LALT] |= 0x80; }

    // Common function keys
    if (GetAsyncKeyState(VK_F1) & 0x8000) { kstate[DIK_F1] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F2) & 0x8000) { kstate[DIK_F2] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F3) & 0x8000) { kstate[DIK_F3] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F4) & 0x8000) { kstate[DIK_F4] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F5) & 0x8000) { kstate[DIK_F5] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F6) & 0x8000) { kstate[DIK_F6] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F7) & 0x8000) { kstate[DIK_F7] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F8) & 0x8000) { kstate[DIK_F8] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_F9) & 0x8000) { kstate[DIK_F9] |= 0x80; anyKey = true; }

    // R/T keys for time warp
    if (GetAsyncKeyState('R') & 0x8000) { kstate[DIK_R] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState('T') & 0x8000) { kstate[DIK_T] |= 0x80; anyKey = true; }

    // Page up/down for zoom
    if (GetAsyncKeyState(VK_PRIOR) & 0x8000) { kstate[DIK_PRIOR] |= 0x80; anyKey = true; }
    if (GetAsyncKeyState(VK_NEXT) & 0x8000)  { kstate[DIK_NEXT] |= 0x80; anyKey = true; }

    // Debug logging - log every key event (rate limited)
    if (anyKey) {
        static int keyLogCount = 0;
        if (keyLogCount < 20) {
            char buf[128];
            sprintf(buf, "KEY: L:%d R:%d U:%d D:%d  SimRunning:%d",
                (kstate[DIK_LEFT] & 0x80) != 0, (kstate[DIK_RIGHT] & 0x80) != 0,
                (kstate[DIK_UP] & 0x80) != 0, (kstate[DIK_DOWN] & 0x80) != 0,
                simRunning ? 1 : 0);
            oapiWriteLog(buf);
            keyLogCount++;
        }
    }

    return false;  // Don't consume - let Orbiter process the keys
}

void VulkanClient::clbkRenderScene()
{
    static int frameCount = 0;
    frameCount++;

    // Log first 5 and then periodically to verify rendering continues
    if (frameCount <= 5 || frameCount % 500 == 0) {
        char buf[128];
        sprintf(buf, "VulkanClient: clbkRenderScene called (frame %d, swapchain=%d)",
            frameCount, m_swapchain.IsInitialized());
        oapiWriteLog(buf);
    }

    if (!m_swapchain.IsInitialized()) {
        return;
    }

    // Update camera from Orbiter
    if (m_sceneRendererInitialized) {
        MATRIX3 rotMatrix;
        oapiCameraRotationMatrix(&rotMatrix);
        double aperture = oapiCameraAperture();

        // Debug: log camera info when it changes
        static double lastM11 = 0, lastM12 = 0, lastM13 = 0;
        if (fabs(rotMatrix.m11 - lastM11) > 0.001 ||
            fabs(rotMatrix.m12 - lastM12) > 0.001 ||
            fabs(rotMatrix.m13 - lastM13) > 0.001) {
            char buf[256];
            sprintf(buf, "CAM: rot[0]=(%.3f,%.3f,%.3f) aperture=%.1f deg",
                rotMatrix.m11, rotMatrix.m12, rotMatrix.m13, aperture * 180.0 / 3.14159);
            oapiWriteLog(buf);
            lastM11 = rotMatrix.m11;
            lastM12 = rotMatrix.m12;
            lastM13 = rotMatrix.m13;
        }

        // Pass to scene renderer - uses rotation matrix directly
        // Near plane: 1m (reasonable for spacecraft)
        // Far plane: 1e9m (to see distant planets/stars)
        m_sceneRenderer.SetCameraFromOrbiter(rotMatrix.data, aperture, 1.0, 1e9);
    }

    // Acquire next swapchain image
    if (!m_swapchain.AcquireNextImage()) {
        // Swapchain needs recreation (window resized, etc.)
        HWND hWnd = GetRenderWindow();
        if (hWnd) {
            RECT rect;
            GetClientRect(hWnd, &rect);
            uint32_t width = rect.right - rect.left;
            uint32_t height = rect.bottom - rect.top;

            if (width > 0 && height > 0) {
                oapiWriteLog(const_cast<char*>("VulkanClient: Recreating swapchain"));
                m_swapchain.Recreate(width, height);
                m_viewportWidth = width;
                m_viewportHeight = height;
            }
        }
        return;
    }

    if (frameCount == 1) {
        oapiWriteLog(const_cast<char*>("VulkanClient: First frame rendering"));
    }

    uint32_t frameIndex = m_swapchain.GetCurrentFrame();
    VkCommandBuffer cmd = m_commandBuffers[frameIndex];

    // Begin command buffer recording
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin render pass with clear color and depth
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkExtent2D extent = m_swapchain.GetExtent();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_swapchain.GetRenderPass();
    rpBegin.framebuffer = m_swapchain.GetCurrentFramebuffer();
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Render vessel meshes
    if (m_meshPipelineInitialized && !m_vesselVisuals.empty()) {
        // Get camera info
        VECTOR3 camPos, camDir;
        MATRIX3 camRot;
        oapiCameraGlobalPos(&camPos);
        oapiCameraGlobalDir(&camDir);
        oapiCameraRotationMatrix(&camRot);

        // Build view matrix from camera rotation
        // D3D uses SetRotation (direct) then transposes for Vulkan column-major
        // Vulkan View = D3D_View^T = MATRIX3 columns as Vulkan columns
        // No Z negation - work in Orbiter's coordinate system like D3D9
        float view[16] = {
            (float)camRot.m11, (float)camRot.m21, (float)camRot.m31, 0.0f,
            (float)camRot.m12, (float)camRot.m22, (float)camRot.m32, 0.0f,
            (float)camRot.m13, (float)camRot.m23, (float)camRot.m33, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        // Build projection matrix (Vulkan conventions: Y-flip, Z range [0, 1])
        double aperture = oapiCameraAperture();
        float fov = (float)(aperture * 2.0);  // Full FOV
        float aspect = (float)m_viewportWidth / (float)m_viewportHeight;
        float nearZ = 1.0f;
        float farZ = 1e8f;
        float tanHalfFov = tanf(fov / 2.0f);

        float proj[16] = { 0 };
        proj[0] = 1.0f / (aspect * tanHalfFov);
        proj[5] = -1.0f / tanHalfFov;  // Negative for Vulkan Y-flip
        proj[10] = farZ / (nearZ - farZ);
        proj[11] = -1.0f;
        proj[14] = (nearZ * farZ) / (nearZ - farZ);

        // Light direction (from sun - simplified for now)
        float lightDir[4] = { 0.577f, 0.577f, -0.577f, 0.0f };

        // Bind mesh pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipeline.GetPipeline());

        // Render each vessel
        for (auto& kv : m_vesselVisuals) {
            VesselVisual* vv = kv.second.get();
            if (!vv || vv->meshes.empty()) continue;

            VESSEL* vessel = oapiGetVesselInterface(vv->hObj);
            if (!vessel) continue;

            // Get vessel position and orientation
            VECTOR3 vesselPos;
            MATRIX3 vesselRot;
            vessel->GetGlobalPos(vesselPos);
            vessel->GetRotationMatrix(vesselRot);

            // Calculate relative position from camera
            VECTOR3 relPos = {
                vesselPos.x - camPos.x,
                vesselPos.y - camPos.y,
                vesselPos.z - camPos.z
            };

            // Build model matrix from vessel orientation
            // D3D uses SetInvRotation (transposed) then transposes again for Vulkan
            // Vulkan Model = D3D_Model^T = (MATRIX3^T)^T = MATRIX3 rows as Vulkan columns
            // No Z negation - work in Orbiter's coordinate system like D3D9
            float model[16] = {
                (float)vesselRot.m11, (float)vesselRot.m12, (float)vesselRot.m13, 0.0f,
                (float)vesselRot.m21, (float)vesselRot.m22, (float)vesselRot.m23, 0.0f,
                (float)vesselRot.m31, (float)vesselRot.m32, (float)vesselRot.m33, 0.0f,
                (float)relPos.x, (float)relPos.y, (float)relPos.z, 1.0f
            };

            // Compute MVP = Proj * View * Model (column-major)
            float mv[16];
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    mv[col*4+row] = 0;
                    for (int k = 0; k < 4; k++) {
                        mv[col*4+row] += view[k*4+row] * model[col*4+k];
                    }
                }
            }

            float mvp[16];
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    mvp[col*4+row] = 0;
                    for (int k = 0; k < 4; k++) {
                        mvp[col*4+row] += proj[k*4+row] * mv[col*4+k];
                    }
                }
            }

            // Render each mesh in vessel
            for (size_t meshIdx = 0; meshIdx < vv->meshes.size(); meshIdx++) {
                auto& mesh = vv->meshes[meshIdx];
                if (!mesh || !mesh->IsUploaded()) continue;

                MESHHANDLE hMesh = (meshIdx < vv->meshHandles.size()) ? vv->meshHandles[meshIdx] : nullptr;

                // Check for per-mesh offset transform
                float meshModel[16];
                float meshMvp[16];
                const float* useModel = model;
                const float* useMvp = mvp;

                if (meshIdx < vv->meshTransforms.size() && vv->meshTransforms[meshIdx].hasOffset) {
                    const MeshTransform& mt = vv->meshTransforms[meshIdx];

                    // Create offset matrix (translation only, column-major)
                    // No Z negation - work in Orbiter's coordinate system like D3D9
                    float offsetMat[16] = {
                        1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        mt.offset[0], mt.offset[1], mt.offset[2], 1.0f
                    };

                    // meshModel = model * offsetMat (column-major multiplication)
                    for (int col = 0; col < 4; col++) {
                        for (int row = 0; row < 4; row++) {
                            meshModel[col*4+row] = 0;
                            for (int k = 0; k < 4; k++) {
                                meshModel[col*4+row] += model[k*4+row] * offsetMat[col*4+k];
                            }
                        }
                    }

                    // Recompute mv and mvp with the offset model matrix
                    float meshMv[16];
                    for (int col = 0; col < 4; col++) {
                        for (int row = 0; row < 4; row++) {
                            meshMv[col*4+row] = 0;
                            for (int k = 0; k < 4; k++) {
                                meshMv[col*4+row] += view[k*4+row] * meshModel[col*4+k];
                            }
                        }
                    }

                    for (int col = 0; col < 4; col++) {
                        for (int row = 0; row < 4; row++) {
                            meshMvp[col*4+row] = 0;
                            for (int k = 0; k < 4; k++) {
                                meshMvp[col*4+row] += proj[k*4+row] * meshMv[col*4+k];
                            }
                        }
                    }

                    useModel = meshModel;
                    useMvp = meshMvp;
                }

                // Bind vertex/index buffers
                VkBuffer vertexBuffers[] = { mesh->GetVertexBuffer() };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(cmd, mesh->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT16);

                for (uint32_t g = 0; g < mesh->GetGroupCount(); g++) {
                    const MeshGroup* grp = mesh->GetGroup(g);
                    if (!grp || grp->indexCount == 0) continue;

                    // Get material for this group
                    float matDiffuse[4] = { 0.7f, 0.7f, 0.7f, 1.0f };
                    float matEmissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

                    const MeshMaterial* mat = mesh->GetMaterial(grp->materialIdx);
                    if (mat) {
                        memcpy(matDiffuse, mat->diffuse, sizeof(float) * 4);
                        memcpy(matEmissive, mat->emissive, sizeof(float) * 4);
                    }

                    // Get texture for this group
                    // Note: oapiGetTextureHandle uses 1-based indexing
                    // Mesh group textureIdx is 0-based, so we add 1 (like D3D9Client does)
                    VkDescriptorSet texDesc = VK_NULL_HANDLE;
                    if (hMesh) {
                        SURFHANDLE hTex = oapiGetTextureHandle(hMesh, grp->textureIdx + 1);
                        if (hTex) {
                            texDesc = m_texManager.GetTextureDescriptor(hTex);
                        }
                    }

                    // Use default texture if none available
                    if (texDesc == VK_NULL_HANDLE) {
                        texDesc = m_meshPipeline.GetDefaultTextureDescriptor();
                    }

                    // Set up push constants
                    MeshPushConstants pc;
                    memcpy(pc.mvp, useMvp, sizeof(float) * 16);
                    memcpy(pc.model, useModel, sizeof(float) * 16);
                    memcpy(pc.lightDir, lightDir, sizeof(float) * 4);
                    memcpy(pc.matDiffuse, matDiffuse, sizeof(float) * 4);
                    memcpy(pc.matEmissive, matEmissive, sizeof(float) * 4);
                    pc.matEmissive[3] = (texDesc != m_meshPipeline.GetDefaultTextureDescriptor()) ? 1.0f : 0.0f;

                    vkCmdPushConstants(cmd, m_meshPipeline.GetLayout(),
                                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                      0, sizeof(MeshPushConstants), &pc);

                    // Bind texture descriptor
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           m_meshPipeline.GetLayout(), 0, 1, &texDesc, 0, nullptr);

                    // Draw the group
                    vkCmdDrawIndexed(cmd, grp->indexCount, 1, grp->indexOffset, grp->vertexOffset, 0);
                }
            }
        }
    }

    // Mark frame as in progress - clbkDisplayFrame will check this
    m_frameInProgress = true;

    // Note: Render pass is left open for ImGui to render into
    // It will be closed and submitted in clbkDisplayFrame()
}

bool VulkanClient::clbkDisplayFrame()
{
    static int displayCount = 0;
    displayCount++;

    // Log periodically to monitor rendering
    if (displayCount <= 5 || displayCount % 500 == 0) {
        char buf[128];
        sprintf(buf, "FRAME: %d (swapchain=%d, inProgress=%d)",
            displayCount, m_swapchain.IsInitialized(), m_frameInProgress);
        oapiWriteLog(buf);
    }

    if (!m_swapchain.IsInitialized() || !m_frameInProgress) {
        return true;  // Always return true - D3D9Client does this too
    }

    m_frameInProgress = false;

    uint32_t frameIndex = m_swapchain.GetCurrentFrame();
    VkCommandBuffer cmd = m_commandBuffers[frameIndex];

    // End render pass and command buffer
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit command buffer
    VkSemaphore waitSemaphores[] = { m_swapchain.GetImageAvailableSemaphore() };
    VkSemaphore signalSemaphores[] = { m_swapchain.GetRenderFinishedSemaphore() };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(m_ctx.GetGraphicsQueue(), 1, &submitInfo, m_swapchain.GetInFlightFence());

    // Present
    if (!m_swapchain.Present()) {
        // Swapchain will be recreated on next frame
        HWND hWnd = GetRenderWindow();
        if (hWnd) {
            RECT rect;
            GetClientRect(hWnd, &rect);
            uint32_t width = rect.right - rect.left;
            uint32_t height = rect.bottom - rect.top;
            if (width > 0 && height > 0) {
                oapiWriteLog(const_cast<char*>("VulkanClient: Recreating swapchain after present"));
                m_swapchain.Recreate(width, height);
                m_viewportWidth = width;
                m_viewportHeight = height;
            }
        }
    } else {
        m_swapchain.AdvanceFrame();
    }

    return true;
}

void VulkanClient::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex)
{
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{ m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3] }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkExtent2D extent = m_swapchain.GetExtent();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_swapchain.GetRenderPass();
    rpBegin.framebuffer = m_swapchain.GetCurrentFramebuffer();
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Future: Draw calls go here

    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);
}

void VulkanClient::clbkGetViewportSize(DWORD *width, DWORD *height) const
{
    *width = m_viewportWidth;
    *height = m_viewportHeight;
}

bool VulkanClient::clbkGetRenderParam(DWORD param, DWORD *value) const
{
    switch (param) {
        case RP_COLOURDEPTH:
            *value = 32;  // BGRA8
            return true;
        case RP_ZBUFFERDEPTH:
            *value = 24;  // D24S8
            return true;
        case RP_STENCILDEPTH:
            *value = 8;   // D24S8
            return true;
        case RP_MAXLIGHTS:
            *value = 8;   // Reasonable default
            return true;
        case RP_ISTLDEVICE:
            *value = 1;   // Yes, we support T&L
            return true;
        case RP_REQUIRETEXPOW2:
            *value = 0;   // No, Vulkan doesn't require power-of-2 textures
            return true;
        default:
            *value = 0;
            return false;
    }
}

// ======================================================================
// ImGui implementation
// ======================================================================

// Callback for ImGui multi-viewport support - creates VkSurface for secondary windows
// This is needed because imgui_impl_win32.cpp doesn't include Vulkan headers
static int ImGui_ImplWin32_CreateVkSurface(ImGuiViewport* viewport, ImU64 vk_instance, const void* vk_allocator, ImU64* out_vk_surface)
{
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)viewport->PlatformHandleRaw;
    createInfo.hinstance = ::GetModuleHandle(nullptr);
    return (int)vkCreateWin32SurfaceKHR((VkInstance)vk_instance, &createInfo, (VkAllocationCallbacks*)vk_allocator, (VkSurfaceKHR*)out_vk_surface);
}

void VulkanClient::clbkImGuiInit()
{
    if (m_imguiInitialized) {
        return;
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Initializing ImGui..."));

    // Create descriptor pool for ImGui (match backend example sizes)
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    const uint32_t poolCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.maxSets = 100 * poolCount;
    poolInfo.poolSizeCount = poolCount;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_ctx.GetDevice(), &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to create ImGui descriptor pool"));
        return;
    }

    // Note: Orbiter's DialogManager handles ImGui_ImplWin32_Init() before calling us
    // We only need to initialize the Vulkan backend

    // Set up the callback for creating VkSurface objects for multi-viewport support
    // This must be done after ImGui_ImplWin32_Init and before ImGui_ImplVulkan_Init
    ImGui::GetPlatformIO().Platform_CreateVkSurface = ImGui_ImplWin32_CreateVkSurface;

    // Initialize ImGui for Vulkan
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = m_ctx.GetInstance();
    initInfo.PhysicalDevice = m_ctx.GetPhysicalDevice();
    initInfo.Device = m_ctx.GetDevice();
    initInfo.QueueFamily = m_ctx.GetGraphicsQueueFamily();
    initInfo.Queue = m_ctx.GetGraphicsQueue();
    initInfo.DescriptorPool = m_imguiDescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = m_swapchain.GetImageCount();
    initInfo.PipelineInfoMain.RenderPass = m_swapchain.GetRenderPass();
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        oapiWriteLog(const_cast<char*>("VulkanClient: Failed to initialize ImGui Vulkan"));
        return;
    }

    m_imguiInitialized = true;
    oapiWriteLog(const_cast<char*>("VulkanClient: ImGui initialized successfully"));
}

void VulkanClient::clbkImGuiShutdown()
{
    if (!m_imguiInitialized) {
        return;
    }

    oapiWriteLog(const_cast<char*>("VulkanClient: Shutting down ImGui..."));

    m_ctx.WaitIdle();

    // Note: Orbiter's DialogManager handles ImGui_ImplWin32_Shutdown() after calling us
    ImGui_ImplVulkan_Shutdown();

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_ctx.GetDevice(), m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }

    m_imguiInitialized = false;
    oapiWriteLog(const_cast<char*>("VulkanClient: ImGui shutdown complete"));
}

void VulkanClient::clbkImGuiNewFrame()
{
    if (!m_imguiInitialized) {
        return;
    }

    // Note: Orbiter's DialogManager handles ImGui_ImplWin32_NewFrame() after calling us
    ImGui_ImplVulkan_NewFrame();
}

void VulkanClient::clbkImGuiRenderDrawData()
{
    if (!m_imguiInitialized || !m_frameInProgress) {
        return;
    }

    ImGui::Render();

    uint32_t frameIndex = m_swapchain.GetCurrentFrame();
    VkCommandBuffer cmd = m_commandBuffers[frameIndex];

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    // Update and Render additional Platform Windows (required when ViewportsEnable is set)
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

// ======================================================================
// Vessel management - create/delete visuals
// ======================================================================

void VulkanClient::clbkNewVessel(OBJHANDLE hVessel)
{
    if (!m_meshPipelineInitialized) {
        oapiWriteLog(const_cast<char*>("VulkanClient: clbkNewVessel - mesh pipeline not ready"));
        return;
    }

    // Check if we already have a visual for this vessel
    if (m_objToVisual.find(hVessel) != m_objToVisual.end()) {
        return;
    }

    // Get vessel interface
    VESSEL* vessel = oapiGetVesselInterface(hVessel);
    if (!vessel) {
        oapiWriteLog(const_cast<char*>("VulkanClient: clbkNewVessel - no vessel interface"));
        return;
    }

    // Create a new vessel visual record
    auto vv = std::make_unique<VesselVisual>();
    vv->hObj = hVessel;

    // Use the VesselVisual pointer as the VISHANDLE (this is opaque to Orbiter)
    VISHANDLE vis = reinterpret_cast<VISHANDLE>(vv.get());

    // Register with Orbiter so we receive clbkVisEvent calls (for mesh updates)
    RegisterVisObject(hVessel, vis);

    // Store in our maps
    m_objToVisual[hVessel] = vis;
    VesselVisual* vvPtr = vv.get();  // Keep pointer before move
    m_vesselVisuals[vis] = std::move(vv);

    char buf[256];
    char vesselName[128];
    oapiGetObjectName(hVessel, vesselName, sizeof(vesselName));
    sprintf_s(buf, "VulkanClient: Registered visual for vessel '%s' (vis=%p)", vesselName, vis);
    oapiWriteLog(buf);

    // Load existing meshes (like D3D9Client does in VVessel constructor)
    DWORD nmesh = vessel->GetMeshCount();
    if (nmesh > 0) {
        vvPtr->meshes.resize(nmesh);
        vvPtr->meshHandles.resize(nmesh, nullptr);
        vvPtr->meshTransforms.resize(nmesh);

        for (DWORD idx = 0; idx < nmesh; idx++) {
            MESHHANDLE hMesh = vessel->GetMeshTemplate(idx);
            if (!hMesh) {
                continue;
            }

            vvPtr->meshHandles[idx] = hMesh;

            // Load mesh offset if any (like D3D9Client does)
            VECTOR3 ofs;
            vessel->GetMeshOffset(idx, ofs);
            if (length(ofs) > 0) {
                vvPtr->meshTransforms[idx].offset[0] = (float)ofs.x;
                vvPtr->meshTransforms[idx].offset[1] = (float)ofs.y;
                vvPtr->meshTransforms[idx].offset[2] = (float)ofs.z;
                vvPtr->meshTransforms[idx].hasOffset = true;
            }

            DWORD nGroups = oapiMeshGroupCount(hMesh);
            if (nGroups == 0) {
                continue;
            }

            auto vulkanMesh = std::make_unique<VulkanMesh>();
            if (!vulkanMesh->Init(&m_ctx, &m_stagingManager)) {
                continue;
            }

            // Convert each mesh group
            for (DWORD g = 0; g < nGroups; g++) {
                MESHGROUPEX* grp = oapiMeshGroupEx(hMesh, g);
                if (!grp || !grp->Vtx || grp->nVtx == 0) {
                    continue;
                }

                // NTVERTEX and MeshVertex have identical layout
                static_assert(sizeof(NTVERTEX) == sizeof(MeshVertex),
                    "NTVERTEX and MeshVertex must have same size");

                const MeshVertex* vertices = reinterpret_cast<const MeshVertex*>(grp->Vtx);

                vulkanMesh->AddGroup(
                    vertices, grp->nVtx,
                    grp->Idx, grp->nIdx,
                    grp->MtrlIdx, grp->TexIdx,
                    grp->UsrFlag, grp->zBias, grp->Flags
                );
            }

            // Load materials
            DWORD nMtrl = oapiMeshMaterialCount(hMesh);
            for (DWORD m = 0; m < nMtrl; m++) {
                MATERIAL* mat = oapiMeshMaterial(hMesh, m);
                if (mat) {
                    MeshMaterial vm;
                    memcpy(vm.diffuse, &mat->diffuse, sizeof(float) * 4);
                    memcpy(vm.ambient, &mat->ambient, sizeof(float) * 4);
                    memcpy(vm.specular, &mat->specular, sizeof(float) * 4);
                    memcpy(vm.emissive, &mat->emissive, sizeof(float) * 4);
                    vm.power = mat->power;
                    vulkanMesh->AddMaterial(vm);
                }
            }

            // Upload to GPU
            if (vulkanMesh->Upload()) {
                vvPtr->meshes[idx] = std::move(vulkanMesh);
            }
        }

        sprintf_s(buf, "VulkanClient: Loaded %lu meshes for vessel '%s'", nmesh, vesselName);
        oapiWriteLog(buf);
    }
}

void VulkanClient::clbkDeleteVessel(OBJHANDLE hVessel)
{
    auto it = m_objToVisual.find(hVessel);
    if (it == m_objToVisual.end()) {
        return;
    }

    VISHANDLE vis = it->second;

    // Unregister from Orbiter
    UnregisterVisObject(hVessel);

    // Remove from our maps
    m_vesselVisuals.erase(vis);
    m_objToVisual.erase(it);

    char buf[256];
    sprintf_s(buf, "VulkanClient: Deleted visual for vessel %p", hVessel);
    oapiWriteLog(buf);
}

// ======================================================================
// Visual events - mesh loading
// ======================================================================

int VulkanClient::clbkVisEvent(OBJHANDLE hObj, VISHANDLE vis, DWORD msg, DWORD_PTR context)
{
    if (!m_meshPipelineInitialized) {
        return 0;
    }

    switch (msg) {
        case EVENT_VESSEL_INSMESH: {
            // Insert mesh at index 'context'
            UINT meshIdx = static_cast<UINT>(context);

            // Get the vessel interface
            VESSEL* vessel = oapiGetVesselInterface(hObj);
            if (!vessel) {
                return 0;
            }

            // Get mesh template
            MESHHANDLE hMesh = vessel->GetMeshTemplate(meshIdx);
            if (!hMesh) {
                return 0;
            }

            // Get or create vessel visual
            auto it = m_vesselVisuals.find(vis);
            if (it == m_vesselVisuals.end()) {
                auto vv = std::make_unique<VesselVisual>();
                vv->hObj = hObj;
                it = m_vesselVisuals.emplace(vis, std::move(vv)).first;
            }
            VesselVisual* vv = it->second.get();

            // Ensure meshes vectors are large enough
            if (vv->meshes.size() <= meshIdx) {
                vv->meshes.resize(meshIdx + 1);
                vv->meshHandles.resize(meshIdx + 1, nullptr);
                vv->meshTransforms.resize(meshIdx + 1);
            }

            vv->meshHandles[meshIdx] = hMesh;

            // Load mesh offset if any
            VECTOR3 ofs;
            vessel->GetMeshOffset(meshIdx, ofs);
            if (length(ofs) > 0) {
                vv->meshTransforms[meshIdx].offset[0] = (float)ofs.x;
                vv->meshTransforms[meshIdx].offset[1] = (float)ofs.y;
                vv->meshTransforms[meshIdx].offset[2] = (float)ofs.z;
                vv->meshTransforms[meshIdx].hasOffset = true;
            } else {
                vv->meshTransforms[meshIdx].hasOffset = false;
            }

            // Create VulkanMesh from mesh template
            DWORD nGroups = oapiMeshGroupCount(hMesh);
            if (nGroups == 0) {
                return 0;
            }

            auto vulkanMesh = std::make_unique<VulkanMesh>();
            if (!vulkanMesh->Init(&m_ctx, &m_stagingManager)) {
                return 0;
            }

            for (DWORD g = 0; g < nGroups; g++) {
                MESHGROUPEX* grp = oapiMeshGroupEx(hMesh, g);
                if (!grp || !grp->Vtx || grp->nVtx == 0) {
                    continue;
                }

                const MeshVertex* vertices = reinterpret_cast<const MeshVertex*>(grp->Vtx);

                vulkanMesh->AddGroup(
                    vertices, grp->nVtx,
                    grp->Idx, grp->nIdx,
                    grp->MtrlIdx, grp->TexIdx,
                    grp->UsrFlag, grp->zBias, grp->Flags
                );
            }

            if (vulkanMesh->Upload()) {
                vv->meshes[meshIdx] = std::move(vulkanMesh);
            }
            break;
        }

        case EVENT_VESSEL_DELMESH: {
            auto it = m_vesselVisuals.find(vis);
            if (it == m_vesselVisuals.end()) {
                return 0;
            }
            VesselVisual* vv = it->second.get();

            if (context == static_cast<DWORD_PTR>(-1)) {
                vv->meshes.clear();
                vv->meshTransforms.clear();
            } else {
                UINT meshIdx = static_cast<UINT>(context);
                if (meshIdx < vv->meshes.size()) {
                    vv->meshes[meshIdx].reset();
                }
                if (meshIdx < vv->meshTransforms.size()) {
                    vv->meshTransforms[meshIdx].hasOffset = false;
                }
            }
            break;
        }

        case EVENT_VESSEL_MESHOFS: {
            // Mesh offset changed at index 'context'
            UINT meshIdx = static_cast<UINT>(context);

            VESSEL* vessel = oapiGetVesselInterface(hObj);
            if (!vessel) {
                return 0;
            }

            auto it = m_vesselVisuals.find(vis);
            if (it == m_vesselVisuals.end()) {
                return 0;
            }
            VesselVisual* vv = it->second.get();

            // Ensure transform array is large enough
            if (vv->meshTransforms.size() <= meshIdx) {
                vv->meshTransforms.resize(meshIdx + 1);
            }

            // Update the mesh offset
            VECTOR3 ofs;
            vessel->GetMeshOffset(meshIdx, ofs);
            if (length(ofs) > 0) {
                vv->meshTransforms[meshIdx].offset[0] = (float)ofs.x;
                vv->meshTransforms[meshIdx].offset[1] = (float)ofs.y;
                vv->meshTransforms[meshIdx].offset[2] = (float)ofs.z;
                vv->meshTransforms[meshIdx].hasOffset = true;
            } else {
                vv->meshTransforms[meshIdx].hasOffset = false;
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

// ======================================================================
// Texture/Surface loading
// ======================================================================

SURFHANDLE VulkanClient::clbkLoadTexture(const char* fname, DWORD flags)
{
    if (!fname || !m_meshPipelineInitialized) {
        return nullptr;
    }

    // Build full path to texture file
    std::string fullPath;
    if (fname[0] == '/' || fname[0] == '\\' || (fname[1] == ':')) {
        fullPath = fname;
    } else {
        fullPath = std::string("Textures/") + fname;
    }

    // Check cache first (bit 3 of flags = store in repository)
    if (flags & 0x8) {
        auto cacheIt = m_textureCache.find(fullPath);
        if (cacheIt != m_textureCache.end()) {
            auto surfIt = m_surfaces.find(cacheIt->second);
            if (surfIt != m_surfaces.end()) {
                surfIt->second->refCount++;
                return cacheIt->second;
            }
        }
    }

    // Create new texture
    auto surfHandle = std::make_unique<SurfaceHandle>();
    surfHandle->texture = std::make_unique<VulkanTexture>();
    surfHandle->filename = fullPath;
    surfHandle->flags = flags;

    // Try loading the texture file
    if (!surfHandle->texture->CreateFromFile(&m_ctx, &m_stagingManager, fullPath.c_str())) {
        // Try without Textures/ prefix
        if (!surfHandle->texture->CreateFromFile(&m_ctx, &m_stagingManager, fname)) {
            char buf[512];
            sprintf_s(buf, "VulkanClient: Failed to load texture: %s", fname);
            oapiWriteLog(buf);
            return nullptr;
        }
    }

    surfHandle->width = surfHandle->texture->GetWidth();
    surfHandle->height = surfHandle->texture->GetHeight();

    SURFHANDLE hSurf = reinterpret_cast<SURFHANDLE>(surfHandle.get());

    if (flags & 0x8) {
        m_textureCache[fullPath] = hSurf;
    }

    m_surfaces[hSurf] = std::move(surfHandle);

    char buf[512];
    sprintf_s(buf, "VulkanClient: Loaded texture: %s (%ux%u)",
              fname, m_surfaces[hSurf]->width, m_surfaces[hSurf]->height);
    oapiWriteLog(buf);

    return hSurf;
}

void VulkanClient::clbkReleaseTexture(SURFHANDLE hTex)
{
    clbkReleaseSurface(hTex);
}

bool VulkanClient::clbkReleaseSurface(SURFHANDLE surf)
{
    if (!surf) {
        return false;
    }

    auto it = m_surfaces.find(surf);
    if (it == m_surfaces.end()) {
        return false;
    }

    it->second->refCount--;
    if (it->second->refCount <= 0) {
        // Remove from texture cache
        for (auto cacheIt = m_textureCache.begin(); cacheIt != m_textureCache.end(); ) {
            if (cacheIt->second == surf) {
                cacheIt = m_textureCache.erase(cacheIt);
            } else {
                ++cacheIt;
            }
        }

        m_texManager.InvalidateTexture(surf);
        m_surfaces.erase(it);
    }

    return true;
}

bool VulkanClient::clbkGetSurfaceSize(SURFHANDLE surf, DWORD* w, DWORD* h)
{
    if (!surf || !w || !h) {
        *w = *h = 0;
        return false;
    }

    auto it = m_surfaces.find(surf);
    if (it == m_surfaces.end()) {
        *w = *h = 0;
        return false;
    }

    *w = it->second->width;
    *h = it->second->height;
    return true;
}

void VulkanClient::clbkIncrSurfaceRef(SURFHANDLE surf)
{
    if (!surf) {
        return;
    }

    auto it = m_surfaces.find(surf);
    if (it != m_surfaces.end()) {
        it->second->refCount++;
    }
}

VulkanTexture* VulkanClient::GetTextureFromSurface(SURFHANDLE surf) const
{
    if (!surf) {
        return nullptr;
    }

    auto it = m_surfaces.find(surf);
    if (it == m_surfaces.end()) {
        return nullptr;
    }

    return it->second->texture.get();
}
