// ==============================================================
// RenderPassFactory.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Shared render pass creation for unified test/window rendering.
// ==============================================================

#ifndef RENDERPASSFACTORY_H
#define RENDERPASSFACTORY_H

#include <vulkan/vulkan.h>

namespace RenderPassFactory {

// Creates a render pass with standard configuration.
// Both headless and window renderers use this to ensure pipeline compatibility.
// Only finalColorLayout differs: TRANSFER_SRC_OPTIMAL for readback, PRESENT_SRC_KHR for display.
VkRenderPass CreateMainRenderPass(
    VkDevice device,
    VkFormat colorFormat,
    VkFormat depthFormat,
    VkImageLayout finalColorLayout
);

} // namespace RenderPassFactory

#endif // RENDERPASSFACTORY_H
