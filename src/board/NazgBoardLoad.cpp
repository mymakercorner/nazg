// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgBoardLoad.h"

#include <exception>
#include <stdexcept>
#include <utility>

#include "adapters/NazgProtocolError.h"
#include "adapters/via/NazgViaLoader.h"
#include "adapters/vial/NazgEncoderMap.h"
#include "adapters/vial/NazgVialLoader.h"

namespace nazg
{
    namespace
    {
        // A VIA board: its candidates gathered and ranked, and the one that draws it loaded --
        // unless the choice is the user's to make.
        Task<void> LoadVia(vial::Protocol& protocol, const DeviceIdentity& identity,
                           std::vector<DefinitionCandidate> candidates, const std::optional<DefinitionRef>& chosen,
                           const BundledDefinitions& bundles, BoardLoad& load)
        {
            // The official definition needs the protocol, which chooses V2 or V3; then it is a
            // lookup in the bundle inflated at start.
            std::optional<DefinitionCandidate>  official;
            std::optional<std::vector<uint8_t>> officialBytes;
            if (bundles.official != nullptr)
            {
                const uint16_t viaProtocol = co_await protocol.GetProtocolVersion();
                officialBytes = bundles.official->Find(identity.vendorId, identity.productId, viaProtocol);
                if (officialBytes)
                    official = DefinitionCandidate{
                        DefinitionRef::Official(via::DefinitionPath(identity.vendorId, identity.productId, viaProtocol)),
                        via::ParseDefinition(*officialBytes), {} };
            }

            // The community's: every definition for this VID:PID -- vendors' made-up ids collide.
            // One that does not parse is left out; the bundle's CI should have refused it.
            if (bundles.community != nullptr)
                for (const via::CommunityDefinition* entry :
                     bundles.community->Find(identity.vendorId, identity.productId))
                    try
                    {
                        DefinitionCandidate candidate{ DefinitionRef::Community(entry->path),
                                                       via::ParseDefinition(*bundles.community->Bytes(entry->path)),
                                                       entry->publisher };
                        candidate.status = entry->status;
                        load.bundled.push_back(std::move(candidate));
                    }
                    catch (const std::exception& failure)
                    {
                        load.skipped.push_back(entry->path + ": " + failure.what());
                    }
            if (official)
                load.bundled.push_back(*official);

            candidates.insert(candidates.end(), load.bundled.begin(), load.bundled.end());
            RankCandidates(candidates, identity.product);
            load.candidates = std::move(candidates);
            load.used       = ResolveCandidate(load.candidates, chosen);

            // Nothing to draw with until the user picks: the picker takes the window.
            if (!load.used)
                co_return;

            const DefinitionCandidate& use = load.candidates[*load.used];
            load.keyboard                  = co_await via::LoadKeyboard(protocol, use.definition);
            if (use.ref.kind == DefinitionRef::Kind::Official && officialBytes)
                load.exportable = std::move(*officialBytes);
            else if (use.ref.kind == DefinitionRef::Kind::Community && bundles.community != nullptr)
                load.exportable = bundles.community->Bytes(use.ref.bundlePath).value_or(std::vector<uint8_t>());
        }

        // What the keycode picker offers, which lighting keycodes work on this firmware, which
        // sections the board has -- and what Keymap draws from beyond the keymap.
        Task<void> ReadExtras(vial::Protocol& protocol, Keyboard& keyboard)
        {
            keyboard.report = co_await vial::ReadBoardReport(protocol);

            // A board's own menus may hang on its firmware version -- the column leaves out
            // those it is too old for. A board that does not answer is version 0, QMK's default.
            if (!keyboard.report.isVial && !keyboard.definition.customMenus.empty())
            {
                try
                {
                    keyboard.report.firmwareVersion = co_await protocol.GetKeyboardValue(via::KeyboardValue::FirmwareVersion);
                }
                catch (const ProtocolError&)
                {
                }
            }

            // Keymap draws a tap dance from its slot. Unread, its keys say "TD n" and the Tap
            // Dance section reads them itself.
            if (const uint8_t count = keyboard.report.tapDanceCount; count > 0)
            {
                try
                {
                    keyboard.tapDances = co_await vial::ReadTapDances(protocol, count, keyboard.keycodeVersion);
                }
                catch (const ProtocolError&)
                {
                }
            }

            // Keymap draws a knob's turns from the encoder map. Unread, or kept by no map, they are
            // its firmware's own code.
            if (const uint8_t knobs = KnobCount(keyboard.definition); knobs > 0)
            {
                try
                {
                    keyboard.encoderMap = co_await vial::ReadEncoderMap(protocol, keyboard.report.isVial,
                                                                        keyboard.report.viaProtocol,
                                                                        keyboard.keymap.Layers(), knobs,
                                                                        keyboard.keycodeVersion);
                }
                catch (const ProtocolError&)
                {
                }
            }
        }
    }

    Task<BoardLoad> LoadBoard(vial::Protocol&                  protocol,
                              DeviceIdentity                   identity,
                              std::vector<DefinitionCandidate> userCandidates,
                              std::optional<DefinitionRef>     chosen,
                              BundledDefinitions               bundles)
    {
        BoardLoad load;

        // Vial first: its probe is harmless on a VIA board, which answers 0xFF.
        if (co_await protocol.Detect())
        {
            // Asked first: a board in the middle of an unlock answers nothing but the unlock's
            // own commands, and echoes the rest -- its keymap would read as garbage.
            const vial::UnlockStatus lock = co_await protocol.GetUnlockStatus();
            if (lock.inProgress)
                throw std::runtime_error("this board is waiting for an unlock that was started and never "
                                         "finished, and answers nothing else until then -- unplug it and "
                                         "plug it back in");

            load.isVial   = true;
            load.lock     = lock;
            load.keyboard = co_await vial::LoadKeyboard(protocol, &load.exportable);
        }
        else
        {
            co_await LoadVia(protocol, identity, std::move(userCandidates), chosen, bundles, load);
        }

        if (load.keyboard)
            co_await ReadExtras(protocol, *load.keyboard);

        co_return load;
    }
}
