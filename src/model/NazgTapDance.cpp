// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgTapDance.h"

namespace nazg
{
    bool TapDance::IsEmpty() const
    {
        for (const std::optional<Keycode>& action : actions)
            if (action)
                return false;
        return true;
    }

    std::optional<DanceFallback> FallbackOf(const TapDance& dance, DanceAction action)
    {
        const std::optional<Keycode>& tap  = dance[DanceAction::Tap];
        const std::optional<Keycode>& hold = dance[DanceAction::Hold];
        if (dance[action] || action == DanceAction::Tap)
            return std::nullopt;

        switch (action)
        {
        case DanceAction::Hold:
            if (tap)
                return DanceFallback{ std::nullopt, *tap };
            break;
        case DanceAction::DoubleTap:
            if (tap)
                return DanceFallback{ tap, *tap };
            break;
        default:
            if (tap)
                return DanceFallback{ tap, hold ? *hold : *tap };
            if (hold)
                return DanceFallback{ std::nullopt, *hold };
            break;
        }
        return std::nullopt;
    }

    TapTiming TimingOf(const TapDance& dance)
    {
        if (dance.IsEmpty())
            return TapTiming::Empty;
        if (!dance[DanceAction::Tap])
            return TapTiming::NoTap;
        if (dance[DanceAction::DoubleTap] || dance[DanceAction::TapHold])
            return TapTiming::Waits;
        return dance[DanceAction::Hold] ? TapTiming::AtRelease : TapTiming::OnlyTap;
    }
}
