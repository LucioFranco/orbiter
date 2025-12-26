// ==============================================================
// VulkanMemory.h
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2024
//
// VMA (Vulkan Memory Allocator) wrapper header.
// Include this in exactly ONE .cpp file with VMA_IMPLEMENTATION defined.
// ==============================================================

#ifndef VULKANMEMORY_H
#define VULKANMEMORY_H

// VMA configuration - must come before including vk_mem_alloc.h
#define VMA_VULKAN_VERSION 1002000  // Vulkan 1.2

// VMA header
#include <vk_mem_alloc.h>

#endif // VULKANMEMORY_H
