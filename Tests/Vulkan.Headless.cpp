// ==============================================================
// Vulkan.Headless.cpp
// Consolidated Vulkan rendering tests using HeadlessRenderer
// Part of the ORBITER VISUALISATION PROJECT (OVP)
//
// Tests rendering functionality with pixel validation and
// optional RenderDoc capture for debugging.
// ==============================================================

#include "../OVP/VulkanClient/HeadlessRenderer.h"
#include "../OVP/VulkanClient/Core/StagingManager.h"
#include "../OVP/VulkanClient/Core/VulkanContext.h"
#include "../OVP/VulkanClient/Core/VulkanTexture.h"
#include "../OVP/VulkanClient/Core/VulkanDescriptors.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

#define CATCH_CONFIG_MAIN
#include "catch2/catch_all.hpp"

// Helper: Check if a pixel color is approximately equal (within tolerance)
static bool ColorApproxEqual(uint8_t actual, uint8_t expected, uint8_t tolerance = 2)
{
    return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= tolerance;
}

// Helper: Convert float color [0,1] to uint8 [0,255]
static uint8_t FloatToUint8(float f)
{
    return static_cast<uint8_t>(std::clamp(f * 255.0f, 0.0f, 255.0f));
}

// BGRA pixel helper - matches VK_FORMAT_B8G8R8A8_UNORM layout
struct BGRAPixel {
    uint8_t b, g, r, a;

    static BGRAPixel FromBuffer(const std::vector<uint8_t>& pixels, size_t x, size_t y, size_t width) {
        size_t idx = (y * width + x) * 4;
        return { pixels[idx], pixels[idx + 1], pixels[idx + 2], pixels[idx + 3] };
    }

    bool ApproxEquals(uint8_t expectedR, uint8_t expectedG, uint8_t expectedB, uint8_t expectedA, uint8_t tolerance = 2) const {
        return ColorApproxEqual(r, expectedR, tolerance) &&
               ColorApproxEqual(g, expectedG, tolerance) &&
               ColorApproxEqual(b, expectedB, tolerance) &&
               ColorApproxEqual(a, expectedA, tolerance);
    }
};

TEST_CASE("HeadlessRenderer initialization", "[Vulkan][Headless]")
{
    HeadlessRenderer renderer;

    SECTION("Basic initialization") {
        REQUIRE(renderer.Init(256, 256));
        REQUIRE(renderer.IsInitialized());
        REQUIRE(renderer.GetWidth() == 256);
        REQUIRE(renderer.GetHeight() == 256);

        INFO("GPU: " << renderer.GetGPUName());
        REQUIRE(!renderer.GetGPUName().empty());

        renderer.Shutdown();
        REQUIRE(!renderer.IsInitialized());
    }

    SECTION("Different resolutions") {
        REQUIRE(renderer.Init(128, 128));
        renderer.Shutdown();

        REQUIRE(renderer.Init(512, 256));
        REQUIRE(renderer.GetWidth() == 512);
        REQUIRE(renderer.GetHeight() == 256);
        renderer.Shutdown();

        REQUIRE(renderer.Init(1920, 1080));
        REQUIRE(renderer.GetWidth() == 1920);
        REQUIRE(renderer.GetHeight() == 1080);
        renderer.Shutdown();
    }
}

