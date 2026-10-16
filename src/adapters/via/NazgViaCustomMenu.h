// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaCustomMenu - what a board's own menus need beyond their description (adapters/via/
// NazgKeyboardDefinition.h, CustomMenu): their values on the wire, VIA's `showIf` conditions,
// and the constraints tying sliders together -- as the VIA app and @the-via/pelpi read them
// (via-vial-commands.md, "A board's own menus on the wire"). Nothing here knows any board.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"

namespace nazg::via
{
    // The values a board's menus hold, as the board sends them, by the name their controls bind --
    // the name conditions and constraints refer to them by.
    using CustomValues = std::map<std::string, std::vector<uint8_t>, std::less<>>;

    // The board's firmware version in conditions: VIA's keyboard value 4, all 32 bits of it.
    inline constexpr std::string_view c_FirmwareVersionName = "id_firmware_version";

    // How many bytes a control's value takes: two for a keycode, a colour, and a range whose
    // maximum passes 255 -- big-endian -- a toggle's longest state, else one. A bound label takes
    // what the frame has room for, a string ending at a zero.
    [[nodiscard]] size_t ValueWidth(const CustomControl& control);

    // A range's value as a number, and back -- big-endian over its width.
    [[nodiscard]] int                  RangeValueOf(const CustomControl& control, const std::vector<uint8_t>& value);
    [[nodiscard]] std::vector<uint8_t> RangeBytes(const CustomControl& control, int value);

    // A toggle is on when its value is its `on` state, byte for byte -- anything else reads off.
    [[nodiscard]] bool IsToggledOn(const CustomControl& control, const std::vector<uint8_t>& value);

    // A bound label's text: the bytes up to the first zero.
    [[nodiscard]] std::string TextOf(const std::vector<uint8_t>& value);

    // Whether a condition holds: {name} is a value's first byte -- the high byte of a two-byte
    // value -- {name.1} its second, a value not known 0; {id_firmware_version} the whole version.
    // Numbers, == != < <= > >=, && ||, ! before one term, brackets -- split as pelpi splits them,
    // at the first of the loosest operator. A condition Nazg cannot read holds: the line shows.
    [[nodiscard]] bool ConditionHolds(std::string_view condition, const CustomValues& values, uint32_t firmwareVersion);
    [[nodiscard]] bool ConditionsHold(const std::vector<std::string>& conditions, const CustomValues& values,
                                      uint32_t firmwareVersion);

    // Whether Nazg reads a condition at all -- one it cannot is held to be true.
    [[nodiscard]] bool IsReadableCondition(std::string_view condition);

    // The values a condition reads, by name, each once.
    [[nodiscard]] std::vector<std::string> NamesIn(std::string_view condition);

    // Moving the range `name` to `requested`, among `ranges` -- every range of the board's menus:
    // the value each range takes then, by name, those that change. A constraint that pushes moves
    // the other range along -- and that one's own constraints, each range once -- one that does
    // not stops this one; as the VIA app resolves them. Ranges with no value yet are left alone.
    [[nodiscard]] std::map<std::string, int> ResolveRange(const std::vector<const CustomControl*>& ranges,
                                                          const CustomValues& values, const std::string& name,
                                                          int requested);

    // What a slider may be moved between: its own limits, narrowed by its constraints that stop
    // rather than push.
    [[nodiscard]] std::pair<int, int> RangeBounds(const CustomControl& control, const std::vector<const CustomControl*>& ranges,
                                                  const CustomValues& values);

    // A control's value read from the board, at its width, and one written -- held in RAM until
    // via::Protocol::SaveCustomValues() stores its channel. A button writes its `on`.
    [[nodiscard]] Task<std::vector<uint8_t>> ReadCustomValue(Protocol& via, const CustomControl& control);
    [[nodiscard]] Task<void> WriteCustomValue(Protocol& via, const CustomControl& control, const std::vector<uint8_t>& value);
}
