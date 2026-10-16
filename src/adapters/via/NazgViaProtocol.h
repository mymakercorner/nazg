// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// via::Protocol - the VIA command set, which Vial firmware answers unchanged.
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
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "adapters/NazgProtocolError.h"
#include "async/NazgTask.h"
#include "transport/NazgDeviceChannel.h"

namespace nazg::via
{
    // The full VIA command set as of protocol 13. Commands this class does not implement
    // yet are listed anyway, so the wire values live in exactly one place.
    enum class Command : uint8_t
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

    enum class KeyboardValue : uint8_t
    {
        Uptime            = 0x01,
        LayoutOptions     = 0x02,
        SwitchMatrixState = 0x03,
        FirmwareVersion   = 0x04,
        DeviceIndication  = 0x05,   // set only
        KeycodesVersion   = 0x06,   // VIA >= protocol 13
    };

    enum class Channel : uint8_t
    {
        Custom      = 0,
        QmkBacklight = 1,
        QmkRgbLight  = 2,
        QmkRgbMatrix = 3,
        QmkAudio     = 4,
        QmkLedMatrix = 5,
    };

    // Every exchange is exactly this long, in both directions.
    inline constexpr size_t c_ReportSize = 32;

    // A buffer command spends 4 bytes on command id, offset and length, leaving 28.
    inline constexpr uint16_t c_BufferChunk = 28;

    // How a firmware lays out id_switch_matrix_state's reply. Both give every row as
    // ceil(columns / 8) bytes, most significant first -- column 0 is the last byte's bit 0.
    enum class SwitchMatrixFormat : uint8_t
    {
        // Mainline VIA from protocol 12: the request names a first row, echoed at byte 2,
        // and the rows follow from byte 3, as many as fit in 28 bytes -- paged by row.
        Paged,

        // VIA before protocol 12, and every vial-qmk (2026-08): no first row, the rows from
        // byte 2, and only when the whole matrix fits -- by the firmware's own test,
        // WholeMatrixFits(). A matrix too big for it reads as all released.
        Whole,
    };

    // The matrix state format a VIA board of this protocol answers in -- VIA's own rule
    // (via-app, use-matrix-test.ts). A Vial board is always Whole, whatever VIA protocol it
    // reports.
    [[nodiscard]] SwitchMatrixFormat SwitchMatrixFormatFor(uint16_t viaProtocol) noexcept;

    // The Whole format's condition, as the firmware writes it: (columns / 8 + 1) * rows <= 28.
    // Stricter than the bytes need -- 8 columns count as two bytes -- but it is the one that
    // decides.
    [[nodiscard]] bool WholeMatrixFits(uint8_t rows, uint8_t columns) noexcept;

    // Which switches are closed, as the board last scanned them.
    struct SwitchMatrixState
    {
        uint8_t              rows    = 0;
        uint8_t              columns = 0;
        std::vector<uint8_t> bytes;   // rows x ceil(columns / 8), as they arrive

        // False for a position outside the matrix.
        [[nodiscard]] bool IsPressed(uint8_t row, uint8_t column) const noexcept;
    };

    class Protocol
    {
    public:
        explicit Protocol(DeviceChannel& channel) : m_Channel(channel) {}

        // 0x01. The one command every version answers, so it is the right first call.
        [[nodiscard]] Task<uint16_t> GetProtocolVersion();

        // 0x02 with a 32-bit value id. Throws ProtocolError if this firmware does not
        // know the value -- KeycodesVersion, for instance, needs protocol 13.
        [[nodiscard]] Task<uint32_t> GetKeyboardValue(KeyboardValue value);

        // 0x03: the value, big-endian from byte 2 as GetKeyboardValue() reads it. The board
        // echoes the request; read the value back to know what it kept.
        [[nodiscard]] Task<void> SetKeyboardValue(KeyboardValue value, uint32_t data);

        // 0x11 / 0x04 / 0x05. Layers, and one key at a time.
        [[nodiscard]] Task<uint8_t>  GetLayerCount();
        [[nodiscard]] Task<uint16_t> GetKeycode(uint8_t layer, uint8_t row, uint8_t column);
        [[nodiscard]] Task<void>     SetKeycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);

        // 0x14 / 0x15, VIA protocol 10 on: what one knob sends turned one way on one layer -- the
        // encoder map, built with ENCODER_MAP_ENABLE; without it the firmware answers 0xFF. A
        // call per direction. vial-qmk answers neither: vial::Protocol::GetVialEncoder() is Vial's.
        [[nodiscard]] Task<uint16_t> GetEncoder(uint8_t layer, uint8_t index, bool clockwise);
        [[nodiscard]] Task<void>     SetEncoder(uint8_t layer, uint8_t index, bool clockwise, uint16_t keycode);

        // 0x12. The whole keymap in 28-byte chunks -- the batched read that exists
        // because asking key by key was too chatty. One round trip per chunk.
        [[nodiscard]] Task<std::vector<uint8_t>> GetKeymapBuffer(uint16_t offset, uint16_t length);