TEST_CASE("HeadlessRenderer clear color", "[Vulkan][Headless]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(64, 64));

    SECTION("Clear to primary color") {
        // Test red - validates full render cycle with pixel readback
        renderer.BeginFrame();
        renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        REQUIRE(pixels.size() == 64 * 64 * 4);

        // Check center pixel (R=255, G=0, B=0, A=255) - using BGRA format
        auto center = BGRAPixel::FromBuffer(pixels, 32, 32, 64);
        INFO("Center pixel: R=" << (int)center.r << " G=" << (int)center.g
             << " B=" << (int)center.b << " A=" << (int)center.a);

        REQUIRE(center.ApproxEquals(255, 0, 0, 255));
    }

    SECTION("Clear to custom color") {
        // Orbiter-style space blue: RGB(51, 102, 204) = (0.2, 0.4, 0.8)
        float r = 0.2f, g = 0.4f, b = 0.8f, a = 1.0f;

        renderer.BeginFrame();
        renderer.Clear(r, g, b, a);
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        auto center = BGRAPixel::FromBuffer(pixels, 32, 32, 64);

        uint8_t expectedR = FloatToUint8(r);
        uint8_t expectedG = FloatToUint8(g);
        uint8_t expectedB = FloatToUint8(b);
        uint8_t expectedA = FloatToUint8(a);

        INFO("Expected: R=" << (int)expectedR << " G=" << (int)expectedG
             << " B=" << (int)expectedB << " A=" << (int)expectedA);
        INFO("Actual: R=" << (int)center.r << " G=" << (int)center.g
             << " B=" << (int)center.b << " A=" << (int)center.a);

        REQUIRE(center.ApproxEquals(expectedR, expectedG, expectedB, expectedA));
    }

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer pixel uniformity", "[Vulkan][Headless]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(32, 32));

    renderer.BeginFrame();
    renderer.Clear(0.5f, 0.5f, 0.5f, 1.0f);
    renderer.EndFrame();
    renderer.Submit();

    auto pixels = renderer.ReadPixels();
    REQUIRE(pixels.size() == 32 * 32 * 4);

    // All pixels should be the same gray color
    uint8_t expectedGray = FloatToUint8(0.5f);

    for (size_t i = 0; i < 32 * 32; i++) {
        size_t idx = i * 4;
        if (!ColorApproxEqual(pixels[idx], expectedGray) ||
            !ColorApproxEqual(pixels[idx + 1], expectedGray) ||
            !ColorApproxEqual(pixels[idx + 2], expectedGray)) {
            INFO("Pixel " << i << " at (" << (i % 32) << ", " << (i / 32) << "): R="
                 << (int)pixels[idx] << " G=" << (int)pixels[idx + 1]
                 << " B=" << (int)pixels[idx + 2]);
            REQUIRE(false);
        }
    }

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer multiple frames", "[Vulkan][Headless]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(32, 32));

    // Frame 1: Red
    renderer.BeginFrame();
    renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
    renderer.EndFrame();
    renderer.Submit();

    auto pixels1 = renderer.ReadPixels();
    auto center1 = BGRAPixel::FromBuffer(pixels1, 16, 16, 32);
    REQUIRE(center1.ApproxEquals(255, 0, 0, 255));

    // Frame 2: Green
    renderer.BeginFrame();
    renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
    renderer.EndFrame();
    renderer.Submit();

    auto pixels2 = renderer.ReadPixels();
    auto center2 = BGRAPixel::FromBuffer(pixels2, 16, 16, 32);
    REQUIRE(center2.ApproxEquals(0, 255, 0, 255));

    // Frame 3: Blue
    renderer.BeginFrame();
    renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
    renderer.EndFrame();
    renderer.Submit();

    auto pixels3 = renderer.ReadPixels();
    auto center3 = BGRAPixel::FromBuffer(pixels3, 16, 16, 32);
    REQUIRE(center3.ApproxEquals(0, 0, 255, 255));

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer RenderDoc integration", "[Vulkan][Headless][RenderDoc]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));

    if (renderer.IsRenderDocAvailable()) {
        INFO("RenderDoc is available");

        uint32_t initialCaptures = renderer.GetCaptureCount();

        // Capture a frame
        renderer.StartCapture();
        renderer.BeginFrame();
        renderer.Clear(0.2f, 0.4f, 0.8f, 1.0f);
        renderer.EndFrame();
        renderer.Submit();
        renderer.EndCapture();

        uint32_t finalCaptures = renderer.GetCaptureCount();
        INFO("Captures: initial=" << initialCaptures << " final=" << finalCaptures);

        // Capture count should have increased
        REQUIRE(finalCaptures > initialCaptures);

        // Verify rendering still works after capture
        auto pixels = renderer.ReadPixels();
        REQUIRE(!pixels.empty());
    } else {
        INFO("RenderDoc not available - skipping capture test");
        WARN("Install RenderDoc and run with renderdoccmd for capture support");
    }

    renderer.Shutdown();
}

// ======================================================================
// ImGui Integration Tests
// ======================================================================

