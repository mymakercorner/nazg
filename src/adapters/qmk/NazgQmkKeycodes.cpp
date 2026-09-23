// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgQmkKeycodes.h"

#include <algorithm>

namespace nazg
{
    const QmkKeycode* FindQmkKeycode(uint16_t value, QmkKeycodeVersion version) noexcept
    {
        const std::span<const QmkKeycode> table = QmkKeycodeTable();

        // Sorted by value, so the rows for one value are adjacent -- at most two, when
        // QMK moved or reused it -- and only they need the version check.
        auto row = std::lower_bound(table.begin(), table.end(), value,
                                    [](const QmkKeycode& entry, uint16_t wanted) { return entry.value < wanted; });

        for (; row != table.end() && row->value == value; ++row)
            if (row->ExistsIn(version))
                return &*row;

        return nullptr;
    }

    // Lookup by name is for writing a keycode -- a typed name, an imported keymap -- not
    // for every frame, so a scan of the ~1000 rows is fine.
    const QmkKeycode* FindQmkKeycodeByName(std::string_view name, QmkKeycodeVersion version) noexcept
    {
        for (const QmkKeycode& row : QmkKeycodeTable())
            if (row.ExistsIn(version) && name == row.name)
                return &row;

        return nullptr;
    }

    std::optional<QmkKeycodeVersion> QmkKeycodeVersionFromBcd(uint32_t bcd) noexcept
    {
        // 0xMMmmpppp: major, minor, patch, each binary-coded decimal -- so 0.0.10 will
        // arrive as 0x00000010, not 0x0000000A. Every version so far is 0.0.x.
        if ((bcd & 0xFFFF0000u) != 0)
            return std::nullopt;

        uint32_t patch = 0;
        for (int shift = 12; shift >= 0; shift -= 4)
        {
            const uint32_t digit = (bcd >> shift) & 0xFu;
            if (digit > 9)
                return std::nullopt;

            patch = patch * 10 + digit;
        }

        if (patch < static_cast<uint32_t>(QmkKeycodeVersion::V0_0_1) ||
            patch > static_cast<uint32_t>(c_LatestQmkKeycodeVersion))
            return std::nullopt;

        return static_cast<QmkKeycodeVersion>(patch);
    }

    const char* QmkKeycodeVersionName(QmkKeycodeVersion version) noexcept
    {
        switch (version)
        {
        case QmkKeycodeVersion::V0_0_1: return "0.0.1";
        case QmkKeycodeVersion::V0_0_2: return "0.0.2";
        case QmkKeycodeVersion::V0_0_3: return "0.0.3";
        case QmkKeycodeVersion::V0_0_4: return "0.0.4";
        case QmkKeycodeVersion::V0_0_5: return "0.0.5";
        case QmkKeycodeVersion::V0_0_6: return "0.0.6";
        case QmkKeycodeVersion::V0_0_7: return "0.0.7";
        case QmkKeycodeVersion::V0_0_8: return "0.0.8";
        case QmkKeycodeVersion::V0_0_9: return "0.0.9";
        case QmkKeycodeVersion::Never:  break;
        }

        return "?";
    }
}
