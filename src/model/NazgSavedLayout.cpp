// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSavedLayout.h"

#include <algorithm>

namespace nazg
{
    size_t SavedLayout::Rows() const noexcept
    {
        return keymap.empty() ? 0 : keymap.front().size();
    }

    size_t SavedLayout::Columns() const noexcept
    {
        size_t columns = 0;
        if (!keymap.empty())
            for (const auto& row : keymap.front())
                columns = std::max(columns, row.size());
        return columns;
    }
}