        // 0x02 with id_switch_matrix_state: the whole matrix, in one round trip or a few.
        // Needs the firmware's consent -- VIA_INSECURE, or unlocked -- and without it the
        // answer is all released, not a refusal: via-vial-commands.md.
        [[nodiscard]] Task<SwitchMatrixState> GetSwitchMatrixState(SwitchMatrixFormat format, uint8_t rows,
                                                                   uint8_t columns);

        // 0x0C / 0x0D / 0x0E / 0x0F.
        [[nodiscard]] Task<uint8_t>              GetMacroCount();
        [[nodiscard]] Task<uint16_t>             GetMacroBufferSize();
        [[nodiscard]] Task<std::vector<uint8_t>> GetMacroBuffer(uint16_t offset, uint16_t length);
        [[nodiscard]] Task<void>                 SetMacroBuffer(uint16_t offset, const std::vector<uint8_t>& bytes);

        // The bytes that hold `count` macros: read 28 at a time until the count-th 0, or the
        // buffer's end -- never the whole buffer, which runs to 15 KB on some boards
        // (ui-design.md, "The Macros section").
        [[nodiscard]] Task<std::vector<uint8_t>> ReadMacros(uint8_t count, uint16_t bufferSize);

        // Writes `bytes` over `stored`, from the first byte that differs to the end of `bytes`,
        // inside the firmware's guard: the buffer's last byte 0xFF first -- no macro plays while it
        // is not 0 -- then 0 last, so a write cut short leaves the macros silent rather than half
        // written. Vial refuses the write while locked, echoing it unchanged: read back to know.
        [[nodiscard]] Task<void> WriteMacros(const std::vector<uint8_t>& stored, const std::vector<uint8_t>& bytes,
                                             uint16_t bufferSize);

        // 0x08 / 0x07 / 0x09: a custom value -- the lighting systems' among them (model/NazgLighting.h).
        // With a channel, VIA V3's format: [channel][value id][value]. Without, the format before
        // it, which VIA 9 and 10 and every vial-qmk speak: [value id][value] -- VialRGB's commands
        // take the value id's place. Get returns `length` bytes of the value; set is held in RAM
        // until the save, which stores the channel's values -- every lighting value, without one.
        [[nodiscard]] Task<std::vector<uint8_t>> GetCustomValue(std::optional<uint8_t> channel, uint8_t valueId,
                                                                std::vector<uint8_t> arguments, size_t length);
        [[nodiscard]] Task<void> SetCustomValue(std::optional<uint8_t> channel, uint8_t valueId,
                                                const std::vector<uint8_t>& value);
        [[nodiscard]] Task<void> SaveCustomValues(std::optional<uint8_t> channel);

        // Probe for a command this firmware may not have. Sends it and reports whether
        // the answer was anything other than 0xFF, because that is the only way to tell:
        // protocol version numbers track features in NEITHER direction (VIA gained
        // custom channels with no bump; Vial gained alt repeat key with none).
        [[nodiscard]] Task<bool> Supports(Command command, std::initializer_list<uint8_t> arguments = {});

    protected:
        // Builds the 32-byte frame, sends it, and verifies the reply echoes the command
        // id. Shared with the Vial branch, which needs the framing but checks its
        // replies differently -- Vial overwrites the buffer from byte 0.
        //
        // A report that does not echo the request is not the reply: the channel reads past
        // it (a Keychron board's layer report, sent on every layer change) and times out if
        // the reply never comes. What the firmware echoes is in ReplyTo().
        [[nodiscard]] Task<std::vector<uint8_t>> Send(Command command, std::initializer_list<uint8_t> arguments);

        // The same, for a frame built whole -- one carrying data, as a buffer write does.
        // `hasArguments` names the sub-id in the error when the firmware refuses it;
        // `echoed` is how many leading bytes the firmware leaves as sent, the command id
        // included -- EchoedBytes() for every command but the custom values, whose address
        // is one byte longer with a channel.
        [[nodiscard]] Task<std::vector<uint8_t>> SendFrame(std::vector<uint8_t> frame, bool hasArguments,
                                                           size_t echoed);

        // How many leading bytes of a request for this command its reply keeps: the command id,
        // then the address the firmware reads and writes past -- via-vial-commands.md, "VIA --
        // command set". 1 for the custom values, which depend on the channel.
        [[nodiscard]] static size_t EchoedBytes(Command command) noexcept;

        // The channel's check that a report is the reply to `frame`: its first `echoed` bytes as
        // sent, or 0xFF in byte 0 -- a refusal, whatever follows it.
        [[nodiscard]] static ReplyCheck ReplyTo(const std::vector<uint8_t>& frame, size_t echoed);

        static std::vector<uint8_t> MakeFrame(uint8_t command, std::initializer_list<uint8_t> arguments);

        DeviceChannel& m_Channel;

    private:
        // 0x0E and 0x12 differ only in the command id.
        [[nodiscard]] Task<std::vector<uint8_t>> ReadChunked(Command command, uint16_t offset, uint16_t length);
    };
}
