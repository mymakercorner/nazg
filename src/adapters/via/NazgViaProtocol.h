// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaProtocol - the VIA command set, which Vial firmware answers unchanged.
//
// This is the shared half of "one backend with a Vial branch": keymap, layers, macros,
// buffers and keyboard values are byte-identical on both firmwares. The Vial branch
// lives in adapters/vial and drives the same channel for the commands that differ.
//
// Wire format, and why the reply check below matters, are documented in
// docs/research_material/via-vial-commands.md. In short: every exchange is one fixed
// 32-byte report, the firmware echoes the buffer back with fields overwritten, and the
// ONLY failure signal is the first byte coming back as 0xFF. There is no status code,
// and a command compiled out of the firmware is indistinguishable from one that does
// not exist -- so capability detection means sending the command and watching for 0xFF.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

#include "async/NazgTask.h"
#include "transport/NazgDeviceChannel.h"

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

    // The full VIA command set as of protocol 13. Commands this class does not implement
    // yet are listed anyway, so the wire values live in exactly one place.
    enum class ViaCommand : uint8_t
    {
        GetProtocolVersion       = 0x01,
        GetKeyboardValue         = 0x02,
        SetKeyboardValue         = 0x03,
        DynamicKeymapGetKeycode  = 0x04,
        DynamicKeymapSetKeycode  = 0x05,
        DynamicKeymapReset       = 0x06,
        CustomSetValue           = 0x07,
        CustomGetValue           = 0x08,
        CustomSave               = 0x09,
        EepromReset              = 0x0A,
        BootloaderJump           = 0x0B,   // no handler on mainline VIA; always 0xFF
        MacroGetCount            = 0x0C,
        MacroGetBufferSize       = 0x0D,
        MacroGetBuffer           = 0x0E,
        MacroSetBuffer           = 0x0F,
        MacroReset               = 0x10,
        DynamicKeymapGetLayerCount = 0x11,
        DynamicKeymapGetBuffer   = 0x12,
        DynamicKeymapSetBuffer   = 0x13,
        DynamicKeymapGetEncoder  = 0x14,   // VIA >= protocol 10; absent on Vial firmware
        DynamicKeymapSetEncoder  = 0x15,
        VialPrefix               = 0xFE,   // Vial firmware only, see adapters/vial
        Unhandled                = 0xFF,
    };

    enum class ViaKeyboardValue : uint8_t
    {
        Uptime            = 0x01,
        LayoutOptions     = 0x02,
        SwitchMatrixState = 0x03,
        FirmwareVersion   = 0x04,
        DeviceIndication  = 0x05,   // set only
        KeycodesVersion   = 0x06,   // VIA >= protocol 13
    };

    enum class ViaChannel : uint8_t
    {
        Custom      = 0,
        QmkBacklight = 1,
        QmkRgbLight  = 2,
        QmkRgbMatrix = 3,
        QmkAudio     = 4,
        QmkLedMatrix = 5,
    };

    // Every exchange is exactly this long, in both directions.
    inline constexpr size_t c_ViaReportSize = 32;

    // A buffer command spends 4 bytes on command id, offset and length, leaving 28.
    inline constexpr uint16_t c_ViaBufferChunk = 28;

    class ViaProtocol
    {
    public:
        explicit ViaProtocol(DeviceChannel& channel) : m_Channel(channel) {}

        // 0x01. The one command every version answers, so it is the right first call.
        [[nodiscard]] Task<uint16_t> GetProtocolVersion();

        // 0x02 with a 32-bit value id. Throws ProtocolError if this firmware does not
        // know the value -- KeycodesVersion, for instance, needs protocol 13.
        [[nodiscard]] Task<uint32_t> GetKeyboardValue(ViaKeyboardValue value);

        // 0x11 / 0x04 / 0x05. Layers, and one key at a time.
        [[nodiscard]] Task<uint8_t>  GetLayerCount();
        [[nodiscard]] Task<uint16_t> GetKeycode(uint8_t layer, uint8_t row, uint8_t column);
        [[nodiscard]] Task<void>     SetKeycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);

        // 0x12. The whole keymap in 28-byte chunks -- the batched read that exists
        // because asking key by key was too chatty. One round trip per chunk.
        [[nodiscard]] Task<std::vector<uint8_t>> GetKeymapBuffer(uint16_t offset, uint16_t length);

        // 0x0C / 0x0D / 0x0E.
        [[nodiscard]] Task<uint8_t>              GetMacroCount();
        [[nodiscard]] Task<uint16_t>             GetMacroBufferSize();
        [[nodiscard]] Task<std::vector<uint8_t>> GetMacroBuffer(uint16_t offset, uint16_t length);

        // Probe for a command this firmware may not have. Sends it and reports whether
        // the answer was anything other than 0xFF, because that is the only way to tell:
        // protocol version numbers track features in NEITHER direction (VIA gained
        // custom channels with no bump; Vial gained alt repeat key with none).
        [[nodiscard]] Task<bool> Supports(ViaCommand command, std::initializer_list<uint8_t> arguments = {});

    protected:
        // Builds the 32-byte frame, sends it, and verifies the reply echoes the command
        // id. Shared with the Vial branch, which needs the framing but checks its
        // replies differently -- Vial overwrites the buffer from byte 0.
        [[nodiscard]] Task<std::vector<uint8_t>> Send(ViaCommand command, std::initializer_list<uint8_t> arguments);

        static std::vector<uint8_t> MakeFrame(uint8_t command, std::initializer_list<uint8_t> arguments);

        DeviceChannel& m_Channel;

    private:
        // 0x0E and 0x12 differ only in the command id.
        [[nodiscard]] Task<std::vector<uint8_t>> ReadChunked(ViaCommand command, uint16_t offset, uint16_t length);
    };
}
