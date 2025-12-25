# Copyright (c) Martin Schweiger
# Licensed under the MIT License

project(VulkanClient VERSION 1.0)

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

set(VULKAN_SOURCE_DIR ${CMAKE_SOURCE_DIR}/OVP/VulkanClient)

set(SourceFiles
    VulkanClient.cpp
)

set(IncludeFiles
    VulkanClient.h
)

add_library(VulkanClient MODULE
    ${SourceFiles}
    ${IncludeFiles}
)

target_include_directories(VulkanClient PUBLIC
    ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_directories(VulkanClient PUBLIC
    ${ORBITER_BINARY_SDK_DIR}/lib
)

target_link_libraries(VulkanClient
    ${ORBITER_LIB}
    ${ORBITER_SDK_LIB}
    Vulkan::Vulkan
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
