// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// leydenjar::Protocol - the Leyden Jar controller's own commands: Rico's capacitive controller for
// the Brand New Model F and Beam Spring keyboards (ui-design.md, "Plugins"). Its firmware is
// vial-qmk's, so a board on it speaks Vial too; these commands sit beside it.
//
// They ride VIA's get and set keyboard value, 0x02 and 0x03, with value ids from 0x81 -- past
// VIA's own -- and the magic 0x21C0 after them (keyboards/leyden_jar/util_protocol.c in Rico's
// vial-qmk fork):
//
//     [0x02 | 0x03] [value id] [0xC0 0x21] [payload from byte 4]
//
// The firmware fills the payload in place and echoes the first four bytes; what it does not know
// it answers id_unhandled, 0xFF in byte 0, as VIA does an unknown value -- vial-qmk hands those to
// raw_hid_receive_kb(), whose default refuses them. So any VIA or Vial board can be asked, which is
// how a Leyden Jar board is found: Probe(). Its scalars are little-endian, the RP2040's own order.
//
// Three commands are never answered, because the controller restarts first: Enter bootloader,
// Erase EEPROM -- which then reboots -- and Reboot, which Nazg does not use.

#pragma once

#include <compare>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"

namespace nazg::leydenjar
{
    inline constexpr uint16_t c_Magic = 0x21C0;

    // The value ids, in the firmware's order.
    enum class Value : uint8_t
    {
        ProtocolVersion   = 0x81,
        Details           = 0x82,
        KeyOutput         = 0x83,   // get and set: the keys typing, or not -- RAM only
        DetectLevels      = 0x84,   // set: measure every key's level, for ColumnLevels
        DacThreshold      = 0x85,   // per bin
        ColumnLevels      = 0x86,
        ScanLogicalMatrix = 0x87,   // set: scan, for LogicalMatrixRow
        ScanControllerMatrix = 0x88,   // set: scan, for ControllerMatrix
        EnterBootloader   = 0x89,   // not answered
        Reboot            = 0x8A,   // not answered
        EraseEeprom       = 0x8B,   // not answered: the controller reboots
        LogicalMatrixRow  = 0x8C,
        ControllerMatrix  = 0x8D,
        MatrixMapping     = 0x8E,
        DacReferenceLevel = 0x8F,   // per bin, protocol 0.9.1 on
        BinMap            = 0x90,   // protocol 0.9.1 on
        IsLeftHalf        = 0x91,   // protocol 1.0.0 on -- the split firmware's
    };

    // The protocol's version: major.mid.minor -- 0.9.1 for a whole board's firmware, 1.0.0 for a
    // split one's (2026-10).
    struct Version
    {
        uint8_t  major = 0;
        uint8_t  mid   = 0;
        uint16_t minor = 0;

        auto operator<=>(const Version&) const = default;
    };

    // From this version: a threshold and a reference level per bin, and the bin of every key.
    // Before it, one threshold and no bins.
    inline constexpr Version c_BinsVersion{ 0, 9, 1 };

    // From this version: which half of a split board answers.
    inline constexpr Version c_SplitVersion{ 1, 0, 0 };

    // A key's level rises when pressed on a Model F, falls on a Beam Spring.
    enum class SwitchTechnology : uint8_t
    {
        ModelF     = 0,
        BeamSpring = 1,
    };

    // Whose connectors the logical matrix is numbered after: the Leyden Jar's own, or the earlier
    // controllers' it replaces pin for pin.
    enum class MatrixWiring : uint8_t
    {
        LeydenJar = 0,
        XWhatsit  = 1,
        Wcass     = 2,
    };

    struct Details
    {
        uint8_t          logicalRows       = 0;   // QMK's matrix -- both halves of a split board
        uint8_t          logicalColumns    = 0;
        uint8_t          controllerRows    = 0;   // the controller's connectors
        uint8_t          controllerColumns = 0;
        SwitchTechnology technology        = SwitchTechnology::ModelF;
        uint8_t          bins              = 0;   // calibration bins, each with its threshold
    };

    // Where each logical row and column is on the controller's connectors.
    struct MatrixMapping
    {
        uint8_t              wiring = 0;   // a MatrixWiring, kept as sent: a later one is not refused
        std::vector<uint8_t> columns;      // [logical column] -> controller column
        std::vector<uint8_t> rows;         // [logical row of a half] -> controller row
    };