TEST_CASE("HeadlessRenderer ImGui initialization", "[Vulkan][Headless][ImGui]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));

    SECTION("Basic ImGui initialization") {
        REQUIRE(!renderer.IsImGuiInitialized());

        REQUIRE(renderer.InitImGui());
        REQUIRE(renderer.IsImGuiInitialized());

        // Verify ImGui context exists
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        REQUIRE(ctx != nullptr);

        renderer.ShutdownImGui();
        REQUIRE(!renderer.IsImGuiInitialized());
    }

    SECTION("Double initialization is safe") {
        REQUIRE(renderer.InitImGui());
        REQUIRE(renderer.InitImGui());  // Should return true without error
        REQUIRE(renderer.IsImGuiInitialized());
    }

    SECTION("Shutdown without init is safe") {
        renderer.ShutdownImGui();  // Should not crash
        REQUIRE(!renderer.IsImGuiInitialized());
    }

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer ImGui frame cycle", "[Vulkan][Headless][ImGui]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));
    REQUIRE(renderer.InitImGui());

    SECTION("Empty frame") {
        renderer.BeginFrame();
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);

        renderer.ImGuiNewFrame();
        // No widgets - just start and end frame
        renderer.ImGuiRender();

        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        REQUIRE(!pixels.empty());

        // Should be cleared to black
        size_t centerIdx = (128 * 256 + 128) * 4;
        REQUIRE(ColorApproxEqual(pixels[centerIdx], 0));      // R
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 1], 0));  // G
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 2], 0));  // B
    }

    SECTION("Multiple frames") {
        for (int i = 0; i < 3; i++) {
            renderer.BeginFrame();
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);

            renderer.ImGuiNewFrame();
            renderer.ImGuiRender();

            renderer.EndFrame();
            renderer.Submit();

            auto pixels = renderer.ReadPixels();
            REQUIRE(!pixels.empty());
        }
    }

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer ImGui widget rendering", "[Vulkan][Headless][ImGui]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));
    REQUIRE(renderer.InitImGui());

    // Render a frame with a window that covers most of the screen
    renderer.BeginFrame();
    renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);

    renderer.ImGuiNewFrame();

    // Create a large window with some content
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(236, 236));
    ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar);
    ImGui::Text("Hello, World!");
    ImGui::Button("Test Button");
    ImGui::End();

    renderer.ImGuiRender();
    renderer.EndFrame();
    renderer.Submit();

    auto pixels = renderer.ReadPixels();
    REQUIRE(!pixels.empty());

    // The window should have rendered some non-black pixels
    // Check center of the window area
    auto center = BGRAPixel::FromBuffer(pixels, 128, 128, 256);

    INFO("Center pixel: R=" << (int)center.r << " G=" << (int)center.g << " B=" << (int)center.b);

    // ImGui dark theme window background is not pure black
    // So at least one channel should be non-zero
    bool hasContent = (center.r > 5 || center.g > 5 || center.b > 5);
    REQUIRE(hasContent);

    renderer.Shutdown();
}

// ======================================================================
// VMA (Vulkan Memory Allocator) & Staging Tests
// ======================================================================

TEST_CASE("Device-local buffer upload via staging", "[Vulkan][Headless][Staging]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(64, 64));

    SECTION("StagingManager initializes") {
        // Verify staging manager is available
        StagingManager* staging = renderer.GetStagingManager();
        REQUIRE(staging != nullptr);
        REQUIRE(staging->IsInitialized());
    }

    SECTION("SceneRenderer uses device-local memory") {
        // Verify that geometry buffers are in optimal GPU memory
        INFO("Device-local buffers provide optimal GPU performance for static geometry");
        REQUIRE(renderer.UsesDeviceLocalBuffers());
    }

    SECTION("Rendering still works with device-local buffers") {
        // Render a frame and verify the triangle appears
        renderer.BeginFrame();
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.RenderScene();
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        REQUIRE(pixels.size() == 64 * 64 * 4);

        // Check that we rendered something (not all black)
        bool hasNonBlackPixel = false;
        for (size_t i = 0; i < 64 * 64; i++) {
            if (pixels[i * 4] > 0 || pixels[i * 4 + 1] > 0 || pixels[i * 4 + 2] > 0) {
                hasNonBlackPixel = true;
                break;
            }
        }
        REQUIRE(hasNonBlackPixel);
    }
}

// ======================================================================
// 3D Rendering Tests (using SceneRenderer)
// ======================================================================

