// ==============================================================
// HeadlessGraphicsClient.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#define OAPI_IMPLEMENTATION

#include "HeadlessGraphicsClient.h"
#include "OrbiterAPI.h"
#include "VesselAPI.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>

// ImGui disabled in headless mode - no includes needed

// ======================================================================
// Module interface
// ======================================================================

static HeadlessGraphicsClient* g_client = nullptr;

DLLCLBK void InitModule(HINSTANCE hDLL)
{
    // Only register as graphics client if explicitly requested via --plugin=HeadlessGraphicsClient
    // This prevents HeadlessGraphicsClient from taking over when VulkanClient should be used
    const char* cmdLine = GetCommandLineA();
    if (!cmdLine || strstr(cmdLine, "HeadlessGraphicsClient") == nullptr) {
        // Not explicitly requested - don't register as graphics client
        return;
    }

    g_client = new HeadlessGraphicsClient(hDLL);
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
// class HeadlessGraphicsClient
// ======================================================================

// Window procedure for hidden window
static LRESULT CALLBACK HeadlessWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

HeadlessGraphicsClient::HeadlessGraphicsClient(HINSTANCE hInstance)
    : GraphicsClient(hInstance)
    , m_hWnd(nullptr)
    , m_windowClass(0)
    , m_viewportWidth(1920)
    , m_viewportHeight(1080)
    , m_frameCount(0)
    , m_frameInProgress(false)
    , m_imguiDescriptorPool(VK_NULL_HANDLE)
    , m_imguiInitialized(false)
{
}

HeadlessGraphicsClient::~HeadlessGraphicsClient()
{
    m_renderer.Shutdown();
    DestroyHiddenWindow();
}

bool HeadlessGraphicsClient::CreateHiddenWindow()
{
    HINSTANCE hInst = ModuleInstance();

    // Register window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HeadlessWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "HeadlessGraphicsClient";

    m_windowClass = RegisterClassEx(&wc);
    if (!m_windowClass) {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Failed to register window class"));
        return false;
    }

    // Create message-only window (HWND_MESSAGE parent) to minimize message traffic
    // This window won't receive broadcast messages or appear on screen
    m_hWnd = CreateWindowEx(
        0,                              // No extended styles
        "HeadlessGraphicsClient",       // Class name
        "Headless Orbiter",             // Window title
        WS_POPUP,                       // Minimal popup style
        0, 0,                           // Position
        1, 1,                           // Minimal size
        HWND_MESSAGE,                   // Message-only window
        nullptr,                        // No menu
        hInst,
        nullptr                         // No extra data
    );

    if (!m_hWnd) {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Failed to create hidden window"));
        return false;
    }

    // No need to hide - message-only windows are invisible by design

    oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Hidden window created"));
    return true;
}

void HeadlessGraphicsClient::DestroyHiddenWindow()
{
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    if (m_windowClass) {
        UnregisterClass("HeadlessGraphicsClient", ModuleInstance());
        m_windowClass = 0;
    }
}

bool HeadlessGraphicsClient::clbkInitialise()
{
    // Call base class initialization
    if (!GraphicsClient::clbkInitialise()) {
        return false;
    }

    oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Initializing..."));

    // Note: HeadlessRenderer initialization is deferred to clbkCreateRenderWindow()
    // This matches the VulkanClient pattern.

    oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Initialization complete"));
    return true;
}

HWND HeadlessGraphicsClient::clbkCreateRenderWindow()
{
    oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Creating headless render context..."));

    // Create hidden window (required for Orbiter's render loop to function)
    if (!CreateHiddenWindow()) {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Failed to create hidden window"));
        return nullptr;
    }

    // Initialize HeadlessRenderer with desired resolution
    if (!m_renderer.Init(m_viewportWidth, m_viewportHeight)) {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Failed to initialize HeadlessRenderer"));
        DestroyHiddenWindow();
        return nullptr;
    }

    char buf[256];
    sprintf_s(buf, "HeadlessGraphicsClient: Render buffer created: %dx%d",
              m_viewportWidth, m_viewportHeight);
    oapiWriteLog(buf);

    sprintf_s(buf, "HeadlessGraphicsClient: Using GPU: %s",
              m_renderer.GetGPUName().c_str());
    oapiWriteLog(buf);

    // Initialize mesh rendering pipeline
    if (!m_renderer.InitMeshRendering()) {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Failed to initialize mesh rendering"));
        // Continue anyway - scene rendering still works
    } else {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Mesh rendering ready"));

        // Initialize texture manager (requires mesh pipeline for descriptor allocation)
        // Provide resolver callback to convert SURFHANDLE -> VulkanTexture*
        SurfaceResolver resolver = [this](SURFHANDLE surf) -> VulkanTexture* {
            return this->GetTextureFromSurface(surf);
        };
        if (m_texManager.Init(m_renderer.GetMeshPipeline(), m_renderer.GetVulkanContext(),
                              m_renderer.GetStagingManager(), resolver)) {
            oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Texture manager ready"));
        }
    }

    oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Headless render context ready"));

    // Return hidden window handle - Orbiter needs non-NULL for render loop
    return m_hWnd;
}

void HeadlessGraphicsClient::clbkDestroyRenderWindow(bool fastclose)
{
    oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Destroying render context..."));

    m_renderer.Shutdown();
    DestroyHiddenWindow();

    GraphicsClient::clbkDestroyRenderWindow(fastclose);
}

void HeadlessGraphicsClient::clbkUpdate(bool running)
{
    static bool bFirstUpdate = true;

    // On first update, scan for existing objects (vessels, planets, etc.)
    // This is needed because clbkNewVessel is only called for vessels created
    // AFTER the graphics client starts - not for vessels loaded with the scenario
    if (bFirstUpdate && m_renderer.IsInitialized()) {
        bFirstUpdate = false;

        DWORD nobj = oapiGetObjectCount();
        char buf[256];
        sprintf_s(buf, "HeadlessGraphicsClient: First update - scanning %lu objects", nobj);
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

void HeadlessGraphicsClient::clbkRenderScene()
{
    if (!m_renderer.IsInitialized()) {
        return;
    }

    m_frameCount++;

    // Log periodically to show rendering continues
    if (m_frameCount <= 5 || m_frameCount % 100 == 0) {
        char buf[128];
        sprintf_s(buf, "HeadlessGraphicsClient: Rendering frame %d", m_frameCount);
        oapiWriteLog(buf);
    }

    // Begin frame
    m_renderer.BeginFrame();

    // Clear to space black
    m_renderer.Clear(0.0f, 0.0f, 0.05f, 1.0f);

    // Render test scene (triangle)
    m_renderer.RenderScene();

    // Render vessel meshes
    if (m_renderer.IsMeshRenderingInitialized() && !m_vesselVisuals.empty()) {
        // Get camera info
        VECTOR3 camPos, camDir;
        MATRIX3 camRot;
        oapiCameraGlobalPos(&camPos);
        oapiCameraGlobalDir(&camDir);
        oapiCameraRotationMatrix(&camRot);

        // Build view matrix from camera rotation
        // Uses MATRIX3 columns as Vulkan columns, with Z negated for handedness
        float view[16] = {
            (float)camRot.m11, (float)camRot.m21, (float)-camRot.m31, 0.0f,
            (float)camRot.m12, (float)camRot.m22, (float)-camRot.m32, 0.0f,
            (float)camRot.m13, (float)camRot.m23, (float)-camRot.m33, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        // Projection matrix (perspective) - Vulkan conventions
        // Vulkan: Y is flipped, Z range is [0, 1]
        float fov = 50.0f * 3.14159f / 180.0f;  // 50 degree FOV
        float aspect = (float)m_viewportWidth / (float)m_viewportHeight;
        float nearZ = 1.0f;
        float farZ = 1e8f;  // Very far for space
        float tanHalfFov = tanf(fov / 2.0f);

        float proj[16] = { 0 };
        proj[0] = 1.0f / (aspect * tanHalfFov);
        proj[5] = -1.0f / tanHalfFov;  // Negative for Vulkan Y-flip
        proj[10] = farZ / (nearZ - farZ);  // Vulkan depth range [0,1]
        proj[11] = -1.0f;
        proj[14] = (nearZ * farZ) / (nearZ - farZ);

        // Light direction (from sun - use fixed for now)
        float lightDir[4] = { 0.577f, 0.577f, -0.577f, 0.0f };

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
            float model[16] = {
                (float)vesselRot.m11, (float)vesselRot.m21, (float)vesselRot.m31, 0.0f,
                (float)vesselRot.m12, (float)vesselRot.m22, (float)vesselRot.m32, 0.0f,
                (float)vesselRot.m13, (float)vesselRot.m23, (float)vesselRot.m33, 0.0f,
                (float)relPos.x, (float)relPos.y, (float)relPos.z, 1.0f
            };

            // Compute MVP = Proj * View * Model (column-major matrix multiplication)
            // For column-major C = A * B: C[col*4+row] = sum_k A[k*4+row] * B[col*4+k]
            float mv[16];
            // mv = view * model
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    mv[col*4+row] = 0;
                    for (int k = 0; k < 4; k++) {
                        mv[col*4+row] += view[k*4+row] * model[col*4+k];
                    }
                }
            }

            // mvp = proj * mv
            float mvp[16];
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    mvp[col*4+row] = 0;
                    for (int k = 0; k < 4; k++) {
                        mvp[col*4+row] += proj[k*4+row] * mv[col*4+k];
                    }
                }
            }

            // Render each mesh in vessel, group by group with per-group materials and textures
            for (size_t meshIdx = 0; meshIdx < vv->meshes.size(); meshIdx++) {
                auto& mesh = vv->meshes[meshIdx];
                if (!mesh || !mesh->IsUploaded()) continue;

                // Get mesh handle for texture lookup (if available)
                MESHHANDLE hMesh = (meshIdx < vv->meshHandles.size()) ? vv->meshHandles[meshIdx] : nullptr;

                for (uint32_t g = 0; g < mesh->GetGroupCount(); g++) {
                    const MeshGroup* grp = mesh->GetGroup(g);
                    if (!grp) continue;

                    // Get material for this group
                    float matDiffuse[4] = { 0.7f, 0.7f, 0.7f, 1.0f };  // default gray
                    float matEmissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

                    const MeshMaterial* mat = mesh->GetMaterial(grp->materialIdx);
                    if (mat) {
                        memcpy(matDiffuse, mat->diffuse, sizeof(float) * 4);
                        memcpy(matEmissive, mat->emissive, sizeof(float) * 4);
                    }

                    // Get texture for this group (textureIdx is 1-based in Orbiter API)
                    VkDescriptorSet texDesc = VK_NULL_HANDLE;
                    if (hMesh && grp->textureIdx > 0) {
                        SURFHANDLE hTex = oapiGetTextureHandle(hMesh, grp->textureIdx);
                        if (hTex) {
                            texDesc = m_texManager.GetTextureDescriptor(hTex);
                        }
                    }

                    // Use textured render if we have a texture, otherwise use material-only
                    if (texDesc != VK_NULL_HANDLE) {
                        m_renderer.RenderMeshGroupTextured(mesh.get(), g, mvp, model, lightDir, texDesc, matDiffuse, matEmissive);
                    } else {
                        m_renderer.RenderMeshGroup(mesh.get(), g, mvp, model, lightDir, matDiffuse, matEmissive);
                    }
                }
            }
        }
    }

    // End frame
    m_renderer.EndFrame();

    m_frameInProgress = true;
}

