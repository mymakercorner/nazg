#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generates the web build in build_Web/ -- WebAssembly through Emscripten, Debug and Release
# side by side as the other builds have them (build-and-release.md, "The web build"):
#     cmake --build build_Web --config Release
#     ctest --test-dir build_Web -C Release --output-on-failure    (the tests, run by Node)
# The page is build_Web/Release/nazg.html, served over HTTP -- not opened as a file:
#     python3 -m http.server -d build_Web/Release 8080
#
# Run in WSL's Ubuntu (or any Linux, or macOS) with emsdk installed and activated, at the
# version below:
#     git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
#     ~/emsdk/emsdk install 6.0.13 && ~/emsdk/emsdk activate 6.0.13
#     . ~/emsdk/emsdk_env.sh
#
# One script and one build directory per toolchain, so they coexist without
# clobbering each other.

NAZG_EMSCRIPTEN_VERSION=6.0.13

if [ ! -f external/SDL/CMakeLists.txt ]; then
    echo
    echo "Submodules are not initialised. Run:"
    echo "    git submodule update --init --recursive"
    echo
    exit 1
fi

if ! command -v emcmake >/dev/null 2>&1; then
    echo
    echo "Emscripten is not on the PATH. Activate emsdk first:"
    echo "    . ~/emsdk/emsdk_env.sh"
    echo
    exit 1
fi

found=$(emcc --version | head -n 1 | sed -n 's/.*) \([0-9][0-9.]*\).*/\1/p')
if [ "$found" != "$NAZG_EMSCRIPTEN_VERSION" ]; then
    echo "Note: Emscripten $found, not $NAZG_EMSCRIPTEN_VERSION, the version this build is known with."
fi

if ! emcmake cmake -S . -B build_Web -G "Ninja Multi-Config"; then
    echo
    echo "CMake configuration failed."
    exit 1
fi

echo
echo "Generated: build_Web/"
