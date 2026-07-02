cmake_minimum_required(VERSION 3.15)

project(PathtracerBenchmark LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# OPTIONS
# ------------------------------------------------------------
option(ENABLE_CUDA "Enable CUDA runtime backend (requires NVIDIA driver)" OFF)
option(BUILD_TESTS "Build unit tests" OFF)
option(USE_FETCHED_VULKAN_LOADER "Build Vulkan-Loader from source when SDK is not found" ON)

message(STATUS "ENABLE_CUDA = ${ENABLE_CUDA}")
set(ENABLE_CUDA_BACKEND ${ENABLE_CUDA})

find_package(Vulkan QUIET)

if(ENABLE_CUDA)
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -allow-unsupported-compiler")
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)

    if(NOT CUDAToolkit_FOUND)
        message(WARNING "ENABLE_CUDA is ON but CUDAToolkit was not found. Disabling CUDA backend.")
        set(ENABLE_CUDA_BACKEND OFF)
    else()
     	message(STATUS "Found CUDAToolkit. Enabling CUDA backend.")
    endif()
endif()

# ------------------------------------------------------------
# Fetch Vulkan Headers
# ------------------------------------------------------------
include(FetchContent)
if(NOT TARGET Vulkan::Headers)
    set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG v1.3.290
    )

    FetchContent_MakeAvailable(VulkanHeaders)
endif()

# ------------------------------------------------------------
# GLFW
# ------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/glfw)

# ------------------------------------------------------------
# Target
# ------------------------------------------------------------
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(PathtracerBenchmark ${SOURCES})

set_target_properties(PathtracerBenchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_compile_definitions(PathtracerBenchmark PRIVATE
    RESOURCE_PATH_PREFIX="${CMAKE_SOURCE_DIR}/src/resources/"
)

if(WIN32)
    target_compile_definitions(PathtracerBenchmark PRIVATE
        NOMINMAX
        VK_USE_PLATFORM_WIN32_KHR
    )
endif()

# ------------------------------------------------------------
# Includes
# ------------------------------------------------------------
target_include_directories(PathtracerBenchmark PRIVATE
    vendor/glfw/include
    vendor/glm
    vendor/io
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/resources/kernel
)

# ------------------------------------------------------------
# Vulkan loader
# ------------------------------------------------------------
set(VULKAN_LOADER_TARGET "")

if(TARGET Vulkan::Vulkan)
    set(VULKAN_LOADER_TARGET Vulkan::Vulkan)
else()
    find_library(VULKAN_LIB
        NAMES vulkan-1 vulkan
    )

    if(VULKAN_LIB)
        set(VULKAN_LOADER_TARGET ${VULKAN_LIB})
    elseif(USE_FETCHED_VULKAN_LOADER)
        set(VULKAN_LOADER_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
        set(VULKAN_LOADER_ENABLE_WSI_WIN32 ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            VulkanLoader
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader.git
            GIT_TAG v1.3.290
        )

        FetchContent_MakeAvailable(VulkanLoader)

        if(TARGET vulkan)
            set(VULKAN_LOADER_TARGET vulkan)
        elseif(TARGET Vulkan::Vulkan)
            set(VULKAN_LOADER_TARGET Vulkan::Vulkan)
        endif()
    endif()
endif()

if(NOT VULKAN_LOADER_TARGET)
    message(FATAL_ERROR "Vulkan loader not found. Install Vulkan SDK or set USE_FETCHED_VULKAN_LOADER=ON.")
endif()

target_link_libraries(PathtracerBenchmark PRIVATE
    glfw
    Vulkan::Headers
    ${VULKAN_LOADER_TARGET}
)

# ------------------------------------------------------------
# CUDA runtime backend (optional)
# ------------------------------------------------------------
if(ENABLE_CUDA_BACKEND)
    message(STATUS "CUDA backend enabled (runtime API)")

    file(GLOB_RECURSE CUDA_SOURCES "src/*.cu")
    if(CUDA_SOURCES)
        target_sources(PathtracerBenchmark PRIVATE ${CUDA_SOURCES})
    endif()

    set_target_properties(PathtracerBenchmark PROPERTIES
        CUDA_ARCHITECTURES native
    )

    target_compile_options(PathtracerBenchmark PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:
            --use_fast_math
            -Xptxas -v
            -lineinfo
            --maxrregcount=64
            -allow-unsupported-compiler
            -Wno-deprecated-gpu-targets
        >
    )

    target_compile_definitions(PathtracerBenchmark PRIVATE HAS_CUDA)
    target_link_libraries(PathtracerBenchmark PRIVATE CUDA::cudart)
    target_include_directories(PathtracerBenchmark PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
else()
    message(STATUS "CUDA backend disabled")
endif()

# ------------------------------------------------------------
# Precompiled shaders
# ------------------------------------------------------------
set(SHADER_DIR "${CMAKE_SOURCE_DIR}/src/resources/shaders")

set(SPIRV_FILES
    "${SHADER_DIR}/pathtracer.spv"
    "${SHADER_DIR}/vert.spv"
    "${SHADER_DIR}/frag.spv"
)

foreach(SPIRV ${SPIRV_FILES})
    if(NOT EXISTS ${SPIRV})
        message(WARNING "Missing shader: ${SPIRV}")
    endif()
endforeach()

# ------------------------------------------------------------
# TESTS
# ------------------------------------------------------------
if(BUILD_TESTS)
    message(WARNING "Tests disabled in this configuration")
endif()