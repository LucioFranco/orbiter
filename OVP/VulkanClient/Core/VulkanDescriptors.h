// ==============================================================
// VulkanDescriptors.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// Descriptor pool and set management for Vulkan resources.
// ==============================================================

#ifndef VULKANDESCRIPTORS_H
#define VULKANDESCRIPTORS_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

class VulkanContext;

// Simple descriptor pool manager
// Creates pools on demand and tracks allocated sets
class VulkanDescriptorPool {
public:
    VulkanDescriptorPool();
    ~VulkanDescriptorPool();

    // Lifecycle
    bool Init(VulkanContext* ctx, uint32_t maxSets = 1000);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Allocation
    VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);
    void FreeSet(VkDescriptorSet set);
    void Reset();  // Free all sets and reset pool

    // Accessors
    VkDescriptorPool GetPool() const { return m_pool; }

private:
    bool m_initialized;
    VulkanContext* m_ctx;
    VkDescriptorPool m_pool;
    uint32_t m_maxSets;
    uint32_t m_allocatedSets;
};

// Builder for descriptor set layouts
class VulkanDescriptorSetLayoutBuilder {
public:
    VulkanDescriptorSetLayoutBuilder();

    // Add bindings
    VulkanDescriptorSetLayoutBuilder& AddBinding(uint32_t binding,
                                                   VkDescriptorType type,
                                                   VkShaderStageFlags stageFlags,
                                                   uint32_t count = 1);

    // Build the layout
    VkDescriptorSetLayout Build(VulkanContext* ctx);

    // Clear bindings for reuse
    void Clear();

private:
    std::vector<VkDescriptorSetLayoutBinding> m_bindings;
};

// Helper for writing descriptor sets
class VulkanDescriptorWriter {
public:
    VulkanDescriptorWriter();

    // Add writes
    VulkanDescriptorWriter& WriteBuffer(uint32_t binding,
                                         VkDescriptorType type,
                                         VkBuffer buffer,
                                         VkDeviceSize offset,
                                         VkDeviceSize range);

    VulkanDescriptorWriter& WriteImage(uint32_t binding,
                                        VkDescriptorType type,
                                        VkSampler sampler,
                                        VkImageView imageView,
                                        VkImageLayout layout);

    VulkanDescriptorWriter& WriteCombinedImageSampler(uint32_t binding,
                                                       VkSampler sampler,
                                                       VkImageView imageView,
                                                       VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Execute writes
    void Update(VkDevice device, VkDescriptorSet set);

    // Clear for reuse
    void Clear();

private:
    std::vector<VkWriteDescriptorSet> m_writes;
    std::vector<VkDescriptorBufferInfo> m_bufferInfos;
    std::vector<VkDescriptorImageInfo> m_imageInfos;
};

#endif // VULKANDESCRIPTORS_H
