// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// QmkKeycodes - what a 16-bit keycode means on a QMK board, for each keycode version.
//
// Shared by the VIA and Vial branches: both store QMK keycodes, and only how the version
// is discovered differs (docs/research_material/keycodes.md, "Picking the dictionary").
//
// A value alone is not enough to name a keycode. QMK has reused values three times --
// 0x7110 is MI_C before keycode spec 0.0.2 and MI_Cs1 after -- so every lookup here takes
// the version as well. The same goes the other way: OU_AUTO is 0x7C20 up to 0.0.5 and
// 0x7780 from 0.0.6.
//
// Fixed keycodes only. Parameterised ranges -- MO(n), LT(layer, kc), LCTL(kc) and the
// like -- are arithmetic, not table entries, and are decoded elsewhere.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace nazg::qmk
{
    // QMK's keycode spec versions, in order. The numbering is Nazg's own: QMK reports
    // 0.0.9 as QMK_KEYCODES_VERSION_BCD 0x00000009, converted by qmk::KeycodeVersionFromBcd.
    //
    // The two Legacy values are the numbering before QMK's renumbering (#18643, 2022-11).
    // Its fixed keycodes are the ones VIA pinned with static asserts, which never moved
    // while that header existed; the two differ only in how TO(n) is encoded, because QMK
    // dropped TO's ON_PRESS bit (#17989, 2022-08) just after VIA protocol 10 arrived.
    // Keycodes VIA never pinned (haptics, for instance) moved between builds back then, so
    // they are not in the table and decode as unknown values.
    enum class KeycodeVersion : uint8_t
    {
        Legacy = 1,    // VIA protocol <= 9, Vial <= 5: TO(n) = 0x5010 | n
        LegacyVia10,   // VIA protocol 10:               TO(n) = 0x5000 | n

        V0_0_1,
        V0_0_2,
        V0_0_3,
        V0_0_4,
        V0_0_5,
        V0_0_6,
        V0_0_7,
        V0_0_8,
        V0_0_9,

        Never = 0xFF,   // as a removedIn: the keycode still exists in the latest version
    };

    inline constexpr KeycodeVersion c_LatestKeycodeVersion = KeycodeVersion::V0_0_9;

    // Before the renumbering: different range bases, so the codec takes another path.
    [[nodiscard]] constexpr bool IsLegacy(KeycodeVersion version) noexcept
    {
        return version < KeycodeVersion::V0_0_1;
    }

    // One row of the table: a keycode at one value, over the versions it held that value.
    struct KeycodeEntry
    {
        uint16_t       value;
        KeycodeVersion since;       // first version with this value
        KeycodeVersion removedIn;   // first version WITHOUT it; Never if still current
        const char*    name;        // short keymap name from the newest version: KC_ENT,
                                    // UG_TOGG. Also the identity -- it survives renames
        const char*    group;       // QMK's grouping: "basic", "quantum", "midi", ...
        const char*    label;       // QMK's display label in ASCII; may be empty

        [[nodiscard]] constexpr bool ExistsIn(KeycodeVersion version) const noexcept
        {
            return version >= since && version < removedIn;
        }
    };

    // The whole table, every version at once, sorted by value then by version. Filter
    // with ExistsIn() to list what one version offers -- that is what a picker shows.
    [[nodiscard]] std::span<const KeycodeEntry> KeycodeTable() noexcept;

    // The keycode a board on this version stores as this value. nullptr when the value
    // is not a fixed keycode there: part of a parameterised range, or simply unassigned.
    [[nodiscard]] const KeycodeEntry* FindKeycode(uint16_t value, KeycodeVersion version) noexcept;

    // The reverse direction, for writing: the keycode with this name as it exists in this
    // version. nullptr means this firmware cannot store it -- the keycode is newer than
    // the board (QK_LLCK before 0.0.6) or was removed.
    [[nodiscard]] const KeycodeEntry* FindKeycodeByName(std::string_view name, KeycodeVersion version) noexcept;

    // 0x00000009 -> V0_0_9. nullopt for a version this build has no table for, including
    // anything newer than qmk::c_LatestKeycodeVersion.
    [[nodiscard]] std::optional<KeycodeVersion> KeycodeVersionFromBcd(uint32_t bcd) noexcept;

    // "0.0.9", or "legacy" / "legacy (VIA 10)", for display and logs.
    [[nodiscard]] const char* KeycodeVersionName(KeycodeVersion version) noexcept;
}
