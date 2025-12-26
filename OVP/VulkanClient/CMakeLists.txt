# Copyright (c) Martin Schweiger
# Licensed under the MIT License

project(VulkanClient VERSION 1.0)

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

set(VULKAN_SOURCE_DIR ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)

# Fetch ImGui (needed by both HeadlessRenderer for testing and VulkanClient)
FetchContent_MakeAvailable(imgui)

# VulkanCore - shared Vulkan infrastructure (no Orbiter dependencies)
add_library(VulkanCore STATIC
    Core/VulkanContext.cpp
    Core/VulkanContext.h
    Core/VulkanSwapchain.cpp
    Core/VulkanSwapchain.h
)

target_include_directories(VulkanCore PUBLIC
    ${Vulkan_INCLUDE_DIRS}
    ${VULKAN_SOURCE_DIR}
)

target_link_libraries(VulkanCore PUBLIC
    Vulkan::Vulkan
)

set_target_properties(VulkanCore PROPERTIES
    FOLDER OVP
)

# HeadlessRenderer - static library for testing (uses VulkanCore + ImGui, no Orbiter dependencies)
add_library(HeadlessRenderer STATIC
    HeadlessRenderer.cpp
    HeadlessRenderer.h
    # ImGui core sources (imgui target is header-only interface)
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(HeadlessRenderer PUBLIC
    ${Vulkan_INCLUDE_DIRS}
    ${VULKAN_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(HeadlessRenderer PUBLIC
    VulkanCore
)

# HeadlessRenderer has its own copy of ImGui (not using Orbiter's shared ImGui context)
# EXPORT_IMGUI_CONTEXT: Tell imconfig.h to use dllexport for GImGui (we own the context)
# IMGUI_API=: No dllimport/dllexport for ImGui functions (static library)
target_compile_definitions(HeadlessRenderer PUBLIC
    EXPORT_IMGUI_CONTEXT
    IMGUI_API=
)

set_target_properties(HeadlessRenderer PROPERTIES
    FOLDER OVP
)

# VulkanClient - main graphics client module

set(SourceFiles
    VulkanClient.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)

set(IncludeFiles
    VulkanClient.h
    resource.h
)

set(ResourceFiles
    VulkanClient.rc
)

add_library(VulkanClient MODULE
    ${SourceFiles}
    ${IncludeFiles}
    ${ResourceFiles}
)

target_include_directories(VulkanClient PUBLIC
    ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${VULKAN_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_directories(VulkanClient PUBLIC
    ${ORBITER_BINARY_SDK_DIR}/lib
)

target_link_libraries(VulkanClient
    VulkanCore
    ${ORBITER_LIB}
)

# Force include all symbols from Orbitersdk (needed for DllMain entry point)
target_link_options(VulkanClient PRIVATE
    /WHOLEARCHIVE:$<TARGET_FILE:Orbitersdk>
)

target_compile_definitions(VulkanClient PRIVATE
    VULKANCLIENT_EXPORTS
)

set_target_properties(VulkanClient
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Modules/Plugin
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Modules/Plugin
    FOLDER OVP
)

add_dependencies(VulkanClient
    ${OrbiterTgt}
    Orbitersdk
)

install(TARGETS VulkanClient
    LIBRARY
    DESTINATION ${ORBITER_INSTALL_PLUGIN_DIR}
)
