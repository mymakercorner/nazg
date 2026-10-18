#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generates a Ninja build in build_Linux/, with Debug and Release side by side as Visual
# Studio has them, so the same commands work:
#     cmake --build build_Linux --config Debug
#     ctest --test-dir build_Linux -C Debug --output-on-failure
#
# One script and one build directory per toolchain, so they coexist without
# clobbering each other. To support another IDE or platform, add a sibling
# script writing to its own build_<name>/ directory.

if [ ! -f external/SDL/CMakeLists.txt ]; then
    echo
    echo "Submodules are not initialised. Run:"
    echo "    git submodule update --init --recursive"
    echo
    exit 1
fi

# hidapi's hidraw backend only: the libusb one would detach the keyboard's kernel driver
# (build-and-release.md, "Linux").
if ! cmake -S . -B build_Linux -G "Ninja Multi-Config" -DHIDAPI_WITH_LIBUSB=OFF; then
    echo
    echo "CMake configuration failed."
    exit 1
fi

echo
echo "Generated: build_Linux/"
