// ==============================================================
// VulkanDescriptors.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
// ==============================================================

#include "VulkanDescriptors.h"
#include "VulkanContext.h"

// ============================================================================
// VulkanDescriptorPool
// ============================================================================

VulkanDescriptorPool::VulkanDescriptorPool()
    : m_initialized(false)
    , m_ctx(nullptr)
    , m_pool(VK_NULL_HANDLE)
    , m_maxSets(0)
    , m_allocatedSets(0)
{
}

VulkanDescriptorPool::~VulkanDescriptorPool()
{
    Shutdown();
}

bool VulkanDescriptorPool::Init(VulkanContext* ctx, uint32_t maxSets)
{
    if (m_initialized) {
        return false;  // Already initialized
    }
    if (!ctx || !ctx->IsInitialized()) {
        return false;
    }

    m_ctx = ctx;
    m_maxSets = maxSets;

    // Pool sizes for common descriptor types
    // These are totals across all sets allocated from this pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, maxSets / 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets / 2 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 2 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets },
        { VK_DESCRIPTOR_TYPE_SAMPLER, maxSets / 2 },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Allow individual frees
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(ctx->GetDevice(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        return false;
    }

    m_initialized = true;
    return true;
}

void VulkanDescriptorPool::Shutdown()
{
    if (!m_initialized || !m_ctx) {
        return;
    }

    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_ctx->GetDevice(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }

    m_ctx = nullptr;
    m_maxSets = 0;
    m_allocatedSets = 0;
    m_initialized = false;
}

VkDescriptorSet VulkanDescriptorPool::AllocateSet(VkDescriptorSetLayout layout)
{
    if (!m_initialized || layout == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_ctx->GetDevice(), &allocInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    m_allocatedSets++;
    return set;
}

void VulkanDescriptorPool::FreeSet(VkDescriptorSet set)
{
    if (!m_initialized || set == VK_NULL_HANDLE) {
        return;
    }

    vkFreeDescriptorSets(m_ctx->GetDevice(), m_pool, 1, &set);
    if (m_allocatedSets > 0) {
        m_allocatedSets--;
    }
}

void VulkanDescriptorPool::Reset()
{
    if (!m_initialized) {
        return;
    }

    vkResetDescriptorPool(m_ctx->GetDevice(), m_pool, 0);
    m_allocatedSets = 0;
}

// ============================================================================
// VulkanDescriptorSetLayoutBuilder
// ============================================================================

VulkanDescriptorSetLayoutBuilder::VulkanDescriptorSetLayoutBuilder()
{
}

VulkanDescriptorSetLayoutBuilder& VulkanDescriptorSetLayoutBuilder::AddBinding(
    uint32_t binding,
    VkDescriptorType type,
    VkShaderStageFlags stageFlags,
    uint32_t count)
{
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = type;
    layoutBinding.descriptorCount = count;
    layoutBinding.stageFlags = stageFlags;
    layoutBinding.pImmutableSamplers = nullptr;

    m_bindings.push_back(layoutBinding);
    return *this;
}

VkDescriptorSetLayout VulkanDescriptorSetLayoutBuilder::Build(VulkanContext* ctx)
{
    if (!ctx || !ctx->IsInitialized() || m_bindings.empty()) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(m_bindings.size());
    layoutInfo.pBindings = m_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(ctx->GetDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    return layout;
}

void VulkanDescriptorSetLayoutBuilder::Clear()
{
    m_bindings.clear();
}

// ============================================================================
// VulkanDescriptorWriter
// ============================================================================

VulkanDescriptorWriter::VulkanDescriptorWriter()
{
}

VulkanDescriptorWriter& VulkanDescriptorWriter::WriteBuffer(
    uint32_t binding,
    VkDescriptorType type,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range)
{
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;
    m_bufferInfos.push_back(bufferInfo);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.descriptorCount = 1;
    // pBufferInfo will be set in Update() using index
    write.pBufferInfo = reinterpret_cast<VkDescriptorBufferInfo*>(m_bufferInfos.size() - 1);

    m_writes.push_back(write);
    return *this;
}

VulkanDescriptorWriter& VulkanDescriptorWriter::WriteImage(
    uint32_t binding,
    VkDescriptorType type,
    VkSampler sampler,
    VkImageView imageView,
    VkImageLayout layout)
{
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = layout;
    m_imageInfos.push_back(imageInfo);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.descriptorCount = 1;
    // pImageInfo will be set in Update() using index stored as pointer value
    write.pImageInfo = reinterpret_cast<VkDescriptorImageInfo*>(m_imageInfos.size() - 1);

    m_writes.push_back(write);
    return *this;
}

VulkanDescriptorWriter& VulkanDescriptorWriter::WriteCombinedImageSampler(
    uint32_t binding,
    VkSampler sampler,
    VkImageView imageView,
    VkImageLayout layout)
{
    return WriteImage(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      sampler, imageView, layout);
}

void VulkanDescriptorWriter::Update(VkDevice device, VkDescriptorSet set)
{
    if (m_writes.empty()) {
        return;
    }

    // Fix up pointers - we stored indices as pointer values
    for (auto& write : m_writes) {
        write.dstSet = set;

        if (write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
            write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
            write.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
            write.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
            // Retrieve index and set actual pointer
            size_t index = reinterpret_cast<size_t>(write.pBufferInfo);
            write.pBufferInfo = &m_bufferInfos[index];
        } else {
            // Image type - retrieve index and set actual pointer
            size_t index = reinterpret_cast<size_t>(write.pImageInfo);
            write.pImageInfo = &m_imageInfos[index];
        }
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(m_writes.size()),
                           m_writes.data(), 0, nullptr);
}

void VulkanDescriptorWriter::Clear()
{
    m_writes.clear();
    m_bufferInfos.clear();
    m_imageInfos.clear();
}
