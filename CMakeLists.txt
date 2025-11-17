set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.15)
project(VulkanApp)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Vulkan REQUIRED)
add_subdirectory(vendor/glfw)
include_directories(
    vendor/glfw/include
    vendor/glm
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(VulkanApp ${SOURCES})
target_link_libraries(VulkanApp Vulkan::Vulkan glfw)