TEST_CASE("HeadlessRenderer 3D scene rendering", "[Vulkan][Headless][3D]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));

    SECTION("Scene renders visible geometry") {
        // First, render without scene to get baseline
        renderer.BeginFrame();
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);  // Black
        renderer.EndFrame();
        renderer.Submit();

        auto baselinePixels = renderer.ReadPixels();
        size_t centerIdx = (128 * 256 + 128) * 4;

        // Baseline should be black
        REQUIRE(ColorApproxEqual(baselinePixels[centerIdx], 0));
        REQUIRE(ColorApproxEqual(baselinePixels[centerIdx + 1], 0));
        REQUIRE(ColorApproxEqual(baselinePixels[centerIdx + 2], 0));

        // Now render with scene
        renderer.BeginFrame();
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);  // Black
        renderer.RenderScene();  // Renders colored triangle
        renderer.EndFrame();
        renderer.Submit();

        auto scenePixels = renderer.ReadPixels();

        // The scene should have some visible colored geometry
        // Check that at least some pixels are not black (triangle is visible)
        int nonBlackPixels = 0;
        for (size_t i = 0; i < 256 * 256; i++) {
            size_t idx = i * 4;
            if (scenePixels[idx] > 10 || scenePixels[idx + 1] > 10 || scenePixels[idx + 2] > 10) {
                nonBlackPixels++;
            }
        }

        INFO("Non-black pixels: " << nonBlackPixels);
        // Triangle should cover some area - expect at least a few hundred pixels
        REQUIRE(nonBlackPixels > 100);
    }

    SECTION("Scene renders colored triangle") {
        renderer.BeginFrame();
        renderer.Clear(0.2f, 0.2f, 0.2f, 1.0f);  // Dark gray background
        renderer.RenderScene();
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();

        // Count pixels that match triangle colors (red, green, blue vertex colors)
        int redPixels = 0;
        int greenPixels = 0;
        int bluePixels = 0;
        int grayPixels = 0;

        uint8_t grayVal = FloatToUint8(0.2f);

        for (size_t y = 0; y < 256; y++) {
            for (size_t x = 0; x < 256; x++) {
                auto px = BGRAPixel::FromBuffer(pixels, x, y, 256);

                // Check for background gray
                if (ColorApproxEqual(px.r, grayVal, 5) &&
                    ColorApproxEqual(px.g, grayVal, 5) &&
                    ColorApproxEqual(px.b, grayVal, 5)) {
                    grayPixels++;
                }
                // Check for strong red component
                else if (px.r > 128 && px.r > px.g && px.r > px.b) {
                    redPixels++;
                }
                // Check for strong green component
                else if (px.g > 128 && px.g > px.r && px.g > px.b) {
                    greenPixels++;
                }
                // Check for strong blue component
                else if (px.b > 128 && px.b > px.r && px.b > px.g) {
                    bluePixels++;
                }
            }
        }

        INFO("Red pixels: " << redPixels);
        INFO("Green pixels: " << greenPixels);
        INFO("Blue pixels: " << bluePixels);
        INFO("Gray (background) pixels: " << grayPixels);

        // The triangle has RGB vertex colors, so we should see some of each
        // The colors blend smoothly across the triangle
        int coloredPixels = redPixels + greenPixels + bluePixels;
        REQUIRE(coloredPixels > 50);  // Should have some colored geometry
        REQUIRE(grayPixels > 0);      // Should have some background visible
    }

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer 3D with ImGui", "[Vulkan][Headless][3D][ImGui]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));
    REQUIRE(renderer.InitImGui());

    // Render 3D scene with ImGui overlay
    renderer.BeginFrame();
    renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    renderer.RenderScene();

    renderer.ImGuiNewFrame();
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(100, 50));
    ImGui::Begin("Overlay", nullptr, ImGuiWindowFlags_NoTitleBar);
    ImGui::Text("3D Scene");
    ImGui::End();
    renderer.ImGuiRender();

    renderer.EndFrame();
    renderer.Submit();

    auto pixels = renderer.ReadPixels();
    REQUIRE(!pixels.empty());

    // Count non-black pixels (scene + UI should produce visible content)
    int visiblePixels = 0;
    for (size_t i = 0; i < 256 * 256; i++) {
        size_t idx = i * 4;
        if (pixels[idx] > 5 || pixels[idx + 1] > 5 || pixels[idx + 2] > 5) {
            visiblePixels++;
        }
    }

    INFO("Visible pixels: " << visiblePixels);
    REQUIRE(visiblePixels > 500);  // Scene + UI should produce significant visible content

    renderer.Shutdown();
}

