// ==============================================================
// VulkanSwapchain.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "VulkanSwapchain.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

VulkanSwapchain::VulkanSwapchain()
    : m_initialized(false)
    , m_ctx(nullptr)
    , m_surface(VK_NULL_HANDLE)
    , m_swapchain(VK_NULL_HANDLE)
    , m_format(VK_FORMAT_UNDEFINED)
    , m_extent{0, 0}
    , m_renderPass(VK_NULL_HANDLE)
    , m_depthFormat(VK_FORMAT_UNDEFINED)
    , m_depthImage(VK_NULL_HANDLE)
    , m_depthMemory(VK_NULL_HANDLE)
    , m_depthImageView(VK_NULL_HANDLE)
    , m_currentImageIndex(0)
    , m_currentFrame(0)
{
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        m_renderFinishedSemaphores[i] = VK_NULL_HANDLE;
        m_inFlightFences[i] = VK_NULL_HANDLE;
    }
}

VulkanSwapchain::~VulkanSwapchain()
{
    Shutdown();
}

bool VulkanSwapchain::Init(VulkanContext* ctx, VkSurfaceKHR surface, uint32_t width, uint32_t height)
{
    if (m_initialized) {
        Shutdown();
    }

    m_ctx = ctx;
    m_surface = surface;
    m_extent = { width, height };

    if (!CreateSwapchain()) {
        std::cerr << "[VulkanSwapchain] Failed to create swapchain" << std::endl;
        return false;
    }
    std::cout << "[VulkanSwapchain] Swapchain created: " << m_extent.width << "x" << m_extent.height << std::endl;

    if (!CreateImageViews()) {
        std::cerr << "[VulkanSwapchain] Failed to create image views" << std::endl;
        return false;
    }
    std::cout << "[VulkanSwapchain] Image views created: " << m_imageViews.size() << std::endl;

    if (!CreateDepthBuffer()) {
        std::cerr << "[VulkanSwapchain] Failed to create depth buffer" << std::endl;
        return false;
    }
    std::cout << "[VulkanSwapchain] Depth buffer created" << std::endl;

    if (!CreateRenderPass()) {
        std::cerr << "[VulkanSwapchain] Failed to create render pass" << std::endl;
        return false;
    }
    std::cout << "[VulkanSwapchain] Render pass created" << std::endl;

    if (!CreateFramebuffers()) {
        std::cerr << "[VulkanSwapchain] Failed to create framebuffers" << std::endl;
        return false;
    }
    std::cout << "[VulkanSwapchain] Framebuffers created: " << m_framebuffers.size() << std::endl;

    if (!CreateSyncObjects()) {
        std::cerr << "[VulkanSwapchain] Failed to create sync objects" << std::endl;
        return false;
    }
    std::cout << "[VulkanSwapchain] Sync objects created" << std::endl;

    // Initialize per-image fences to VK_NULL_HANDLE (one per swapchain image)
    m_imagesInFlight.assign(m_images.size(), VK_NULL_HANDLE);

    m_initialized = true;
    return true;
}

void VulkanSwapchain::Shutdown()
{
    if (m_ctx) {
        m_ctx->WaitIdle();
    }

    DestroySyncObjects();
    CleanupSwapchain();

    m_ctx = nullptr;
    m_surface = VK_NULL_HANDLE;
    m_initialized = false;
}

bool VulkanSwapchain::Recreate(uint32_t width, uint32_t height)
{
    if (!m_ctx || m_surface == VK_NULL_HANDLE) {
        return false;
    }

    m_ctx->WaitIdle();

    // Store old swapchain for cleanup
    VkSwapchainKHR oldSwapchain = m_swapchain;

    // Update extent
    m_extent = { width, height };

    // Cleanup old resources (except swapchain itself, we'll pass it to create)
    CleanupSwapchain();

    // Create new swapchain
    if (!CreateSwapchain()) {
        return false;
    }

    if (!CreateImageViews()) {
        return false;
    }

    if (!CreateDepthBuffer()) {
        return false;
    }

    if (!CreateRenderPass()) {
        return false;
    }

    if (!CreateFramebuffers()) {
        return false;
    }

    // Reset per-image tracking for new swapchain images
    m_imagesInFlight.assign(m_images.size(), VK_NULL_HANDLE);

    std::cout << "[VulkanSwapchain] Recreated: " << width << "x" << height << std::endl;
    return true;
}

bool VulkanSwapchain::AcquireNextImage()
{
    VkDevice device = m_ctx->GetDevice();

    // Wait for the fence of the current frame
    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // Acquire image
    VkResult result = vkAcquireNextImageKHR(
        device,
        m_swapchain,
        UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE,
        &m_currentImageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;  // Swapchain needs recreation
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "[VulkanSwapchain] Failed to acquire swapchain image" << std::endl;
        return false;
    }

    // If image is already in flight, wait for it
    if (!m_imagesInFlight.empty() && m_imagesInFlight[m_currentImageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &m_imagesInFlight[m_currentImageIndex], VK_TRUE, UINT64_MAX);
    }

    // Associate image with the fence for this frame
    if (!m_imagesInFlight.empty()) {
        m_imagesInFlight[m_currentImageIndex] = m_inFlightFences[m_currentFrame];
    }

    // Reset fence only when we know we're submitting work
    vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);

    return true;
}

bool VulkanSwapchain::Present()
{
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_ctx->GetPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false;
    }

    if (result != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] Failed to present swapchain image" << std::endl;
        return false;
    }

    return true;
}

