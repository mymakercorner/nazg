# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

cmake_minimum_required(VERSION 3.21.0)

project(
  nazg
  VERSION 0.1.0
  LANGUAGES C CXX)

# C++20 for coroutines: protocol sequences are hundreds of async round trips, and
# co_await keeps them readable. Coroutines need no threads, so they port to Emscripten
# where std::thread does not. std::variant (capability model) comes along for free.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Solution Explorer tidiness: group CMake's generated targets (ALL_BUILD, ZERO_CHECK)
# and the third-party dependencies into folders instead of one flat list.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

# --- Dependencies -------------------------------------------------------------------
# All are git submodules under external/, pinned to exact commits. See CLAUDE.md.

# Trim third-party extras we never build.
set(SDL_TEST_LIBRARY        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES            OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL             OFF CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_HIDTEST    OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests         OFF CACHE BOOL "" FORCE)

# CMAKE_FOLDER is inherited by targets created below, placing every dependency
# under an "external" folder in the IDE.
set(CMAKE_FOLDER external)

add_subdirectory(external/SDL)
add_subdirectory(external/hidapi)
add_subdirectory(external/json)
add_subdirectory(external/minlzma/minlzlib)

unset(CMAKE_FOLDER)

# Dear ImGui has no CMake build of its own; compile the core plus only the two
# backends we use. Renderer backend is SDL_GPU -- see CLAUDE.md for why, and for the
# fallback to imgui_impl_opengl3 if it causes trouble.
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_CORE_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
)

set(IMGUI_BACKEND_SOURCES
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
)

set(IMGUI_SOURCES ${IMGUI_CORE_SOURCES} ${IMGUI_BACKEND_SOURCES})

# --- Application --------------------------------------------------------------------

set(NAZG_SOURCES
  src/Main.cpp
  src/async/NazgTask.h
  src/transport/NazgHidTransport.h
  src/transport/NazgHidTransport.cpp
)

add_executable(nazg
  ${NAZG_SOURCES}
  ${IMGUI_SOURCES}
)

# IDE-only: file the vendored ImGui sources under their own group so they do not
# clutter the project's own sources. Forward slashes nest the groups.
# Mirror the src/ directory structure in the IDE.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${NAZG_SOURCES})

source_group("Source Files/ImGui"          FILES ${IMGUI_CORE_SOURCES})
source_group("Source Files/ImGui/Backends" FILES ${IMGUI_BACKEND_SOURCES})

target_include_directories(nazg PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
  ${CMAKE_CURRENT_SOURCE_DIR}/external/minlzma
)

# hidapi::hidapi is a portable alias resolving to the right per-platform target
# (winapi / darwin / hidraw), so no platform branching is needed here.
target_link_libraries(nazg PRIVATE
  SDL3::SDL3
  hidapi::hidapi
  nlohmann_json::nlohmann_json
  minlzlib
)

# --- Tests --------------------------------------------------------------------------
# CTest, not a test framework: CMake ships it, so there is no submodule and no
# framework choice locked in. Run with:
#   ctest --test-dir build_VS2022 -C Debug --output-on-failure

option(NAZG_BUILD_TESTS "Build the test executables" ON)

if(NAZG_BUILD_TESTS)
    enable_testing()

    # Compiles the sources it needs directly. When the test list grows, pull the
    # shared sources into a nazg_core static library and link both against that.
    add_executable(nazg_transport_shutdown_test
      tests/TransportShutdownTest.cpp
      src/transport/NazgHidTransport.cpp
    )

    target_include_directories(nazg_transport_shutdown_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(nazg_transport_shutdown_test PRIVATE SDL3::SDL3 hidapi::hidapi)
    set_property(TARGET nazg_transport_shutdown_test PROPERTY FOLDER tests)

    add_test(NAME transport_shutdown COMMAND nazg_transport_shutdown_test)
endif()

# Make nazg the solution's startup project so F5 runs the app, not ALL_BUILD.
# Must be set on the directory holding the top-level project().
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT nazg)

# Build as a GUI-subsystem app on Windows so no console window appears (notably when
# launching from Visual Studio). Diagnostics still reach you: SDL_Log writes via
# OutputDebugString -- visible in the VS Output pane -- and SDL also attaches to a parent
# console when the app is started from a terminal. SDL3's SDL_main.h supplies WinMain.
if(WIN32)
    set_target_properties(nazg PROPERTIES WIN32_EXECUTABLE ON)
endif()

# For universal macOS release builds, configure with:
#   -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