bool HeadlessGraphicsClient::clbkDisplayFrame()
{
    if (!m_renderer.IsInitialized() || !m_frameInProgress) {
        return true;
    }

    m_frameInProgress = false;

    // Submit the frame for rendering
    m_renderer.Submit();

    // Auto-capture frames if output path is set
    if (!m_captureOutputPath.empty()) {
        std::ostringstream filename;
        filename << m_captureOutputPath << "/frame_" << m_frameCount << ".ppm";
        SaveFrameToPPM(filename.str().c_str());
    }

    return true;
}

void HeadlessGraphicsClient::clbkGetViewportSize(DWORD *width, DWORD *height) const
{
    *width = m_viewportWidth;
    *height = m_viewportHeight;
}

bool HeadlessGraphicsClient::clbkGetRenderParam(DWORD param, DWORD *value) const
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
            *value = 8;
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
// Vessel management - create/delete visuals
// ======================================================================

void HeadlessGraphicsClient::clbkNewVessel(OBJHANDLE hVessel)
{
    if (!m_renderer.IsInitialized()) {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: clbkNewVessel - renderer not ready"));
        return;
    }

    // Check if we already have a visual for this vessel
    if (m_objToVisual.find(hVessel) != m_objToVisual.end()) {
        return;
    }

    // Get vessel interface
    VESSEL* vessel = oapiGetVesselInterface(hVessel);
    if (!vessel) {
        oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: clbkNewVessel - no vessel interface"));
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
    sprintf_s(buf, "HeadlessGraphicsClient: Registered visual for vessel '%s' (vis=%p)", vesselName, vis);
    oapiWriteLog(buf);

    // Load existing meshes (like D3D9Client does in VVessel constructor)
    if (m_renderer.IsMeshRenderingInitialized()) {
        DWORD nmesh = vessel->GetMeshCount();
        if (nmesh > 0) {
            vvPtr->meshes.resize(nmesh);
            vvPtr->meshHandles.resize(nmesh, nullptr);  // Store mesh handles for texture lookup

            for (DWORD idx = 0; idx < nmesh; idx++) {
                MESHHANDLE hMesh = vessel->GetMeshTemplate(idx);
                if (!hMesh) {
                    // Mesh might be loaded on-demand, skip for now
                    continue;
                }

                // Store mesh handle for texture lookup (texture indices are 1-based in oapiGetTextureHandle)
                vvPtr->meshHandles[idx] = hMesh;

                DWORD nGroups = oapiMeshGroupCount(hMesh);
                if (nGroups == 0) {
                    continue;
                }

                auto vulkanMesh = std::make_unique<VulkanMesh>();
                if (!vulkanMesh->Init(m_renderer.GetVulkanContext(), m_renderer.GetStagingManager())) {
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

            sprintf_s(buf, "HeadlessGraphicsClient: Loaded %lu meshes for vessel '%s'", nmesh, vesselName);
            oapiWriteLog(buf);
        }
    }
}

void HeadlessGraphicsClient::clbkDeleteVessel(OBJHANDLE hVessel)
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
    sprintf_s(buf, "HeadlessGraphicsClient: Deleted visual for vessel %p", hVessel);
    oapiWriteLog(buf);
}

// ======================================================================
// Visual events - mesh loading
// ======================================================================

int HeadlessGraphicsClient::clbkVisEvent(OBJHANDLE hObj, VISHANDLE vis, DWORD msg, DWORD_PTR context)
{
    if (!m_renderer.IsInitialized()) {
        return 0;
    }

    switch (msg) {
        case EVENT_VESSEL_INSMESH: {
            // Insert mesh at index 'context'
            UINT meshIdx = static_cast<UINT>(context);

            // Get the vessel interface
            VESSEL* vessel = oapiGetVesselInterface(hObj);
            if (!vessel) {
                oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: clbkVisEvent - no vessel interface"));
                return 0;
            }

            // Get mesh template
            MESHHANDLE hMesh = vessel->GetMeshTemplate(meshIdx);
            if (!hMesh) {
                // Mesh might be loaded on-demand, just log and skip
                char buf[256];
                sprintf_s(buf, "HeadlessGraphicsClient: Mesh %u not preloaded, skipping", meshIdx);
                oapiWriteLog(buf);
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

            // Ensure meshes and meshHandles vectors are large enough
            if (vv->meshes.size() <= meshIdx) {
                vv->meshes.resize(meshIdx + 1);
                vv->meshHandles.resize(meshIdx + 1, nullptr);
            }

            // Store mesh handle for texture lookup
            vv->meshHandles[meshIdx] = hMesh;

            // Create VulkanMesh from mesh template
            DWORD nGroups = oapiMeshGroupCount(hMesh);
            if (nGroups == 0) {
                return 0;
            }

            auto vulkanMesh = std::make_unique<VulkanMesh>();
            if (!vulkanMesh->Init(m_renderer.GetVulkanContext(), m_renderer.GetStagingManager())) {
                oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Failed to init VulkanMesh"));
                return 0;
            }

            // Convert each mesh group
            for (DWORD g = 0; g < nGroups; g++) {
                MESHGROUPEX* grp = oapiMeshGroupEx(hMesh, g);
                if (!grp || !grp->Vtx || grp->nVtx == 0) {
                    continue;
                }

                // NTVERTEX and MeshVertex have identical layout (32 bytes)
                // We can reinterpret_cast safely due to matching layout
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

            // Upload to GPU
            if (!vulkanMesh->Upload()) {
                oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: Failed to upload VulkanMesh"));
                return 0;
            }

            vv->meshes[meshIdx] = std::move(vulkanMesh);

            char buf[256];
            sprintf_s(buf, "HeadlessGraphicsClient: Loaded mesh %u with %lu groups for vessel %p",
                      meshIdx, nGroups, hObj);
            oapiWriteLog(buf);
            break;
        }

        case EVENT_VESSEL_DELMESH: {
            // Delete mesh at index 'context', or all if context == -1
            auto it = m_vesselVisuals.find(vis);
            if (it == m_vesselVisuals.end()) {
                return 0;
            }
            VesselVisual* vv = it->second.get();

            if (context == static_cast<DWORD_PTR>(-1)) {
                // Delete all meshes
                vv->meshes.clear();
            } else {
                UINT meshIdx = static_cast<UINT>(context);
                if (meshIdx < vv->meshes.size()) {
                    vv->meshes[meshIdx].reset();
                }
            }
            break;
        }

        default:
            // Other events not yet handled
            break;
    }

    return 0;
}

// ======================================================================
// ImGui interface - disabled for headless mode
// ======================================================================

void HeadlessGraphicsClient::clbkImGuiInit()
{
    // ImGui is completely disabled in headless mode
    oapiWriteLog(const_cast<char*>("HeadlessGraphicsClient: ImGui disabled (headless mode)"));
}

void HeadlessGraphicsClient::clbkImGuiShutdown()
{
    // Nothing to shut down
}

void HeadlessGraphicsClient::clbkImGuiNewFrame()
{
    // No-op
}

void HeadlessGraphicsClient::clbkImGuiRenderDrawData()
{
    // No-op
}

// ======================================================================
// Test API
// ======================================================================

std::vector<uint8_t> HeadlessGraphicsClient::CaptureFrame()
{
    if (!m_renderer.IsInitialized()) {
        return {};
    }

    return m_renderer.ReadPixels();
}

void HeadlessGraphicsClient::SaveFrameToPPM(const char* path)
{
    auto pixels = CaptureFrame();
    if (pixels.empty()) {
        char buf[256];
        sprintf_s(buf, "HeadlessGraphicsClient: Failed to capture frame for %s", path);
        oapiWriteLog(buf);
        return;
    }

    // Write as PPM (simple format for debugging)
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        char buf[256];
        sprintf_s(buf, "HeadlessGraphicsClient: Failed to open %s for writing", path);
        oapiWriteLog(buf);
        return;
    }

    // PPM header
    file << "P6\n" << m_viewportWidth << " " << m_viewportHeight << "\n255\n";

    // Convert BGRA to RGB
    for (uint32_t y = 0; y < m_viewportHeight; y++) {
        for (uint32_t x = 0; x < m_viewportWidth; x++) {
            size_t idx = (y * m_viewportWidth + x) * 4;
            uint8_t b = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t r = pixels[idx + 2];
            file.put(r);
            file.put(g);
            file.put(b);
        }
    }

    char buf[256];
    sprintf_s(buf, "HeadlessGraphicsClient: Saved frame to %s", path);
    oapiWriteLog(buf);
}

// ======================================================================
// Texture/Surface loading
// ======================================================================

SURFHANDLE HeadlessGraphicsClient::clbkLoadTexture(const char* fname, DWORD flags)
{
    if (!fname || !m_renderer.IsInitialized()) {
        return nullptr;
    }

    // Build full path to texture file
    // Orbiter textures are typically in Textures/ folder relative to exe
    std::string fullPath;
    if (fname[0] == '/' || fname[0] == '\\' || (fname[1] == ':')) {
        // Absolute path
        fullPath = fname;
    } else {
        // Relative path - prepend Textures folder
        char rootDir[MAX_PATH];
        oapiGetObjectName(nullptr, rootDir, MAX_PATH);  // This doesn't work - use alternative
        // Use current directory for now (Orbiter runs from its install dir)
        fullPath = std::string("Textures/") + fname;
    }

    // Check cache first (bit 3 of flags = store in repository)
    if (flags & 0x8) {
        auto cacheIt = m_textureCache.find(fullPath);
        if (cacheIt != m_textureCache.end()) {
            // Found in cache - increment ref count and return
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
    VulkanContext* ctx = m_renderer.GetVulkanContext();
    StagingManager* staging = m_renderer.GetStagingManager();

    if (!surfHandle->texture->CreateFromFile(ctx, staging, fullPath.c_str())) {
        // Try without Textures/ prefix (might already be full path)
        if (!surfHandle->texture->CreateFromFile(ctx, staging, fname)) {
            char buf[512];
            sprintf_s(buf, "HeadlessGraphicsClient: Failed to load texture: %s", fname);
            oapiWriteLog(buf);
            return nullptr;
        }
    }

    surfHandle->width = surfHandle->texture->GetWidth();
    surfHandle->height = surfHandle->texture->GetHeight();

    // Create SURFHANDLE from pointer
    SURFHANDLE hSurf = reinterpret_cast<SURFHANDLE>(surfHandle.get());

    // Store in cache if flag set
    if (flags & 0x8) {
        m_textureCache[fullPath] = hSurf;
    }

    // Store in surface map
    m_surfaces[hSurf] = std::move(surfHandle);

    char buf[512];
    sprintf_s(buf, "HeadlessGraphicsClient: Loaded texture: %s (%ux%u)",
              fname, m_surfaces[hSurf]->width, m_surfaces[hSurf]->height);
    oapiWriteLog(buf);

    return hSurf;
}

void HeadlessGraphicsClient::clbkReleaseTexture(SURFHANDLE hTex)
{
    clbkReleaseSurface(hTex);
}

bool HeadlessGraphicsClient::clbkReleaseSurface(SURFHANDLE surf)
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
        // Remove from texture cache if present
        for (auto cacheIt = m_textureCache.begin(); cacheIt != m_textureCache.end(); ) {
            if (cacheIt->second == surf) {
                cacheIt = m_textureCache.erase(cacheIt);
            } else {
                ++cacheIt;
            }
        }

        // Remove from TextureManager cache
        m_texManager.InvalidateTexture(surf);

        // Destroy the surface
        m_surfaces.erase(it);
    }

    return true;
}

bool HeadlessGraphicsClient::clbkGetSurfaceSize(SURFHANDLE surf, DWORD* w, DWORD* h)
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

void HeadlessGraphicsClient::clbkIncrSurfaceRef(SURFHANDLE surf)
{
    if (!surf) {
        return;
    }

    auto it = m_surfaces.find(surf);
    if (it != m_surfaces.end()) {
        it->second->refCount++;
    }
}

VulkanTexture* HeadlessGraphicsClient::GetTextureFromSurface(SURFHANDLE surf) const
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
