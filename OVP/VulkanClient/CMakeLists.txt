# Copyright (c) Martin Schweiger
# Licensed under the MIT License

project(VulkanClient VERSION 1.0)

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

# Find glslc shader compiler
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Please install the Vulkan SDK.")
endif()

# Fetch GLM for matrix math (header-only)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
# Configure GLM as header-only before making available
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# Fetch VMA (Vulkan Memory Allocator) - header-only
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vma)

# Fetch stb (stb_image for texture loading) - header-only
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

set(VULKAN_SOURCE_DIR ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)

# Shader compilation function
# Compiles GLSL to SPIR-V and generates a C header with the bytecode
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/Shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

function(compile_shader SHADER_SOURCE)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    string(REPLACE "." "_" SHADER_VAR ${SHADER_NAME})
    set(SPV_FILE ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC} -o ${SPV_FILE} ${SHADER_SOURCE}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling shader ${SHADER_NAME}"
        VERBATIM
    )
    set(SHADER_SPV_FILES ${SHADER_SPV_FILES} ${SPV_FILE} PARENT_SCOPE)
endfunction()

# Compile shaders
compile_shader(${VULKAN_SOURCE_DIR}/Shaders/basic.vert)
compile_shader(${VULKAN_SOURCE_DIR}/Shaders/basic.frag)
compile_shader(${VULKAN_SOURCE_DIR}/Shaders/textured.vert)
compile_shader(${VULKAN_SOURCE_DIR}/Shaders/textured.frag)
compile_shader(${VULKAN_SOURCE_DIR}/Shaders/mesh.vert)
compile_shader(${VULKAN_SOURCE_DIR}/Shaders/mesh.frag)

# Create custom target for shaders
add_custom_target(VulkanShaders DEPENDS ${SHADER_SPV_FILES})

# Fetch ImGui (needed by both HeadlessRenderer for testing and VulkanClient)
FetchContent_MakeAvailable(imgui)

# VulkanCore - shared Vulkan infrastructure (no Orbiter dependencies)
add_library(VulkanCore STATIC
    Core/VulkanContext.cpp
    Core/VulkanContext.h
    Core/VulkanSwapchain.cpp
    Core/VulkanSwapchain.h
    Core/VulkanPipeline.cpp
    Core/VulkanPipeline.h
    Core/TexturedPipeline.cpp
    Core/TexturedPipeline.h
    Core/VulkanBuffer.cpp
    Core/VulkanBuffer.h
    Core/StagingManager.cpp
    Core/StagingManager.h
    Core/SceneRenderer.cpp
    Core/SceneRenderer.h
    Core/VulkanTexture.cpp
    Core/VulkanTexture.h
    Core/DDSLoader.cpp
    Core/DDSLoader.h
    Core/VulkanDescriptors.cpp
    Core/VulkanDescriptors.h
    Core/VulkanMemory.h
    Core/RenderPassFactory.cpp
    Core/RenderPassFactory.h
    Core/MeshPipeline.cpp
    Core/MeshPipeline.h
    Mesh/VulkanMesh.cpp
    Mesh/VulkanMesh.h
    Mesh/SphereGenerator.cpp
    Mesh/SphereGenerator.h
    Resources/TextureManager.cpp
    Resources/TextureManager.h
    Tile/TileCoord.h
    Tile/PatchMeshGenerator.h
    Tile/TileTextureLoader.cpp
    Tile/TileTextureLoader.h
    Tile/TileRenderer.cpp
    Tile/TileRenderer.h
    Tile/TreeArchive.cpp
    Tile/TreeArchive.h
)

target_include_directories(VulkanCore PUBLIC
    ${Vulkan_INCLUDE_DIRS}
    ${VULKAN_SOURCE_DIR}
    ${vma_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
)

target_link_libraries(VulkanCore PUBLIC
    Vulkan::Vulkan
    glm::glm
    zlib
)

# VMA is header-only, included via VulkanMemory.h

# Add shader output directory to include paths (for loading compiled shaders)
target_compile_definitions(VulkanCore PUBLIC
    SHADER_DIR="${SHADER_OUTPUT_DIR}"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

# Ensure shaders are compiled before VulkanCore
add_dependencies(VulkanCore VulkanShaders)

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

# HeadlessGraphicsClient - GraphicsClient using HeadlessRenderer for testing
add_library(HeadlessGraphicsClient MODULE
    HeadlessGraphicsClient.cpp
    HeadlessGraphicsClient.h
)

target_include_directories(HeadlessGraphicsClient PUBLIC
    ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${VULKAN_SOURCE_DIR}
)

target_link_directories(HeadlessGraphicsClient PUBLIC
    ${ORBITER_BINARY_SDK_DIR}/lib
)

target_link_libraries(HeadlessGraphicsClient
    HeadlessRenderer
    ${ORBITER_LIB}
)

# Force include all symbols from Orbitersdk (needed for DllMain entry point)
target_link_options(HeadlessGraphicsClient PRIVATE
    /WHOLEARCHIVE:$<TARGET_FILE:Orbitersdk>
)

target_compile_definitions(HeadlessGraphicsClient PRIVATE
    HEADLESSGRAPHICSCLIENT_EXPORTS
)

set_target_properties(HeadlessGraphicsClient
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Modules/Plugin
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Modules/Plugin
    FOLDER OVP
)

add_dependencies(HeadlessGraphicsClient
    ${OrbiterTgt}
    Orbitersdk
)

install(TARGETS HeadlessGraphicsClient
    LIBRARY
    DESTINATION ${ORBITER_INSTALL_PLUGIN_DIR}
)
