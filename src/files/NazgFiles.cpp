// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgFiles.h"

#include <fstream>
#include <iterator>

namespace nazg
{
    std::filesystem::path PathFromUtf8(std::string_view path)
    {
        return std::filesystem::path(std::u8string(path.begin(), path.end()));
    }

    std::string Utf8FromPath(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::optional<std::vector<uint8_t>> ReadWholeFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return std::nullopt;
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
}
