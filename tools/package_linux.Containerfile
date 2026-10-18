# SPDX-License-Identifier: GPL-3.0-or-later
#
# The machine tools/package_linux.py builds Nazg's Linux release in (build-and-release.md, "Linux:
# one build for most distributions"): AlmaLinux 8, glibc 2.28 -- the oldest glibc the binary asks
# for, so it starts on Debian 10+, Ubuntu 20.04+, RHEL / Rocky 8+, Fedora and Arch. The image Python
# wheels are built in, pinned like the submodules: the tag says when, the digest is what is used.
FROM quay.io/pypa/manylinux_2_28_x86_64:2026.10.16-1@sha256:1e7b8460ad8fb3fb0fbe1cd818845159ac896b4fd17452d56e892c291f3b97a8

# GCC 13, the compiler Nazg's Linux build was first proven with. The development headers of what SDL
# loads when it runs -- X11, Wayland, Vulkan, D-Bus (the file dialogs' portal), libudev -- compile
# its backends in; the binary does not depend on them. No audio: Nazg plays none. libudev is also
# hidapi's hidraw backend, the one library Nazg links besides glibc.
RUN dnf install -y \
        gcc-toolset-13-gcc-c++ gcc-toolset-13-binutils ninja-build git \
        libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel \
        libXi-devel libXScrnSaver-devel libXtst-devel libxkbcommon-devel \
        wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGL-devel \
        vulkan-headers dbus-devel systemd-devel cairo-devel pango-devel \
    && dnf clean all

# libdecor, which AlmaLinux 8 lacks: the window's title bar and frame on a Wayland desktop that
# draws none for its programs (GNOME). SDL loads the system's own when it runs; this copy is only
# for SDL to compile its support against. Pinned by commit, as the submodules are.
RUN pipx install meson==1.6.1 \
    && git clone --branch 0.2.5 https://gitlab.freedesktop.org/libdecor/libdecor.git /tmp/libdecor \
    && test "$(git -C /tmp/libdecor rev-parse HEAD)" = 149c6f0b05663aaa69fdf7f94be2483776d1a311 \
    && meson setup /tmp/libdecor/build /tmp/libdecor --prefix=/usr --libdir=lib64 \
           -Ddemo=false -Dgtk=disabled \
    && ninja -C /tmp/libdecor/build install \
    && rm -rf /tmp/libdecor
