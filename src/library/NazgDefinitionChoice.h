// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Which definition draws a VIA board, when more than one could.
//
// A quarter of QMK's keyboards share their VID:PID, and a board can have a user definition
// beside VIA's official one, so a VID:PID often finds several candidates. Nazg asks once
// and remembers the answer per device -- a choice -- keyed by what the device reports.
// On connect:
//
//   1. a choice for the device -> its definition, if it is still a candidate;
//   2. otherwise exactly one candidate -> that one;
//   3. otherwise ask, candidates ranked: user definitions before the official one, and
//      within each, a name matching the product string first.
//
// A choice is found in two steps: first one matching everything the device reports,
// release number and serial included; then one matching the VID:PID and the manufacturer
// and product strings alone. A firmware update that bumps the release number keeps the
// choice; two revisions of one board, same strings, share it until one is changed, which
// then saves an exact choice of its own.
//
// Pure: no files, no device. The choices are kept by DefinitionLibrary. The design:
// docs/research_material/via-registry.md, "Choosing a definition on connect".

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"

namespace nazg
{
    // What a connected board reports about itself -- what a choice is remembered by. The
    // OS device path is not part of it: it changes with the USB port.
    struct DeviceIdentity
    {
        uint16_t    vendorId  = 0;
        uint16_t    productId = 0;
        std::string manufacturer;
        std::string product;
        uint16_t    release = 0;   // bcdDevice -- QMK's usb.device_version
        std::string serial;        // empty when the firmware sets none, as QMK usually does

        bool operator==(const DeviceIdentity&) const = default;
    };

    // A definition, wherever it comes from: a user definition by its entry number, or an
    // official one by its path in VIA's bundle ("v3/3389265408") -- a path named after the
    // VID:PID, so a newer bundle keeps it valid.
    struct DefinitionRef
    {
        enum class Kind
        {
            User,
            Official,
        };

        Kind        kind   = Kind::User;
        uint32_t    userId = 0;      // Kind::User
        std::string officialPath;    // Kind::Official

        static DefinitionRef User(uint32_t id) { return { Kind::User, id, {} }; }
        static DefinitionRef Official(std::string path) { return { Kind::Official, 0, std::move(path) }; }

        bool operator==(const DefinitionRef&) const = default;
    };

    // "user:7" or "official:v3/3389265408", as the library's index stores them.
    [[nodiscard]] std::string FormatDefinitionRef(const DefinitionRef& ref);

    // The inverse; nullopt for anything else.
    [[nodiscard]] std::optional<DefinitionRef> ParseDefinitionRef(std::string_view text);

    struct DefinitionChoice
    {
        DeviceIdentity device;
        DefinitionRef  definition;
    };

    // The choice for a device, in two steps: an exact match, else the most recent of those
    // matching VID:PID, manufacturer and product. Null when there is none. `choices` is in
    // the order they were made, oldest first.
    [[nodiscard]] const DefinitionChoice* FindChoice(const std::vector<DefinitionChoice>& choices,
                                                     const DeviceIdentity&                device) noexcept;

    // Same VID:PID, manufacturer and product -- FindChoice()'s second step, where the
    // release number and serial may differ.
    [[nodiscard]] bool IsSameBoard(const DeviceIdentity& a, const DeviceIdentity& b) noexcept;

    // A definition that could draw a board, parsed, with where it came from.
    struct DefinitionCandidate
    {
        DefinitionRef      ref;
        KeyboardDefinition definition;
        std::string        origin;   // a user definition's: where it was imported from
    };

    // Puts candidates in the order a picker offers them: user definitions before the
    // official one, as VIA overlays them; within each, those whose name matches the
    // product string first -- lowercase, letters and digits only, either containing the
    // other. Otherwise the order is kept.
    void RankCandidates(std::vector<DefinitionCandidate>& candidates, std::string_view product);

    // Which candidate draws the board without asking: the chosen one if it is among them,
    // else the only one. Nullopt means ask -- no candidate, or several and no choice.
    [[nodiscard]] std::optional<size_t> ResolveCandidate(const std::vector<DefinitionCandidate>& candidates,
                                                         const std::optional<DefinitionRef>&     chosen) noexcept;
}