// ======================================================================
// Texture Tests (Phase 4)
// ======================================================================

TEST_CASE("VulkanTexture creation from memory", "[Vulkan][Headless][Texture]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(64, 64));

    StagingManager* staging = renderer.GetStagingManager();
    REQUIRE(staging != nullptr);
    REQUIRE(staging->IsInitialized());

    VulkanContext* ctx = staging->GetContext();
    REQUIRE(ctx != nullptr);

    SECTION("2x2 RGBA test pattern") {
        // Create a simple 2x2 checkerboard texture
        // R, G, B, A for each pixel
        uint8_t pixels[2 * 2 * 4] = {
            255, 0, 0, 255,    // Red (top-left)
            0, 255, 0, 255,    // Green (top-right)
            0, 0, 255, 255,    // Blue (bottom-left)
            255, 255, 0, 255   // Yellow (bottom-right)
        };

        VulkanTexture texture;
        REQUIRE(texture.CreateFromMemory(ctx, staging, pixels, 2, 2));
        REQUIRE(texture.IsValid());
        REQUIRE(texture.GetWidth() == 2);
        REQUIRE(texture.GetHeight() == 2);

        // Verify we got valid Vulkan handles
        REQUIRE(texture.GetImage() != VK_NULL_HANDLE);
        REQUIRE(texture.GetImageView() != VK_NULL_HANDLE);
        REQUIRE(texture.GetSampler() != VK_NULL_HANDLE);

        // Verify descriptor info
        VkDescriptorImageInfo descInfo = texture.GetDescriptorInfo();
        REQUIRE(descInfo.imageView == texture.GetImageView());
        REQUIRE(descInfo.sampler == texture.GetSampler());
        REQUIRE(descInfo.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        texture.Destroy();
        REQUIRE(!texture.IsValid());
    }

    SECTION("Larger texture") {
        // 32x32 white texture
        std::vector<uint8_t> pixels(32 * 32 * 4, 255);

        VulkanTexture texture;
        REQUIRE(texture.CreateFromMemory(ctx, staging, pixels.data(), 32, 32));
        REQUIRE(texture.IsValid());
        REQUIRE(texture.GetWidth() == 32);
        REQUIRE(texture.GetHeight() == 32);

        texture.Destroy();
    }

    SECTION("Invalid parameters") {
        VulkanTexture texture;

        // Null data should fail
        REQUIRE_FALSE(texture.CreateFromMemory(ctx, staging, nullptr, 2, 2));

        // Zero dimensions should fail
        uint8_t pixels[4] = { 255, 255, 255, 255 };
        REQUIRE_FALSE(texture.CreateFromMemory(ctx, staging, pixels, 0, 2));
        REQUIRE_FALSE(texture.CreateFromMemory(ctx, staging, pixels, 2, 0));
    }

    renderer.Shutdown();
}

TEST_CASE("VulkanDescriptorPool creation", "[Vulkan][Headless][Texture]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(64, 64));

    StagingManager* staging = renderer.GetStagingManager();
    VulkanContext* ctx = staging->GetContext();
    REQUIRE(ctx != nullptr);

    SECTION("Basic pool creation") {
        VulkanDescriptorPool pool;
        REQUIRE(pool.Init(ctx, 100));
        REQUIRE(pool.IsInitialized());
        REQUIRE(pool.GetPool() != VK_NULL_HANDLE);

        pool.Shutdown();
        REQUIRE(!pool.IsInitialized());
    }

    SECTION("Double initialization returns false") {
        VulkanDescriptorPool pool;
        REQUIRE(pool.Init(ctx, 100));
        REQUIRE_FALSE(pool.Init(ctx, 100));  // Already initialized

        pool.Shutdown();
    }

    renderer.Shutdown();
}

TEST_CASE("VulkanDescriptorSetLayoutBuilder", "[Vulkan][Headless][Texture]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(64, 64));

    StagingManager* staging = renderer.GetStagingManager();
    VulkanContext* ctx = staging->GetContext();
    REQUIRE(ctx != nullptr);

    SECTION("Create sampler layout") {
        VulkanDescriptorSetLayoutBuilder builder;
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           VK_SHADER_STAGE_FRAGMENT_BIT);

        VkDescriptorSetLayout layout = builder.Build(ctx);
        REQUIRE(layout != VK_NULL_HANDLE);

        // Cleanup
        vkDestroyDescriptorSetLayout(ctx->GetDevice(), layout, nullptr);
    }

    SECTION("Create uniform buffer layout") {
        VulkanDescriptorSetLayoutBuilder builder;
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT);

        VkDescriptorSetLayout layout = builder.Build(ctx);
        REQUIRE(layout != VK_NULL_HANDLE);

        vkDestroyDescriptorSetLayout(ctx->GetDevice(), layout, nullptr);
    }

    SECTION("Create multi-binding layout") {
        VulkanDescriptorSetLayoutBuilder builder;
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           VK_SHADER_STAGE_VERTEX_BIT)
               .AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           VK_SHADER_STAGE_FRAGMENT_BIT)
               .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           VK_SHADER_STAGE_FRAGMENT_BIT);

        VkDescriptorSetLayout layout = builder.Build(ctx);
        REQUIRE(layout != VK_NULL_HANDLE);

        vkDestroyDescriptorSetLayout(ctx->GetDevice(), layout, nullptr);
    }

    renderer.Shutdown();
}

