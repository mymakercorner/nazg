// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// VialProtocol - the Vial extensions, which live behind a single 0xFE prefix byte.
//
// It derives from ViaProtocol because Vial firmware IS VIA firmware plus this command
// space: keymap, layers, macros and buffers are byte-identical and inherited unchanged.
// That is the "one backend with a Vial branch" decision, in code.
//
// Two differences from the VIA half that are easy to get wrong, both from
// docs/research_material/via-vial-commands.md:
//
//   - Vial replies OVERWRITE the buffer from byte 0, so there is no command id to echo
//     back and no 0xFF marker to check. Every reply has to be validated by its content.
//     A sub-command the firmware was built without does not answer 0xFF either: it
//     falls through its switch and the request comes back unchanged, so "the reply
//     equals the request" is how an absent Vial feature announces itself.
//   - Vial's own scalars -- protocol version, definition size, page index, setting id --
//     are LITTLE-endian, where VIA's are big-endian, because these commands copy bytes
//     straight out of EEPROM. Keycodes are the exception and stay big-endian, since
//     those come from the dynamic keymap code shared with VIA. Same device, same
//     report, two byte orders.
//
// Also inherited from that document: vial-qmk reports VIA protocol 9 while supporting
// the V3 custom channels, so a client must NOT gate features on the VIA version.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"

namespace nazg
{
    enum class VialCommand : uint8_t
    {
        GetKeyboardId    = 0x00,
        GetSize          = 0x01,
        GetDefinition    = 0x02,
        GetEncoder       = 0x03,
        SetEncoder       = 0x04,
        GetUnlockStatus  = 0x05,
        UnlockStart      = 0x06,
        UnlockPoll       = 0x07,
        Lock             = 0x08,
        QmkSettingsQuery = 0x09,
        QmkSettingsGet   = 0x0A,
        QmkSettingsSet   = 0x0B,
        QmkSettingsReset = 0x0C,
        DynamicEntryOp   = 0x0D,
    };

    enum class VialDynamicEntry : uint8_t
    {
        GetNumberOfEntries = 0x00,
        TapDanceGet        = 0x01,
        TapDanceSet        = 0x02,
        ComboGet           = 0x03,
        ComboSet           = 0x04,
        KeyOverrideGet     = 0x05,
        KeyOverrideSet     = 0x06,
        AltRepeatKeyGet    = 0x07,
        AltRepeatKeySet    = 0x08,
    };

    struct VialIdentity
    {
        uint32_t protocolVersion = 0;
        uint64_t keyboardUid     = 0;
        bool     supportsVialRgb = false;
    };

    // How many of each dynamic entry this firmware has room for. Zero means the feature
    // is compiled out. This is the capability query -- NOT the protocol version, which
    // stayed at 6 when alt repeat key was added in 2025.
    struct VialEntryCounts
    {
        uint8_t tapDance     = 0;
        uint8_t combo        = 0;
        uint8_t keyOverride  = 0;
        uint8_t altRepeatKey = 0;
        bool    capsWord     = false;   // feature bits in the last byte of the reply
        bool    layerLock    = false;
    };

    // One tap dance slot as the board stores it, `vial_tap_dance_entry_t`: four raw keycodes --
    // 0 (KC_NO) where an action is empty -- and the slot's own tapping term in milliseconds.
    struct VialTapDanceEntry
    {
        uint16_t onTap       = 0;
        uint16_t onHold      = 0;
        uint16_t onDoubleTap = 0;
        uint16_t onTapHold   = 0;
        uint16_t tappingTerm = 0;

        bool operator==(const VialTapDanceEntry&) const = default;
    };

    // One combo slot as the board stores it, `vial_combo_entry_t`: four input keycodes -- the first
    // 0 (COMBO_END) ending them, so an input after an empty one is never matched -- and the output,
    // 0 where empty. A slot whose first input is 0 is unused.
    struct VialComboEntry
    {
        std::array<uint16_t, 4> inputs{};
        uint16_t                output = 0;

        bool operator==(const VialComboEntry&) const = default;
    };

    // One key override slot as the board stores it, `vial_key_override_entry_t`: the trigger and
    // the replacement, 0 where empty; the layers it acts on, one bit each; three modifier masks in
    // the USB HID order (model/NazgKeycode.h, Mod) -- held, not held, hidden from the computer --
    // and the options, Vial's enable flag in bit 7 (model/NazgKeyOverride.h, KeyOverrideOption).
    struct VialKeyOverrideEntry
    {
        uint16_t trigger         = 0;
        uint16_t replacement     = 0;
        uint16_t layers          = 0;
        uint8_t  triggerMods     = 0;
        uint8_t  negativeModMask = 0;
        uint8_t  suppressedMods  = 0;
        uint8_t  options         = 0;

        bool operator==(const VialKeyOverrideEntry&) const = default;
    };

    // QMK Settings' id for the combo term, `combo_term`: one u16 in milliseconds for every combo.
    inline constexpr uint16_t c_QmkSettingComboTerm = 2;

    struct VialUnlockStatus
    {
        bool unlocked   = false;
        bool inProgress = false;

        // The keys to hold down to unlock, by matrix position, as (row, column) -- Vial calls them
        // its unlock combo, nothing to do with the Combos feature. Empty on a VIAL_INSECURE build,
        // which reports itself unlocked and lists none.
        std::vector<std::pair<uint8_t, uint8_t>> unlockKeys;
    };

    // How far an unlock has got, from 0xFE 0x07.
    struct VialUnlockProgress
    {
        bool    unlocked   = false;
        bool    inProgress = false;
        uint8_t countdown  = 0;   // steps left, from c_VialUnlockSteps down to 0
    };

