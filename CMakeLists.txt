# Le Pixophone — live audiovisual instrument (video side).
#
# Port of the 2011 Pure Data + GEM system (pd_original/, patch114) to one
# native application: JUCE for windows, GUI and devices; sokol_gfx for the GPU
# (D3D11 / Metal / GL core). Decisions: Documentation/PIXOPHONE-PLAN.md.
#
# Configure/build (Windows example, from the repo root):
#   cmake -S . -B build
#   cmake --build build --config Release
# Binary: build/LePixophone_artefacts/<config>/Le Pixophone.exe
# After a fresh clone run ./tools/setup.sh (initialises the submodules).

cmake_minimum_required(VERSION 3.22)

project(LePixophone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

foreach(dep JUCE sokol sokol-tools-bin)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/${dep}/README.md")
        message(FATAL_ERROR
            "ThirdParty/${dep} is missing — run ./tools/setup.sh "
            "(or: git submodule update --init --recursive)")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# GPU backend per platform (D4). sokol-shdc emits only the shader language of
# the backend we build: Metal bytecode can only be produced on a Mac anyway.
# P0 implements the Windows surfaces; macOS and Linux land in P5.
# ---------------------------------------------------------------------------
if(WIN32)
    set(PIXO_SOKOL_BACKEND SOKOL_D3D11)
    set(PIXO_SHADER_SLANG  hlsl5)
elseif(APPLE)
    set(PIXO_SOKOL_BACKEND SOKOL_METAL)
    set(PIXO_SHADER_SLANG  metal_macos)
else()
    set(PIXO_SOKOL_BACKEND SOKOL_GLCORE)
    set(PIXO_SHADER_SLANG  glsl430)
endif()

if(NOT WIN32)
    message(FATAL_ERROR
        "P0 only implements the Windows (D3D11) GPU surfaces. macOS (Metal) and "
        "Linux (GL core) surfaces arrive in phase P5 — see Documentation/PIXOPHONE-PLAN.md.")
endif()

# ---------------------------------------------------------------------------
# Dependencies (pinned submodules, D3/D4/D18)
# ---------------------------------------------------------------------------
add_subdirectory(ThirdParty/JUCE "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

include("${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/sokol-tools-bin/sokol_shaders.cmake")

# sokol_gfx: header-only; Source/Render/SokolImpl.cpp is its single
# implementation unit. Built as its own library so the app's JUCE warning
# flags don't apply to third-party code.
add_library(pixo_sokol STATIC Source/Render/SokolImpl.cpp)
target_include_directories(pixo_sokol PUBLIC ThirdParty/sokol)
target_compile_definitions(pixo_sokol PUBLIC ${PIXO_SOKOL_BACKEND})
if(WIN32)
    target_link_libraries(pixo_sokol PUBLIC d3d11 dxgi)
endif()

# ---------------------------------------------------------------------------
# Shaders — compiled by sokol-shdc at build time into C headers under
# <build>/compile_shaders/Source/..., included as "Render/shaders/<name>.glsl.h".
# ---------------------------------------------------------------------------
set(PIXO_SHADERS
    Source/Render/shaders/testpattern.glsl)

set(PIXO_SHADER_HEADERS)
foreach(shader ${PIXO_SHADERS})
    sokol_shader("${shader}" "${PIXO_SHADER_SLANG}")
    list(APPEND PIXO_SHADER_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/${shader}.h")
endforeach()

# ---------------------------------------------------------------------------
# The application
# ---------------------------------------------------------------------------
juce_add_gui_app(LePixophone
    PRODUCT_NAME                "Le Pixophone"
    VERSION                     "${PROJECT_VERSION}"
    COMPANY_NAME                "Pix et Bel"
    BUNDLE_ID                   "org.pixetbel.pixophone"
    CAMERA_PERMISSION_ENABLED   TRUE
    CAMERA_PERMISSION_TEXT      "Le Pixophone films the light table with a camera.")

target_sources(LePixophone PRIVATE
    Source/Main.cpp
    Source/App/ControlWindow.cpp
    Source/App/OutputWindow.cpp
    Source/Render/GpuSurfaceComponent.cpp
    Source/Render/RenderEngine.cpp
    ${PIXO_SHADER_HEADERS})

if(WIN32)
    target_sources(LePixophone PRIVATE
        Source/Render/Win/D3D11Backend.cpp
        Source/Render/Win/NativeSurfaceWindow.cpp)
endif()

target_include_directories(LePixophone PRIVATE
    Source
    "${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/Source")

target_compile_definitions(LePixophone PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LePixophone,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LePixophone,JUCE_VERSION>"
    JUCE_USE_CURL=0)

target_link_libraries(LePixophone
    PRIVATE
        pixo_sokol
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    # Parallel compilation; sources are UTF-8 without BOM.
    target_compile_options(LePixophone PRIVATE /MP /utf-8)
endif()
