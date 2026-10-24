# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
#
# The web build's page asks for its script with the version, as Module.locateFile() asks for the
# rest (resources/package/web/shell.html): a browser then never runs a release's page with the one
# before's script from its cache. Emscripten writes that script tag itself, so the version goes in
# after linking -- and the build stops if Emscripten ever writes the tag otherwise.
#
#   cmake -DPAGE=<nazg.html> -DSCRIPT=<nazg.js> -DVERSION=<NAZG_VERSION> -P VersionWebPage.cmake

file(READ "${PAGE}" text)
string(FIND "${text}" "src=${SCRIPT}>" at)
if(at EQUAL -1)
    string(FIND "${text}" "src=\"${SCRIPT}?v=${VERSION}\">" done)
    if(done EQUAL -1)
        message(FATAL_ERROR "${PAGE}: no <script src=${SCRIPT}> to give the version -- has Emscripten changed how it writes it?")
    endif()
    return()   # already done: a relink without a change to the page
endif()

string(REPLACE "src=${SCRIPT}>" "src=\"${SCRIPT}?v=${VERSION}\">" text "${text}")
file(WRITE "${PAGE}" "${text}")
