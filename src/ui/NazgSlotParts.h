// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SlotParts - what the sections editing a board's slots share: Macros, Tap Dance and Combos, each
// its slots in the strip, read when first shown, written by Save and undone by Revert. Shared once
// there were three (Rico, 2026-10-10, agreed to wait for the third): the line saying whether the
// slots could be read, the slot line's write state and its Save / Revert, the names of the
// changed slots, and the unlock keys outlined on a locked board.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "ui/NazgBoardDescription.h"

namespace nazg
{
    // "M3", "M3 and M5", "M3, M5 and M7".
    [[nodiscard]] std::string JoinNames(const std::vector<std::string>& names);

    // While the slots are not read: "Reading the <what>..." while `busy`, else `failure` and a Try
    // again button. True when Try again was clicked.
    [[nodiscard]] bool DrawReading(const char* what, bool busy, const std::string& failure);

    // On the slot line, after its own parts: what was written -- "Writing...", "Not written yet --
    // <changed> changed" when `changed` names something, the last write's `message`, or that Save
    // writes.
    void DrawWriteState(bool busy, const std::string& changed, const std::string& message, bool isWarning);

    // Save and Revert, at the line's right end. Which was clicked.
    enum class SlotWrite
    {
        None,
        Save,
        Revert,
    };
    [[nodiscard]] SlotWrite DrawSaveRevert(bool canSave, bool canRevert);

    // A locked board's unlock keys, outlined in the warning's colour: the way to Boot.
    void MarkUnlockKeys(BoardDescription& board, const std::optional<VialUnlockStatus>& lock);
}