TEST_CASE("Descriptor set allocation", "[Vulkan][Headless][Texture]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(64, 64));

    StagingManager* staging = renderer.GetStagingManager();
    VulkanContext* ctx = staging->GetContext();
    REQUIRE(ctx != nullptr);

    // Create descriptor pool
    VulkanDescriptorPool pool;
    REQUIRE(pool.Init(ctx, 100));

    // Create layout
    VulkanDescriptorSetLayoutBuilder builder;
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                       VK_SHADER_STAGE_FRAGMENT_BIT);
    VkDescriptorSetLayout layout = builder.Build(ctx);
    REQUIRE(layout != VK_NULL_HANDLE);

    SECTION("Allocate single set") {
        VkDescriptorSet set = pool.AllocateSet(layout);
        REQUIRE(set != VK_NULL_HANDLE);

        pool.FreeSet(set);
    }

    SECTION("Allocate multiple sets") {
        std::vector<VkDescriptorSet> sets;
        for (int i = 0; i < 10; i++) {
            VkDescriptorSet set = pool.AllocateSet(layout);
            REQUIRE(set != VK_NULL_HANDLE);
            sets.push_back(set);
        }

        // Free all sets
        for (auto set : sets) {
            pool.FreeSet(set);
        }
    }

    SECTION("Pool reset") {
        // Allocate several sets
        for (int i = 0; i < 5; i++) {
            pool.AllocateSet(layout);
        }

        // Reset should free all
        pool.Reset();

        // Should be able to allocate again after reset
        VkDescriptorSet set = pool.AllocateSet(layout);
        REQUIRE(set != VK_NULL_HANDLE);
    }

    // Cleanup
    vkDestroyDescriptorSetLayout(ctx->GetDevice(), layout, nullptr);
    pool.Shutdown();
    renderer.Shutdown();
}

TEST_CASE("VulkanDescriptorWriter with texture", "[Vulkan][Headless][Texture]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(64, 64));

    StagingManager* staging = renderer.GetStagingManager();
    VulkanContext* ctx = staging->GetContext();
    REQUIRE(ctx != nullptr);

    // Create texture
    uint8_t pixels[2 * 2 * 4] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 0, 255
    };
    VulkanTexture texture;
    REQUIRE(texture.CreateFromMemory(ctx, staging, pixels, 2, 2));

    // Create descriptor pool and layout
    VulkanDescriptorPool pool;
    REQUIRE(pool.Init(ctx, 100));

    VulkanDescriptorSetLayoutBuilder builder;
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                       VK_SHADER_STAGE_FRAGMENT_BIT);
    VkDescriptorSetLayout layout = builder.Build(ctx);
    REQUIRE(layout != VK_NULL_HANDLE);

    // Allocate descriptor set
    VkDescriptorSet set = pool.AllocateSet(layout);
    REQUIRE(set != VK_NULL_HANDLE);

    // Write texture to descriptor
    VulkanDescriptorWriter writer;
    writer.WriteCombinedImageSampler(0, texture.GetSampler(),
                                      texture.GetImageView());
    writer.Update(ctx->GetDevice(), set);

    INFO("Successfully wrote texture descriptor");

    // Cleanup
    vkDestroyDescriptorSetLayout(ctx->GetDevice(), layout, nullptr);
    pool.Shutdown();
    texture.Destroy();
    renderer.Shutdown();
}
