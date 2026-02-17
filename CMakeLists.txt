set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.15)
set(CMAKE_DISABLE_FIND_PACKAGE_Doxygen TRUE)
project(PathtracerBenchmark)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Vulkan REQUIRED)
add_subdirectory(vendor/glfw)
include_directories(
    vendor/glfw/include
    vendor/glm
    vendor/io
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(PathtracerBenchmark ${SOURCES})
target_link_libraries(PathtracerBenchmark Vulkan::Vulkan glfw)