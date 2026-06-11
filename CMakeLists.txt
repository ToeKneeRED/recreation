cmake_minimum_required(VERSION 3.24)
project(recreation VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RECREATION_BUILD_TOOLS "Build offline tools" ON)
option(RECREATION_ENABLE_VALIDATION "Enable vulkan validation layers in debug" ON)
option(RECREATION_FETCH_SDL3 "Download SDL3 when not found on the system" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(warnings)

include(FetchContent)
find_package(ZLIB QUIET)

# Vulkan headers and the volk loader are pinned and fetched, no SDK needed.
# volk dlopens the system vulkan loader at runtime, missing drivers degrade
# the renderer to a stub instead of failing the build.
FetchContent_Declare(VulkanHeaders
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG v1.4.304
  GIT_SHALLOW ON)
FetchContent_Declare(volk
  GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG 1.4.304
  GIT_SHALLOW ON)
FetchContent_Declare(VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG v3.1.0
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(VulkanHeaders volk VulkanMemoryAllocator)
target_link_libraries(volk PUBLIC Vulkan::Headers)

# Shaders compile to spirv at build time and get embedded. Prefer a host
# glslang (required when cross compiling, e.g. android), build one otherwise.
find_program(RECREATION_GLSLANG NAMES glslang glslangValidator)
if(NOT RECREATION_GLSLANG)
  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "cross compiling needs a host glslang in PATH")
  endif()
  set(ENABLE_OPT OFF)
  set(ENABLE_HLSL OFF)
  set(ENABLE_SPVREMAPPER OFF)
  set(GLSLANG_TESTS OFF)
  set(GLSLANG_ENABLE_INSTALL OFF)
  FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 15.1.0
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(glslang)
  message(STATUS "glslang not found, building it")
endif()
include(shaders)

find_package(SDL3 QUIET)
if(NOT SDL3_FOUND AND RECREATION_FETCH_SDL3)
  set(SDL_TEST_LIBRARY OFF)
  # Static keeps the artifacts self contained, no SDL3.dll shipping.
  set(SDL_SHARED OFF)
  set(SDL_STATIC ON)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.16
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(SDL3)
  set(SDL3_FOUND TRUE)
endif()
if(SDL3_FOUND)
  message(STATUS "sdl3 found, native windowing enabled")
else()
  message(STATUS "sdl3 not found, runtime is headless")
endif()

set(RECREATION_MODULES
  core
  ecs
  asset
  render
  bethesda
  world
  net
  script
)

foreach(module ${RECREATION_MODULES})
  add_subdirectory(engine/${module})
endforeach()

add_subdirectory(runtime)

if(RECREATION_BUILD_TOOLS)
  add_subdirectory(tools/esminfo)
endif()
