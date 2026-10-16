// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ProtocolError - what every adapter throws when bytes do not mean what they should: a reply
// the protocol did not ask for, a definition that does not parse, a stream that does not
// inflate. Shared by all protocols, so it lives in nazg itself rather than in one of them.

#pragma once

#include <stdexcept>
#include <string>

namespace nazg
{
    // Raised when the device answers something the protocol did not ask for, including
    // the 0xFF "unhandled" marker. Deliberately a sibling of HidTransportError rather
    // than a subclass: a caller that only cares "the operation failed" catches
    // std::runtime_error, and one that cares why can tell a refusal from a dead cable.
    class ProtocolError : public std::runtime_error
    {
    public:
        explicit ProtocolError(const std::string& message) : std::runtime_error(message) {}
    };
}
