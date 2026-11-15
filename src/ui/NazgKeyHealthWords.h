// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyHealthWords - what Signal levels says of a key's health, in words: the Diagnostics section's
// and its saved report's, so the two always say the same (ui-design.md, "Plugins"). The words lead
// with the effect and carry no numbers (Rico, 2026-10-17): those are in the trace and the tooltip.
//
// No ImGui.

#pragma once

#include <string>

#include "model/NazgKeyHealth.h"

namespace nazg
{
    [[nodiscard]] const char* WordOf(KeyVerdict verdict);   // "failing", "at risk", ...

    // One reason. `other`: the key whose press moves it, for the two reasons that name one.
    [[nodiscard]] std::string SentenceOf(KeyReason reason, KeyVerdict verdict, const std::string& other = {});

    // The same problem shared by many keys, said once of the keyboard.
    [[nodiscard]] std::string PluralOf(KeyReason reason);

    // A key's reasons, joined. `movedBy`: the name of the key in judged.movedBy, when there is one.
    [[nodiscard]] std::string ReasonsOf(const JudgedKey& judged, const char* separator, const std::string& movedBy);
}
