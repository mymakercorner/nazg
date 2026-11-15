// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyHealthWords.h"

namespace nazg
{
    const char* WordOf(KeyVerdict verdict)
    {
        switch (verdict)
        {
        case KeyVerdict::Failing: return "failing";
        case KeyVerdict::AtRisk:  return "at risk";
        case KeyVerdict::ToCheck: return "to check";
        case KeyVerdict::ToDo:    return "to do";
        case KeyVerdict::Good:    break;
        }
        return "good";
    }

    std::string SentenceOf(KeyReason reason, KeyVerdict verdict, const std::string& other)
    {
        switch (reason)
        {
        case KeyReason::TypesByItself:     return "Types by itself: seen pressed without being touched";
        case KeyReason::NeverReaches:      return "Does not work: its presses never reach its threshold";
        case KeyReason::TypesWhenPressed:  return "Types by itself when " + other + " is pressed: goes past its threshold";
        case KeyReason::NearsWhenPressed:  return "May type by itself when " + other + " is pressed: comes close to its threshold";
        case KeyReason::WentPastUntouched: return "May type by itself: went past its threshold untouched";
        case KeyReason::RestsClose:        return "May type by itself: rests close to its threshold";
        case KeyReason::WandersAtRest:     return "May type by itself: its level wanders at rest";
        case KeyReason::OnlyJustPast:      return "May miss presses: even its firmest press is only just past its threshold";
        case KeyReason::NotPressedYet:     return "Not pressed yet";
        case KeyReason::PressAgain:        return "Press it again, firmly: not past its threshold yet";
        case KeyReason::HoldToMeasure:     return "Registered: hold it a moment to measure it";
        case KeyReason::TopOfRange:
            return verdict == KeyVerdict::ToCheck ? "To check: reads at the top of the range, its real level may be higher"
                                                  : "reads at the top of the range";
        }
        return {};
    }

    std::string PluralOf(KeyReason reason)
    {
        switch (reason)
        {
        case KeyReason::TypesByItself:     return "were seen pressed without being touched -- they type by themselves";
        case KeyReason::NeverReaches:      return "never reach their threshold -- they do not work";
        case KeyReason::WentPastUntouched: return "went past their threshold untouched -- they may type by themselves";
        case KeyReason::RestsClose:        return "rest close to their threshold -- they may type by themselves";
        case KeyReason::WandersAtRest:     return "wander at rest -- they may type by themselves";
        case KeyReason::OnlyJustPast:      return "only just get past their threshold -- they may miss presses";
        case KeyReason::TypesWhenPressed:  return "go past their threshold when a neighbour is pressed -- they type by themselves";
        case KeyReason::NearsWhenPressed:  return "come close to their threshold when a neighbour is pressed -- they may type by themselves";
        default:                           break;
        }
        return SentenceOf(reason, KeyVerdict::AtRisk);
    }

    std::string ReasonsOf(const JudgedKey& judged, const char* separator, const std::string& movedBy)
    {
        std::string text;
        for (KeyReason reason : judged.reasons)
            text += (text.empty() ? "" : separator) + SentenceOf(reason, judged.verdict, movedBy);
        return text;
    }
}