    // Where an unlock's countdown starts (VIAL_UNLOCK_COUNTER_MAX in vial-qmk).
    inline constexpr uint8_t c_VialUnlockSteps = 50;

    class VialProtocol : public ViaProtocol
    {
    public:
        explicit VialProtocol(DeviceChannel& channel) : ViaProtocol(channel) {}

        // 0xFE 0x00. Returns nothing when this is a plain VIA board: the firmware does
        // not know 0xFE and answers with VIA's 0xFF marker in byte 0. A Vial board puts
        // the low byte of its protocol version there instead, and those run 0 to 6, so
        // the two cannot be confused in practice.
        [[nodiscard]] Task<std::optional<VialIdentity>> Detect();

        // 0xFE 0x01, then 0xFE 0x02 page by page. The result is the definition exactly
        // as stored in flash: LZMA-compressed JSON, NOT yet decompressed. Decoding it
        // is the descriptor layer's job, with minlzma.
        [[nodiscard]] Task<uint32_t>             GetDefinitionSize();
        [[nodiscard]] Task<std::vector<uint8_t>> DownloadDefinition();

        // 0xFE 0x0D 0x00.
        [[nodiscard]] Task<VialEntryCounts> GetEntryCounts();

        // 0xFE 0x0D 0x01 / 0x02: one tap dance slot, read or written. The entry travels as the
        // firmware stores it, little-endian -- keycodes included, unlike the keymap's. A write
        // passes Vial's keycode firewall, so a locked board stores Boot as 0: read it back. A slot
        // past the board's count answers a non-zero status, thrown as a ProtocolError.
        [[nodiscard]] Task<VialTapDanceEntry> GetTapDance(uint8_t index);
        [[nodiscard]] Task<void>              SetTapDance(uint8_t index, const VialTapDanceEntry& entry);

        // 0xFE 0x0D 0x03 / 0x04: one combo slot, read or written, as the tap dances. Only the output
        // passes Vial's keycode firewall; the inputs are stored as sent.
        [[nodiscard]] Task<VialComboEntry> GetCombo(uint8_t index);
        [[nodiscard]] Task<void>           SetCombo(uint8_t index, const VialComboEntry& entry);

        // 0xFE 0x0D 0x05 / 0x06: one key override slot, read or written, as the tap dances. Only the
        // replacement passes Vial's keycode firewall.
        [[nodiscard]] Task<VialKeyOverrideEntry> GetKeyOverride(uint8_t index);
        [[nodiscard]] Task<void>                 SetKeyOverride(uint8_t index, const VialKeyOverrideEntry& entry);

        // 0xFE 0x0A / 0x0B: one QMK setting's value, `width` bytes little-endian (1, 2 or 4 -- the
        // setting's own; the firmware writes only that many over the request). A write sends four
        // bytes, of which the firmware takes its width. A setting the firmware lacks answers a
        // non-zero status, thrown as a ProtocolError; QMK settings compiled out echo the request.
        [[nodiscard]] Task<uint32_t> GetQmkSetting(uint16_t id, size_t width);
        [[nodiscard]] Task<void>     SetQmkSetting(uint16_t id, uint32_t value);

        // 0xFE 0x09, from Vial protocol 4: the QMK settings this firmware has with an id above
        // `after`, as many as one report holds -- query again from the last to have them all.
        // None when QMK settings are compiled out: the firmware fills the report with 0xFF.
        [[nodiscard]] Task<std::vector<uint16_t>> QueryQmkSettings(uint16_t after);

        // 0xFE 0x05. Worth calling before any write: a locked board silently rewrites
        // QK_BOOT to 0 in everything it accepts, so a write can "succeed" and store
        // something else.
        [[nodiscard]] Task<VialUnlockStatus> GetUnlockStatus();

        // 0xFE 0x06. The board starts an unlock: it counts down while the unlock keys are held.
        // Until it succeeds or the board restarts, the board drops every command but the few
        // an unlock needs -- echoing each back unanswered, so a keymap read returns garbage
        // and a write seems to succeed. Nothing cancels it.
        [[nodiscard]] Task<void> StartUnlock();

        // 0xFE 0x07. A poll counts one step down when the unlock keys are held and more than 100 ms
        // have passed since the last step; any other poll -- a key released, or one too soon
        // -- starts the countdown over. So polls must come more than 100 ms apart, and an
        // unlock takes c_VialUnlockSteps of them.
        [[nodiscard]] Task<VialUnlockProgress> PollUnlock();

        // 0xFE 0x08. Locks the board again; a restart does too.
        [[nodiscard]] Task<void> Lock();

        // 0xFE 0x03. Vial returns BOTH directions in one reply, where VIA needs a call
        // per direction -- and vial-qmk does not implement VIA's 0x14/0x15 at all.
        struct EncoderPair
        {
            uint16_t counterClockwise = 0;
            uint16_t clockwise        = 0;
        };
        [[nodiscard]] Task<EncoderPair> GetEncoder(uint8_t layer, uint8_t index);

    private:
        // Like ViaProtocol::Send but checked differently, for the reason in the header
        // comment. Returns the raw 32 bytes for the caller to interpret. Pass
        // expectsData = false for commands that answer with nothing, so their empty
        // reply is not mistaken for an unsupported feature.
        [[nodiscard]] Task<std::vector<uint8_t>> SendVial(VialCommand                    command,
                                                          std::initializer_list<uint8_t> arguments,
                                                          bool                           expectsData = true);
    };
}