void VulkanSwapchain::AdvanceFrame()
{
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkFramebuffer VulkanSwapchain::GetCurrentFramebuffer() const
{
    if (m_currentImageIndex < m_framebuffers.size()) {
        return m_framebuffers[m_currentImageIndex];
    }
    return VK_NULL_HANDLE;
}

VkSemaphore VulkanSwapchain::GetImageAvailableSemaphore() const
{
    return m_imageAvailableSemaphores[m_currentFrame];
}

VkSemaphore VulkanSwapchain::GetRenderFinishedSemaphore() const
{
    return m_renderFinishedSemaphores[m_currentFrame];
}

VkFence VulkanSwapchain::GetInFlightFence() const
{
    return m_inFlightFences[m_currentFrame];
}

bool VulkanSwapchain::CreateSwapchain()
{
    VkPhysicalDevice physicalDevice = m_ctx->GetPhysicalDevice();
    VkDevice device = m_ctx->GetDevice();

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_surface, &capabilities);

    VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat();
    VkPresentModeKHR presentMode = ChoosePresentMode();
    VkExtent2D extent = ChooseExtent(m_extent.width, m_extent.height);

    // Request one more image than minimum for triple buffering
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {
        m_ctx->GetGraphicsQueueFamily(),
        m_ctx->GetPresentQueueFamily()
    };

    if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        return false;
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, m_images.data());

    m_format = surfaceFormat.format;
    m_extent = extent;

    return true;
}

bool VulkanSwapchain::CreateImageViews()
{
    VkDevice device = m_ctx->GetDevice();

    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool VulkanSwapchain::CreateRenderPass()
{
    // Color attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Don't need depth after render
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Dependencies for color and depth
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    return vkCreateRenderPass(m_ctx->GetDevice(), &renderPassInfo, nullptr, &m_renderPass) == VK_SUCCESS;
}

bool VulkanSwapchain::CreateFramebuffers()
{
    VkDevice device = m_ctx->GetDevice();

    m_framebuffers.resize(m_imageViews.size());

    for (size_t i = 0; i < m_imageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            m_imageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = m_extent.width;
        fbInfo.height = m_extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool VulkanSwapchain::CreateSyncObjects()
{
    VkDevice device = m_ctx->GetDevice();

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled so first frame doesn't wait forever

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat()
{
    VkPhysicalDevice physicalDevice = m_ctx->GetPhysicalDevice();

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, nullptr);

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, formats.data());

    // Prefer BGRA8 SRGB
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    // Fall back to BGRA8 UNORM
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM) {
            return format;
        }
    }

    // Just use the first available format
    return formats[0];
}

VkPresentModeKHR VulkanSwapchain::ChoosePresentMode()
{
    VkPhysicalDevice physicalDevice = m_ctx->GetPhysicalDevice();

    uint32_t modeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_surface, &modeCount, nullptr);

    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_surface, &modeCount, modes.data());

    // Prefer mailbox (triple buffering without tearing)
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    // Fall back to FIFO (vsync, always supported)
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::ChooseExtent(uint32_t width, uint32_t height)
{
    VkPhysicalDevice physicalDevice = m_ctx->GetPhysicalDevice();

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_surface, &capabilities);

    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent = { width, height };
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return extent;
}

VkFormat VulkanSwapchain::ChooseDepthFormat()
{
    VkPhysicalDevice physicalDevice = m_ctx->GetPhysicalDevice();

    // Preferred formats in order of preference
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    // D16_UNORM is required to be supported, so this shouldn't happen
    return VK_FORMAT_D16_UNORM;
}

bool VulkanSwapchain::CreateDepthBuffer()
{
    VkDevice device = m_ctx->GetDevice();

    m_depthFormat = ChooseDepthFormat();

    // Create depth image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_depthFormat;
    imageInfo.extent = { m_extent.width, m_extent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, m_depthImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_ctx->FindMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthMemory) != VK_SUCCESS) {
        vkDestroyImage(device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(device, m_depthImage, m_depthMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        vkFreeMemory(device, m_depthMemory, nullptr);
        vkDestroyImage(device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
        m_depthMemory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void VulkanSwapchain::DestroyDepthBuffer()
{
    VkDevice device = m_ctx ? m_ctx->GetDevice() : VK_NULL_HANDLE;

    if (device == VK_NULL_HANDLE) {
        return;
    }

    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }

    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }

    if (m_depthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, m_depthMemory, nullptr);
        m_depthMemory = VK_NULL_HANDLE;
    }
}

void VulkanSwapchain::CleanupSwapchain()
{
    VkDevice device = m_ctx ? m_ctx->GetDevice() : VK_NULL_HANDLE;

    if (device == VK_NULL_HANDLE) {
        return;
    }

    for (auto fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
    }
    m_framebuffers.clear();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    DestroyDepthBuffer();

    for (auto view : m_imageViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    m_imageViews.clear();
    m_images.clear();
    m_imagesInFlight.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VulkanSwapchain::DestroySyncObjects()
{
    VkDevice device = m_ctx ? m_ctx->GetDevice() : VK_NULL_HANDLE;

    if (device == VK_NULL_HANDLE) {
        return;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, m_imageAvailableSemaphores[i], nullptr);
            m_imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
        if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, m_renderFinishedSemaphores[i], nullptr);
            m_renderFinishedSemaphores[i] = VK_NULL_HANDLE;
        }
        if (m_inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, m_inFlightFences[i], nullptr);
            m_inFlightFences[i] = VK_NULL_HANDLE;
        }
    }
}
