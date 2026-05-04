cmake_minimum_required(VERSION 3.15)
set(CMAKE_DISABLE_FIND_PACKAGE_Doxygen TRUE)
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -allow-unsupported-compiler")
project(PathtracerBenchmark LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
option(ENABLE_CUDA "Enable CUDA backend" ON)
message(STATUS "ENABLE_CUDA = ${ENABLE_CUDA}")

if(ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit)
endif()

add_subdirectory(vendor/glfw)

include_directories(
    vendor/glfw/include
    vendor/glm
    vendor/io
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/resources/kernel
)

file(GLOB_RECURSE SOURCES "src/*.cpp")

if(CUDAToolkit_FOUND AND ENABLE_CUDA)
    file(GLOB_RECURSE CUDA_SOURCES "src/*.cu")
    list(APPEND SOURCES ${CUDA_SOURCES})
    add_compile_definitions(HAS_CUDA)
    add_compile_definitions(NOMINMAX)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
endif()

add_executable(PathtracerBenchmark ${SOURCES})

target_link_libraries(PathtracerBenchmark Vulkan::Vulkan glfw)

if(CUDAToolkit_FOUND AND ENABLE_CUDA)
    set_target_properties(PathtracerBenchmark PROPERTIES
        CUDA_ARCHITECTURES native
    )

    target_compile_options(PathtracerBenchmark PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:
            --use_fast_math
            -Xptxas -v
            -Xptxas -d
            -lineinfo
            --maxrregcount=64
            -allow-unsupported-compiler
            -Wno-deprecated-gpu-targets
        >
    )

    target_link_libraries(PathtracerBenchmark CUDA::cudart)
    target_include_directories(PathtracerBenchmark PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
endif()

# ============================================================
# Compile shaders into resources/shaders
# ============================================================
find_program(GLSLC glslc)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Please install the Vulkan SDK.")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/resources/shaders")

# Compile shaders on every build so Visual Studio never reuses stale SPIR-V
set(PATHTRACER_SPV "${SHADER_SOURCE_DIR}/pathtracer.spv")
set(VERT_SPV "${SHADER_SOURCE_DIR}/vert.spv")
set(FRAG_SPV "${SHADER_SOURCE_DIR}/frag.spv")

add_custom_target(CompileShaders ALL
    COMMAND ${GLSLC} -o "${PATHTRACER_SPV}" "${SHADER_SOURCE_DIR}/pathtracer.comp"
    COMMAND ${GLSLC} -o "${VERT_SPV}" "${SHADER_SOURCE_DIR}/quad.vert"
    COMMAND ${GLSLC} -o "${FRAG_SPV}" "${SHADER_SOURCE_DIR}/quad.frag"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Compiling shaders"
    VERBATIM
)
add_dependencies(PathtracerBenchmark CompileShaders)