// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgIcons.h"

namespace nazg
{
    std::string Utf8Of(Icon icon)
    {
        // Tabler's code points are all in the Private Use Area of the first plane: three bytes.
        const auto point = static_cast<char32_t>(icon);
        if (point == 0)
            return {};

        return { static_cast<char>(0xE0 | (point >> 12)), static_cast<char>(0x80 | ((point >> 6) & 0x3F)),
                 static_cast<char>(0x80 | (point & 0x3F)) };
    }
}
