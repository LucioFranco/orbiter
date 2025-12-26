// ==============================================================
// Vulkan.Headless.cpp
// Consolidated Vulkan rendering tests using HeadlessRenderer
// Part of the ORBITER VISUALISATION PROJECT (OVP)
//
// Tests rendering functionality with pixel validation and
// optional RenderDoc capture for debugging.
// ==============================================================

#include "../OVP/VulkanClient/HeadlessRenderer.h"
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

    SECTION("Clear to red") {
        renderer.BeginFrame();
        renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        REQUIRE(pixels.size() == 64 * 64 * 4);

        // Check center pixel (R=255, G=0, B=0, A=255)
        size_t centerIdx = (32 * 64 + 32) * 4;
        INFO("Center pixel: R=" << (int)pixels[centerIdx]
             << " G=" << (int)pixels[centerIdx + 1]
             << " B=" << (int)pixels[centerIdx + 2]
             << " A=" << (int)pixels[centerIdx + 3]);

        REQUIRE(ColorApproxEqual(pixels[centerIdx], 255));      // R
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 1], 0));    // G
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 2], 0));    // B
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 3], 255));  // A
    }

    SECTION("Clear to green") {
        renderer.BeginFrame();
        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        size_t centerIdx = (32 * 64 + 32) * 4;

        INFO("Center pixel: R=" << (int)pixels[centerIdx]
             << " G=" << (int)pixels[centerIdx + 1]
             << " B=" << (int)pixels[centerIdx + 2]
             << " A=" << (int)pixels[centerIdx + 3]);

        REQUIRE(ColorApproxEqual(pixels[centerIdx], 0));        // R
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 1], 255));  // G
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 2], 0));    // B
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 3], 255));  // A
    }

    SECTION("Clear to blue") {
        renderer.BeginFrame();
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        size_t centerIdx = (32 * 64 + 32) * 4;

        INFO("Center pixel: R=" << (int)pixels[centerIdx]
             << " G=" << (int)pixels[centerIdx + 1]
             << " B=" << (int)pixels[centerIdx + 2]
             << " A=" << (int)pixels[centerIdx + 3]);

        REQUIRE(ColorApproxEqual(pixels[centerIdx], 0));        // R
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 1], 0));    // G
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 2], 255));  // B
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 3], 255));  // A
    }

    SECTION("Clear to custom color") {
        // Orbiter-style space blue: RGB(51, 102, 204) = (0.2, 0.4, 0.8)
        float r = 0.2f, g = 0.4f, b = 0.8f, a = 1.0f;

        renderer.BeginFrame();
        renderer.Clear(r, g, b, a);
        renderer.EndFrame();
        renderer.Submit();

        auto pixels = renderer.ReadPixels();
        size_t centerIdx = (32 * 64 + 32) * 4;

        uint8_t expectedR = FloatToUint8(r);
        uint8_t expectedG = FloatToUint8(g);
        uint8_t expectedB = FloatToUint8(b);
        uint8_t expectedA = FloatToUint8(a);

        INFO("Expected: R=" << (int)expectedR << " G=" << (int)expectedG
             << " B=" << (int)expectedB << " A=" << (int)expectedA);
        INFO("Actual: R=" << (int)pixels[centerIdx]
             << " G=" << (int)pixels[centerIdx + 1]
             << " B=" << (int)pixels[centerIdx + 2]
             << " A=" << (int)pixels[centerIdx + 3]);

        REQUIRE(ColorApproxEqual(pixels[centerIdx], expectedR));
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 1], expectedG));
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 2], expectedB));
        REQUIRE(ColorApproxEqual(pixels[centerIdx + 3], expectedA));
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
    size_t centerIdx = (16 * 32 + 16) * 4;
    REQUIRE(ColorApproxEqual(pixels1[centerIdx], 255));      // R
    REQUIRE(ColorApproxEqual(pixels1[centerIdx + 1], 0));    // G

    // Frame 2: Green
    renderer.BeginFrame();
    renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
    renderer.EndFrame();
    renderer.Submit();

    auto pixels2 = renderer.ReadPixels();
    REQUIRE(ColorApproxEqual(pixels2[centerIdx], 0));        // R
    REQUIRE(ColorApproxEqual(pixels2[centerIdx + 1], 255));  // G

    // Frame 3: Blue
    renderer.BeginFrame();
    renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
    renderer.EndFrame();
    renderer.Submit();

    auto pixels3 = renderer.ReadPixels();
    REQUIRE(ColorApproxEqual(pixels3[centerIdx], 0));        // R
    REQUIRE(ColorApproxEqual(pixels3[centerIdx + 1], 0));    // G
    REQUIRE(ColorApproxEqual(pixels3[centerIdx + 2], 255));  // B

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

TEST_CASE("HeadlessRenderer diagnostics", "[Vulkan][Headless]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));

    // Print diagnostics (useful for debugging in CI/CD)
    renderer.PrintDiagnostics();

    std::string gpuName = renderer.GetGPUName();
    INFO("GPU: " << gpuName);
    REQUIRE(!gpuName.empty());

    renderer.Shutdown();
}

// Phase 2 placeholder tests (to be implemented with geometry support)
/*
TEST_CASE("HeadlessRenderer draw triangle", "[Vulkan][Headless][Geometry]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));

    // TODO: Implement when geometry support is added
    // renderer.BeginFrame();
    // renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    // renderer.DrawTriangle(vertices);
    // renderer.EndFrame();
    // renderer.Submit();
    //
    // auto pixels = renderer.ReadPixels();
    // // Verify triangle pixels are set

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer textured quad", "[Vulkan][Headless][Geometry]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));

    // TODO: Implement when texture support is added

    renderer.Shutdown();
}

TEST_CASE("HeadlessRenderer MVP transforms", "[Vulkan][Headless][Geometry]")
{
    HeadlessRenderer renderer;
    REQUIRE(renderer.Init(256, 256));

    // TODO: Implement when uniform buffer support is added

    renderer.Shutdown();
}
*/
