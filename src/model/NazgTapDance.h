// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// TapDance - one key doing up to four things, told apart by how it is pressed (ui-design.md, "The
// Tap Dance section"): a tap, a hold, a double tap, a tap then a hold, and the tapping term that
// separates them. Vial's, one slot of its table; protocol-neutral here -- the section turns it
// into the board's entry and back.
//
// What the firmware does with it is vial-qmk's (quantum/vial.c, read 2026-10-10), and this says it
// for the panel: what an empty action does instead, and when the tap is sent.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "model/NazgKeycode.h"

namespace nazg
{
    enum class DanceAction : uint8_t
    {
        Tap,
        Hold,
        DoubleTap,
        TapHold,   // tapped, then pressed again within the term and held
    };

    inline constexpr DanceAction c_DanceActions[] = { DanceAction::Tap, DanceAction::Hold, DanceAction::DoubleTap,
                                                      DanceAction::TapHold };

    struct TapDance
    {
        // By DanceAction; none where the board stores 0 (KC_NO).
        std::array<std::optional<Keycode>, 4> actions;
        uint16_t                              tappingTerm = 200;

        [[nodiscard]] std::optional<Keycode>&       operator[](DanceAction action) { return actions[static_cast<size_t>(action)]; }
        [[nodiscard]] const std::optional<Keycode>& operator[](DanceAction action) const
        {
            return actions[static_cast<size_t>(action)];
        }

        [[nodiscard]] bool IsEmpty() const;

        bool operator==(const TapDance&) const = default;
    };

    // What the firmware plays for an empty action (on_dance_finished): `tappedFirst` tapped, if
    // any, then `held` held. An empty hold holds the tap; an empty double tap taps the tap, then
    // holds it -- the tap twice; an empty tap then hold taps the tap, then holds the hold (the tap
    // when there is none), or holds the hold alone when there is no tap. None when the action is
    // set, is the tap -- an empty tap sends nothing -- or nothing would be played.
    struct DanceFallback
    {
        std::optional<Keycode> tappedFirst;
        Keycode                held;
    };
    [[nodiscard]] std::optional<DanceFallback> FallbackOf(const TapDance& dance, DanceAction action);

    // When a quick press is sent (process_record_vial, tap_dance_task).
    enum class TapTiming : uint8_t
    {
        Empty,        // no action at all
        NoTap,        // a quick press sends nothing
        AtRelease,    // a tap and a hold, nothing else: the tap goes as the key is released
        Waits,        // a double tap or a tap then hold: the tap waits out the term from the press
        OnlyTap,      // a tap alone, which still waits out the term -- a plain key does it at once
    };
    [[nodiscard]] TapTiming TimingOf(const TapDance& dance);
}
