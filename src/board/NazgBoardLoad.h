// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// BoardLoad - what opening a board reads: which protocol it speaks, which definition draws
// it, its keymap, and what it reports about itself. The protocol work of connecting, apart
// from the frame loop so a scripted channel can test it: Main.cpp opens the device, hands
// the protocol over, and shows what comes back.
//
// A Vial board describes itself. A VIA board is drawn by one of its candidates (library/
// NazgDefinitionChoice.h): the user definitions the caller read for its VID:PID, the
// community bundle's and VIA's official one. When the remembered choice does not settle
// which, the load stops there, with no keyboard, and the picker asks.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/via/NazgCommunityBundle.h"
#include "adapters/via/NazgViaBundle.h"
#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "library/NazgDefinitionChoice.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    // The bundles a VIA board's definition may come from, beside the user definitions; either
    // may be missing. Owned by the caller, and outliving the load.
    struct BundledDefinitions
    {
        const via::DefinitionBundle*          official  = nullptr;
        const via::CommunityDefinitionBundle* community = nullptr;
    };

    struct BoardLoad
    {
        // The board, read; none when a VIA board's definition is still to be picked.
        std::optional<Keyboard> keyboard;

        bool                              isVial = false;
        std::optional<vial::UnlockStatus> lock;   // a Vial board's, as it loaded

        // A VIA board's candidates, ranked as the picker shows them, and the one drawing it. The
        // bundled ones are also kept apart, to rebuild the list when the library changes.
        std::vector<DefinitionCandidate> candidates;
        std::vector<DefinitionCandidate> bundled;
        std::optional<size_t>            used;   // into candidates

        // The definition drawing the board as its source has it, when that is the board itself
        // or a bundle -- for "Export definition...". Empty for a user definition, whose stored
        // copy the library has.
        std::vector<uint8_t> exportable;

        // Community files for this VID:PID that did not parse, each with why -- left out.
        std::vector<std::string> skipped;
    };

    // Throws HidTransportError or ProtocolError when the board does not answer as it should, and
    // std::runtime_error for a Vial board waiting for an unlock that was started and never
    // finished. `chosen` is the remembered choice, if any.
    [[nodiscard]] Task<BoardLoad> LoadBoard(vial::Protocol&                  protocol,
                                            DeviceIdentity                   identity,
                                            std::vector<DefinitionCandidate> userCandidates,
                                            std::optional<DefinitionRef>     chosen,
                                            BundledDefinitions               bundles);
}
