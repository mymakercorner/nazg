// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// What a tap dance does, as vial-qmk's quantum/vial.c has it: the fallbacks of empty actions, and
// when a quick press is sent.

#include <cstdio>

#include "model/NazgTapDance.h"
#include "TestSupport.h"

using nazg::DanceAction;
using nazg::Keycode;
using nazg::LayerKey;
using nazg::LayerOp;
using nazg::NamedKey;
using nazg::TapDance;
using nazg::TapTiming;

namespace
{
    const Keycode c_Esc  = NamedKey{ "KC_ESC" };
    const Keycode c_Caps = NamedKey{ "KC_CAPS" };
    const Keycode c_L1   = LayerKey{ LayerOp::Momentary, 1 };

    TapDance Dance(std::optional<Keycode> tap, std::optional<Keycode> hold, std::optional<Keycode> twice,
                   std::optional<Keycode> tapHold)
    {
        TapDance dance;
        dance.actions = { tap, hold, twice, tapHold };
        return dance;
    }

    void TestFallbacks()
    {
        std::printf("fallbacks of empty actions\n");

        const TapDance escCaps = Dance(c_Esc, std::nullopt, c_Caps, std::nullopt);
        Check(!FallbackOf(escCaps, DanceAction::Tap), "a set action has no fallback");
        Check(!FallbackOf(escCaps, DanceAction::DoubleTap), "nor a set double tap");

        const auto hold = FallbackOf(escCaps, DanceAction::Hold);
        Check(hold && !hold->tappedFirst && hold->held == c_Esc, "an empty hold holds the tap");

        const auto tapHold = FallbackOf(escCaps, DanceAction::TapHold);
        Check(tapHold && tapHold->tappedFirst == c_Esc && tapHold->held == c_Esc,
              "an empty tap then hold, with no hold, taps the tap then holds it");

        const TapDance spaceLayer = Dance(NamedKey{ "KC_SPC" }, c_L1, std::nullopt, std::nullopt);
        const auto     twice      = FallbackOf(spaceLayer, DanceAction::DoubleTap);
        Check(twice && twice->tappedFirst == Keycode{ NamedKey{ "KC_SPC" } } && twice->held == Keycode{ NamedKey{ "KC_SPC" } },
              "an empty double tap is the tap twice");
        const auto then = FallbackOf(spaceLayer, DanceAction::TapHold);
        Check(then && then->tappedFirst == Keycode{ NamedKey{ "KC_SPC" } } && then->held == c_L1,
              "an empty tap then hold taps the tap, then holds the hold");

        const TapDance holdOnly = Dance(std::nullopt, c_L1, std::nullopt, std::nullopt);
        Check(!FallbackOf(holdOnly, DanceAction::Tap), "an empty tap sends nothing");
        Check(!FallbackOf(holdOnly, DanceAction::DoubleTap), "no tap: an empty double tap sends nothing");
        const auto alone = FallbackOf(holdOnly, DanceAction::TapHold);
        Check(alone && !alone->tappedFirst && alone->held == c_L1, "no tap: an empty tap then hold holds the hold");

        Check(!FallbackOf(TapDance{}, DanceAction::Hold), "an empty dance plays nothing");
    }

    void TestTiming()
    {
        std::printf("when the tap is sent\n");

        Check(TimingOf(TapDance{}) == TapTiming::Empty, "nothing set");
        Check(TimingOf(Dance(c_Esc, c_L1, std::nullopt, std::nullopt)) == TapTiming::AtRelease,
              "a tap and a hold alone: the tap at release");
        Check(TimingOf(Dance(c_Esc, std::nullopt, c_Caps, std::nullopt)) == TapTiming::Waits,
              "a double tap makes the tap wait");
        Check(TimingOf(Dance(c_Esc, c_L1, std::nullopt, c_Caps)) == TapTiming::Waits,
              "so does a tap then hold, a hold or not");
        Check(TimingOf(Dance(c_Esc, std::nullopt, std::nullopt, std::nullopt)) == TapTiming::OnlyTap,
              "a tap alone still waits");
        Check(TimingOf(Dance(std::nullopt, c_L1, std::nullopt, std::nullopt)) == TapTiming::NoTap,
              "no tap: a quick press sends nothing");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestFallbacks();
    TestTiming();

    return TestResult();
}