    // What the controller reports about itself, read once when its section opens.
    struct ControllerInfo
    {
        Version              version;
        Details              details;
        MatrixMapping        mapping;
        std::vector<uint16_t> thresholds;        // [bin]; one before c_BinsVersion
        std::vector<uint16_t> referenceLevels;   // [bin]; none before c_BinsVersion
        std::vector<std::vector<uint8_t>> bins;  // [controller column][controller row]; all 0 before c_BinsVersion
        bool                 isLeftHalf = true;  // a split board's half; true for a whole board
    };

    class Protocol : public via::Protocol
    {
    public:
        using via::Protocol::Protocol;

        // Whether the board is a Leyden Jar: its protocol version, or none when it refuses the
        // question -- every other board -- or echoes it unchanged, which reads as 0.0.0. Throws
        // only what the channel throws.
        [[nodiscard]] Task<std::optional<Version>> Probe();

        // Throws ProtocolError when refused, as every call below.
        [[nodiscard]] Task<Version> GetVersion();
        [[nodiscard]] Task<Details> GetDetails();
        [[nodiscard]] Task<MatrixMapping> GetMatrixMapping(const Details& details);
        [[nodiscard]] Task<uint16_t> GetDacThreshold(uint8_t bin);
        [[nodiscard]] Task<uint16_t> GetDacReferenceLevel(uint8_t bin);
        [[nodiscard]] Task<bool> IsLeftHalf();

        // The bin of each of `rows` keys in one controller column; 255 for a position no key is
        // calibrated at.
        [[nodiscard]] Task<std::vector<uint8_t>> GetColumnBins(uint8_t column, uint8_t rows);

        // Whether the keys type. Off, the controller still scans -- for the monitors -- but sends
        // nothing to the host. RAM only: an unplug turns it back on, a crash of Nazg does not.
        [[nodiscard]] Task<bool> IsKeyOutputEnabled();
        [[nodiscard]] Task<void> SetKeyOutputEnabled(bool enabled);

        // Measures every key's level, then reads one controller column's, `rows` of them.
        [[nodiscard]] Task<void> DetectLevels();
        [[nodiscard]] Task<std::vector<uint16_t>> GetColumnLevels(uint8_t column, uint8_t rows);

        // Scans the matrix as QMK sees it, then reads one logical row: bit n is column n.
        [[nodiscard]] Task<void> ScanLogicalMatrix();
        [[nodiscard]] Task<uint32_t> GetLogicalMatrixRow(uint8_t row);

        // Scans the controller's matrix, then reads it: a byte per controller column, `columns` of
        // them, bit n being controller row n. The bits are the comparators' as they come, set for a
        // level above the threshold: pressed on a Model F, RELEASED on a Beam Spring -- only the
        // logical scan inverts them (common.c, leyden_jar_logical_matrix_scan()). A controller the
        // firmware finds wired to no working matrix answers every key released, so as not to send
        // presses: FF on a Beam Spring, 0 on a Model F -- the bare B104, 2026-10-17 (Rico).
        [[nodiscard]] Task<void> ScanControllerMatrix();
        [[nodiscard]] Task<std::vector<uint8_t>> GetControllerMatrix(uint8_t columns);

        // Not answered: the board leaves -- to its bootloader, or rebooting with its EEPROM
        // cleared, which VIA's and Vial's settings live in. Hotplug sees it go.
        [[nodiscard]] Task<void> EnterBootloader();
        [[nodiscard]] Task<void> EraseEeprom();

    private:
        [[nodiscard]] static std::vector<uint8_t> MakeRequest(via::Command command, Value value,
                                                              std::initializer_list<uint8_t> payload = {});
        [[nodiscard]] Task<std::vector<uint8_t>> Ask(via::Command command, Value value,
                                                     std::initializer_list<uint8_t> payload = {});
    };

    // Everything ControllerInfo holds, in the order the Leyden Jar tool reads it: what a version
    // lacks is left at its default. Throws ProtocolError if the board refuses any of it.
    [[nodiscard]] Task<ControllerInfo> ReadControllerInfo(Protocol& protocol);

    // The threshold the firmware sets when it finds the controller wired to no working matrix -- its
    // highest level at most 380 at start-up -- so that nothing is ever pressed: 300 on a Beam Spring,
    // 1023 on a Model F (ui-design.md, "The levels' window"). The bare B104 answers 300.
    [[nodiscard]] uint16_t UnconnectedThreshold(SwitchTechnology technology) noexcept;

    // Every threshold at that sentinel: there are no levels to judge.
    [[nodiscard]] bool IsWiredToNoMatrix(const ControllerInfo& info) noexcept;
}
