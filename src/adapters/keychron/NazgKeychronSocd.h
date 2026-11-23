// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A Hall-effect Keychron's SOCD pairs: two keys, and what is sent while both are down
// (keychron-commands.md, "SOCD"; read and played on a K10 HE V3, 2026-11-23). A pair is apart from its
// keys' modes and distances, which it leaves untouched. The bytes are NazgKeychronProtocol.h's; here,
// what a change makes of a profile, and a pair played as the firmware plays it.
//
// Pure: no device, no window.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "adapters/keychron/NazgKeychronProtocol.h"

namespace nazg::keychron
{
    // The Launcher's four choices, whichever key wins under Absolute Priority. Type 1 -- both keys once
    // both are past 3.83 mm -- is none of them: the Launcher draws it blank, and Nazg does not write it.
    enum class SocdChoice : uint8_t
    {
        RappySnappy,        // the one pressed deeper
        LastKeyPriority,
        AbsolutePriority,   // always one of the two
        Neutral,
    };

    // None for type 1, and for a type of no published source.
    [[nodiscard]] std::optional<SocdChoice> SocdChoiceOf(SocdType type) noexcept;

    // What the choice writes: Absolute Priority keeps the winner `now` has, the first key if it has none.
    [[nodiscard]] SocdType SocdTypeFor(SocdChoice choice, SocdType now) noexcept;

    // The depth past which, both keys there, types 1 and 2 stop comparing them: a constant of the
    // firmware's (230 / 60 mm), which a board of 3.5 mm never reaches.
    inline constexpr int c_SocdBottomSixtieths = 230;

    // The positions a slot can hold: three bits of row, five of column.
    [[nodiscard]] bool FitsSocdSlot(KeyPosition key) noexcept;

    [[nodiscard]] KeyPosition FirstKeyOf(const SocdSlot& slot) noexcept;
    [[nodiscard]] KeyPosition SecondKeyOf(const SocdSlot& slot) noexcept;
    [[nodiscard]] SocdSlot    MakeSocdSlot(KeyPosition first, KeyPosition second, SocdType type) noexcept;

    // The slots holding a pair, in order; the first that holds none, or none left; the slot pairing the
    // two keys, either way round.
    [[nodiscard]] std::vector<uint8_t>   SocdSlotsInUse(const Profile& profile);
    [[nodiscard]] std::optional<uint8_t> FreeSocdSlot(const Profile& profile);
    [[nodiscard]] std::optional<uint8_t> SocdSlotPairing(const Profile& profile, KeyPosition a, KeyPosition b);

    // What the firmware does with a write, applied to the profile Nazg keeps; and whether a profile read
    // back holds it. A pair removed leaves its slot all zero.
    void               ApplySocdWrite(Profile& profile, const SocdWrite& write);
    [[nodiscard]] bool Holds(const Profile& profile, const SocdWrite& write);

    // One pair played as socd_action() plays it: which of its keys are sent, from how far each is down and
    // whether its own mode has it pressed. For the keys the computer cannot tell apart; asked of every
    // reading, in order.
    class SocdPlayer
    {
    public:
        struct Key
        {
            int  sixtieths = 0;   // its travel, 1/60 mm
            bool pressed   = false;
        };

        struct Sent
        {
            bool first  = false;
            bool second = false;

            bool operator==(const Sent&) const = default;
        };

        void Reset() noexcept { *this = {}; }
        Sent Step(SocdType type, Key first, Key second) noexcept;

    private:
        enum class Last : uint8_t
        {
            None,
            First,
            Second,
        };

        Last m_Last = Last::None;   // the firmware's state[i]: the key last held alone, or decided for
        Key  m_Before[2];
        Sent m_Sent;
    };
}
