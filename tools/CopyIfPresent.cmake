# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
#
# Copy SOURCE into the DESTINATION directory if it exists; do nothing otherwise.
# `cmake -E copy_if_different` fails on a missing file, and an optional file -- the bundle
# of VIA's definitions, which a build may not have -- must not fail the build.
#
#   cmake -DSOURCE=<file> -DDESTINATION=<directory> -P CopyIfPresent.cmake

if(EXISTS "${SOURCE}")
    file(COPY "${SOURCE}" DESTINATION "${DESTINATION}")
endif()
