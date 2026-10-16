// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Files - paths as UTF-8, and a whole file read at once: what the app, the definition
// library, the validator and the fonts each used to write out for themselves.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nazg
{
    // A UTF-8 path -- as SDL's dialogs and Nazg's settings hold them -- as a path, on every
    // platform: from a plain char string, Windows would read it in the ANSI code page.
    [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view path);

    // And back, for messages and files: in C++20 path::u8string() is a std::u8string.
    [[nodiscard]] std::string Utf8FromPath(const std::filesystem::path& path);

    // Every byte of a file; nullopt when it cannot be opened.
    [[nodiscard]] std::optional<std::vector<uint8_t>> ReadWholeFile(const std::filesystem::path& path);
}
