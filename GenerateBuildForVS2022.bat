@echo off
REM SPDX-License-Identifier: GPL-3.0-or-later
REM
REM Generates a Visual Studio 2022 solution in build_VS2022\
REM
REM One script and one build directory per toolchain, so they coexist without
REM clobbering each other. To support another IDE or platform, add a sibling
REM script writing to its own build_<name>\ directory.

if not exist "external\SDL\CMakeLists.txt" (
    echo.
    echo Submodules are not initialised. Run:
    echo     git submodule update --init --recursive
    echo.
    exit /b 1
)

cmake -S . -B build_VS2022 -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo.
    echo CMake configuration failed.
    exit /b 1
)

echo.
echo Generated: build_VS2022\nazg.sln
