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
//   - Vial's own scalars -- protocol version, definition size, page index, setting id --
//     are LITTLE-endian, where VIA's are big-endian, because these commands copy bytes
//     straight out of EEPROM. Keycodes are the exception and stay big-endian, since
//     those come from the dynamic keymap code shared with VIA. Same device, same
//     report, two byte orders.
//
// Also inherited from that document: vial-qmk reports VIA protocol 9 while supporting
// the V3 custom channels, so a client must NOT gate features on the VIA version.

#pragma once

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

    struct VialUnlockStatus
    {
        bool unlocked   = false;
        bool inProgress = false;

        // The matrix positions to hold down, as (row, column). Empty on a VIAL_INSECURE
        // build, which reports itself unlocked and lists no combo.
        std::vector<std::pair<uint8_t, uint8_t>> combo;
    };

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

        // 0xFE 0x05. Worth calling before any write: a locked board silently rewrites
        // QK_BOOT to 0 in everything it accepts, so a write can "succeed" and store
        // something else.
        [[nodiscard]] Task<VialUnlockStatus> GetUnlockStatus();

        // 0xFE 0x03. Vial returns BOTH directions in one reply, where VIA needs a call
        // per direction -- and vial-qmk does not implement VIA's 0x14/0x15 at all.
        struct EncoderPair
        {
            uint16_t counterClockwise = 0;
            uint16_t clockwise        = 0;
        };
        [[nodiscard]] Task<EncoderPair> GetEncoder(uint8_t layer, uint8_t index);

    private:
        // Like ViaProtocol::Send but without the echo check, for the reason in the
        // header comment. Returns the raw 32 bytes for the caller to interpret.
        [[nodiscard]] Task<std::vector<uint8_t>> SendVial(VialCommand command,
                                                          std::initializer_list<uint8_t> arguments);
    };
}
