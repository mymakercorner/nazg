// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSavedLayoutTransfer.h"

#include <algorithm>
#include <set>
#include <utility>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/via/NazgViaMacro.h"
#include "adapters/via/NazgViaSavedLayout.h"
#include "adapters/vial/NazgEncoderMap.h"
#include "adapters/vial/NazgVialLoader.h"
#include "model/NazgQmkSettings.h"

namespace nazg
{
    namespace
    {
        bool IsBoot(const std::optional<Keycode>& key)
        {
            return key && IsNamed(*key, "QK_BOOT");
        }

        // A slot holding something: a macro with an action, a rule with a key.
        bool Holds(const via::MacroActions& macro) { return !macro.empty(); }
        template <typename T>
        bool Holds(const T& slot)
        {
            return !slot.IsEmpty();
        }

        std::string Plural(size_t count, const char* one, const char* many)
        {
            return std::to_string(count) + " " + (count == 1 ? one : many);
        }

        // The board's slots against the file's: those that differ, and the file's past the board's
        // count that hold something, said as left out. A slot the file could not read is skipped --
        // the file's reading said it.
        template <typename T>
        std::vector<size_t> CompareSlots(const std::vector<std::optional<T>>& board, const std::vector<std::optional<T>>& file,
                                         const char* one, const char* many, std::vector<std::string>& leftOut)
        {
            std::vector<size_t> differ;
            size_t              past = 0;
            for (size_t slot = 0; slot < file.size(); ++slot)
            {
                if (!file[slot])
                    continue;
                if (slot >= board.size())
                {
                    if (Holds(*file[slot]))
                        ++past;
                    continue;
                }
                if (board[slot] != file[slot])
                    differ.push_back(slot);
            }
            if (past > 0)
                leftOut.push_back(Plural(past, one, many) + " past the " + std::to_string(board.size()) +
                                  " this keyboard has");
            return differ;
        }
    }

    size_t LayoutChanges::Count() const noexcept
    {
        return keys.size() + knobs.size() + (layoutOptions ? 1 : 0) + (macros.empty() ? 0 : 1) + tapDances.size() +
               combos.size() + keyOverrides.size() + altRepeatKeys.size() + qmkSettings.size();
    }

    bool LayoutChanges::NeedsUnlock(const SavedLayout& file) const
    {
        if (!macros.empty())
            return true;
        for (const KeyChange& key : keys)
            if (IsBoot(key.then))
                return true;
        for (const KnobChange& knob : knobs)
            if (IsBoot(knob.then))
                return true;
        for (size_t slot : tapDances)
            for (const auto& action : file.tapDances[slot]->actions)
                if (IsBoot(action))
                    return true;
        for (size_t slot : combos)
            if (IsBoot(file.combos[slot]->output))
                return true;
        for (size_t slot : keyOverrides)
            if (IsBoot(file.keyOverrides[slot]->replacement))
                return true;
        for (size_t slot : altRepeatKeys)
            if (IsBoot(file.altRepeatKeys[slot]->altKey))
                return true;
        return false;
    }

    // VIA's names read with the table VIA would use: its VIA protocol's on a VIA board, the keycode
    // version's on a Vial board, which always says VIA 9 (via-layout-files.md, "For Nazg").
    SavedLayout ReadSavedLayoutFile(std::string_view text, const Keyboard& keyboard)
    {
        if (!via::IsSavedLayout(text))
            return vial::ReadSavedLayout(text, keyboard.keycodeVersion);
        const via::NameTable table = keyboard.report.isVial ? via::NameTableFor(keyboard.keycodeVersion)
                                                            : via::NameTableFor(keyboard.report.viaProtocol);
        return via::ReadSavedLayout(text, table, keyboard.keycodeVersion, keyboard.definition.matrixRows,
                                    keyboard.definition.matrixColumns);
    }

    Task<BoardContents> ReadBoardContents(vial::Protocol& protocol, const Keyboard& keyboard, uint16_t vendorId,
                                          uint16_t productId)
    {
        BoardContents      board;
        SavedLayout& layout = board.layout;
        const BoardReport& report = keyboard.report;

        layout.vialProtocol   = report.isVial ? static_cast<int32_t>(report.vialProtocol) : -1;
        layout.viaProtocol    = report.viaProtocol;
        layout.vendorId       = vendorId;
        layout.productId      = productId;
        layout.keycodeVersion = keyboard.keycodeVersion;

        if (report.isVial)
            if (const std::optional<vial::Identity> identity = co_await protocol.Detect())
                layout.uid = identity->keyboardUid;

        const Keymap& keymap = keyboard.keymap;
        layout.keymap.resize(keymap.Layers());
        for (uint8_t layer = 0; layer < keymap.Layers(); ++layer)
        {
            layout.keymap[layer].resize(keymap.Rows());
            for (uint8_t row = 0; row < keymap.Rows(); ++row)
                for (uint8_t column = 0; column < keymap.Columns(); ++column)
                    layout.keymap[layer][row].push_back(keymap.At(layer, row, column));
        }

        for (const auto& turns : keyboard.encoderMap)
        {
            auto& knobs = layout.knobs.emplace_back();
            for (const KnobTurns& knob : turns)
                knobs.push_back({ knob.clockwise, knob.counterClockwise });
        }

        if (!keyboard.layoutSelection.empty())
            layout.layoutOptions = keyboard.layoutOptions;

        if (report.macroCount > 0)
        {
            board.macroBufferSize = co_await protocol.GetMacroBufferSize();
            board.macroBytes      = co_await protocol.ReadMacros(report.macroCount, board.macroBufferSize);
            for (via::MacroActions& macro :
                 via::DecodeMacros(board.macroBytes, report.macroCount, via::MacroFormatFor(report)))
                layout.macros.push_back(std::move(macro));
        }

        const qmk::KeycodeVersion version = keyboard.keycodeVersion;
        if (report.tapDanceCount > 0)
            for (TapDance& dance : co_await vial::ReadTapDances(protocol, report.tapDanceCount, version))
                layout.tapDances.push_back(std::move(dance));
        if (report.comboCount > 0)
            for (Combo& combo : co_await vial::ReadCombos(protocol, report.comboCount, version))
                layout.combos.push_back(std::move(combo));
        if (report.keyOverrideCount > 0)
            for (KeyOverride& rule : co_await vial::ReadKeyOverrides(protocol, report.keyOverrideCount, version))
                layout.keyOverrides.push_back(std::move(rule));
        if (report.altRepeatKeyCount > 0)
            for (AltRepeatKey& rule : co_await vial::ReadAltRepeatKeys(protocol, report.altRepeatKeyCount, version))
                layout.altRepeatKeys.push_back(std::move(rule));

        if (report.hasQmkSettings)
            for (uint16_t qsid : co_await protocol.ListQmkSettings())
                if (const size_t width = QmkSettingWidth(qsid); width != 0)
                    layout.qmkSettings[qsid] = co_await protocol.GetQmkSetting(qsid, width);

        co_return board;
    }

    LayoutChanges CompareWithBoard(const BoardContents& board, const SavedLayout& file, const Keyboard& keyboard)
    {
        LayoutChanges             changes;
        const SavedLayout&        now     = board.layout;
        std::vector<std::string>& leftOut = changes.leftOut;

        // The keymap, by cell: the same matrix, or nothing -- another one would put its keys on others.
        const Keymap& keymap = keyboard.keymap;
        if (!file.keymap.empty() && (file.Rows() != keymap.Rows() || file.Columns() != keymap.Columns()))
        {
            changes.keymapFits = false;
            leftOut.push_back("the file's keymap has " + std::to_string(file.Rows()) + " × " +
                              std::to_string(file.Columns()) + " cells, this keyboard's " + std::to_string(keymap.Rows()) +
                              " × " + std::to_string(keymap.Columns()) + ": its keys would land on other keys");
        }

        if (changes.keymapFits)
        {
            // Only the cells a key of the definition uses: the others are not drawn, and Vial leaves them.
            std::set<std::pair<uint8_t, uint8_t>> used;
            for (const via::DefinitionKey& key : keyboard.definition.keys)
                if (key.IsSwitch())
                    used.insert({ key.row, key.column });

            for (size_t layer = 0; layer < file.keymap.size(); ++layer)
            {
                if (layer >= keymap.Layers())
                {
                    leftOut.push_back(Plural(file.keymap.size() - keymap.Layers(), "layer", "layers") +
                                      " past the " + std::to_string(keymap.Layers()) + " this keyboard has");
                    break;
                }
                for (const auto& [row, column] : used)
                {
                    if (row >= file.keymap[layer].size() || column >= file.keymap[layer][row].size())
                        continue;
                    const std::optional<Keycode>& then = file.keymap[layer][row][column];
                    const Keycode&                was  = keymap.At(static_cast<uint8_t>(layer), row, column);
                    if (then && *then != was)
                        changes.keys.push_back({ static_cast<uint8_t>(layer), row, column, was, *then });
                }
            }

            // Knobs: by index, as the encoder map numbers them.
            bool anyTurn = false;
            for (size_t layer = 0; layer < file.knobs.size(); ++layer)
                for (size_t knob = 0; knob < file.knobs[layer].size(); ++knob)
                {
                    const SavedKnob& saved = file.knobs[layer][knob];
                    if (!saved.clockwise && !saved.counterClockwise)
                        continue;
                    anyTurn = true;
                    if (layer >= now.knobs.size() || knob >= now.knobs[layer].size())
                        continue;
                    const SavedKnob& held = now.knobs[layer][knob];
                    for (const bool clockwise : { false, true })
                    {
                        const std::optional<Keycode>& then = clockwise ? saved.clockwise : saved.counterClockwise;
                        const std::optional<Keycode>& was  = clockwise ? held.clockwise : held.counterClockwise;
                        if (then && was && *then != *was)
                            changes.knobs.push_back({ static_cast<uint8_t>(layer), static_cast<uint8_t>(knob), clockwise,
                                                      *was, *then });
                    }
                }
            if (anyTurn && now.knobs.empty())
                leftOut.push_back("the knobs' turns: this keyboard's firmware keeps no encoder map");

            if (file.layoutOptions && now.layoutOptions && *file.layoutOptions != *now.layoutOptions)
                changes.layoutOptions = file.layoutOptions;
        }
        else if (file.layoutOptions)
            leftOut.push_back("the layout options: they are another keyboard's");

        // Macros: written as one buffer, so they must fit it together.
        changes.macros = CompareSlots(now.macros, file.macros, "macro", "macros", leftOut);
        if (!changes.macros.empty())
        {
            std::vector<via::MacroActions> all;
            for (const auto& macro : now.macros)
                all.push_back(*macro);
            for (size_t slot : changes.macros)
                all[slot] = *file.macros[slot];

            std::string                error;
            const std::vector<uint8_t> bytes = via::EncodeMacros(all, via::MacroFormatFor(keyboard.report), &error);
            if (bytes.empty() && !error.empty())
            {
                leftOut.push_back("the macros: " + error);
                changes.macros.clear();
            }
            else if (bytes.size() > board.macroBufferSize)
            {
                leftOut.push_back("the macros: they take " + std::to_string(bytes.size()) + " bytes, this keyboard holds " +
                                  std::to_string(board.macroBufferSize));
                changes.macros.clear();
            }
        }

        const qmk::KeycodeVersion version = keyboard.keycodeVersion;
        changes.tapDances = CompareSlots(now.tapDances, file.tapDances, "tap dance", "tap dances", leftOut);
        std::erase_if(changes.tapDances, [&](size_t slot) { return !vial::EncodeTapDance(*file.tapDances[slot], version); });
        changes.combos = CompareSlots(now.combos, file.combos, "combo", "combos", leftOut);
        std::erase_if(changes.combos, [&](size_t slot) { return !vial::EncodeCombo(*file.combos[slot], version); });
        changes.keyOverrides = CompareSlots(now.keyOverrides, file.keyOverrides, "key override", "key overrides", leftOut);
        std::erase_if(changes.keyOverrides,
                      [&](size_t slot) { return !vial::EncodeKeyOverride(*file.keyOverrides[slot], version); });
        changes.altRepeatKeys =
            CompareSlots(now.altRepeatKeys, file.altRepeatKeys, "alt repeat key", "alt repeat keys", leftOut);
        std::erase_if(changes.altRepeatKeys,
                      [&](size_t slot) { return !vial::EncodeAltRepeatKey(*file.altRepeatKeys[slot], version); });

        size_t lacked = 0;
        for (const auto& [qsid, value] : file.qmkSettings)
        {
            const auto held = now.qmkSettings.find(qsid);
            if (held == now.qmkSettings.end())
                ++lacked;
            else if (held->second != value)
                changes.qmkSettings.push_back(qsid);
        }
        if (lacked > 0)
            leftOut.push_back(Plural(lacked, "QMK setting", "QMK settings") + " this keyboard's firmware lacks");

        std::erase_if(changes.keys, [&](const KeyChange& key) { return !qmk::EncodeKeycode(key.then, version); });
        std::erase_if(changes.knobs, [&](const KnobChange& knob) { return !qmk::EncodeKeycode(knob.then, version); });

        return changes;
    }

    Task<void> WriteChanges(vial::Protocol& protocol, const Keyboard& keyboard, const BoardContents& board,
                            const SavedLayout& file, const LayoutChanges& changes, WriteProgress& progress)
    {
        const qmk::KeycodeVersion version = keyboard.keycodeVersion;
        const BoardReport&        report  = keyboard.report;
        progress = WriteProgress{ 0, changes.Count(), {}, changes };
        LayoutChanges& left = progress.left;

        for (const KeyChange& key : changes.keys)
        {
            const Keycode stored = co_await via::WriteKeycode(protocol, key.layer, key.row, key.column, key.then, version);
            if (stored != key.then)
                progress.notKept.push_back("layer " + std::to_string(key.layer) + ", row " + std::to_string(key.row) +
                                           ", column " + std::to_string(key.column));
            else
                std::erase_if(left.keys, [&](const KeyChange& other)
                              { return other.layer == key.layer && other.row == key.row && other.column == key.column; });
            ++progress.done;
        }

        for (const KnobChange& knob : changes.knobs)
        {
            const Keycode stored =
                co_await vial::WriteKnobTurn(protocol, report.isVial, knob.layer, knob.knob, knob.clockwise, knob.then, version);
            if (stored != knob.then)
                progress.notKept.push_back("knob " + std::to_string(knob.knob) + " on layer " + std::to_string(knob.layer));
            else
                std::erase_if(left.knobs, [&](const KnobChange& other)
                              { return other.layer == knob.layer && other.knob == knob.knob && other.clockwise == knob.clockwise; });
            ++progress.done;
        }

        if (changes.layoutOptions)
        {
            co_await protocol.SetKeyboardValue(via::KeyboardValue::LayoutOptions, *changes.layoutOptions);
            if (co_await protocol.GetKeyboardValue(via::KeyboardValue::LayoutOptions) != *changes.layoutOptions)
                progress.notKept.push_back("the layout options");
            else
                left.layoutOptions.reset();
            ++progress.done;
        }

        if (!changes.macros.empty())
        {
            std::vector<via::MacroActions> all;
            for (const auto& macro : board.layout.macros)
                all.push_back(*macro);
            for (size_t slot : changes.macros)
                all[slot] = *file.macros[slot];

            const via::MacroFormat     format = via::MacroFormatFor(report);
            const std::vector<uint8_t> bytes  = via::EncodeMacros(all, format);
            co_await protocol.WriteMacros(board.macroBytes, bytes, board.macroBufferSize);
            const std::vector<uint8_t> stored = co_await protocol.ReadMacros(report.macroCount, board.macroBufferSize);
            if (via::DecodeMacros(stored, report.macroCount, format) != all)
                progress.notKept.push_back("the macros");
            else
                left.macros.clear();
            ++progress.done;
        }

        for (size_t slot : changes.tapDances)
        {
            const vial::TapDanceEntry entry = *vial::EncodeTapDance(*file.tapDances[slot], version);
            co_await protocol.SetTapDance(static_cast<uint8_t>(slot), entry);
            if (co_await protocol.GetTapDance(static_cast<uint8_t>(slot)) != entry)
                progress.notKept.push_back("tap dance " + std::to_string(slot));
            else
                std::erase(left.tapDances, slot);
            ++progress.done;
        }
        for (size_t slot : changes.combos)
        {
            const vial::ComboEntry entry = *vial::EncodeCombo(*file.combos[slot], version);
            co_await protocol.SetCombo(static_cast<uint8_t>(slot), entry);
            if (co_await protocol.GetCombo(static_cast<uint8_t>(slot)) != entry)
                progress.notKept.push_back("combo " + std::to_string(slot));
            else
                std::erase(left.combos, slot);
            ++progress.done;
        }
        for (size_t slot : changes.keyOverrides)
        {
            const vial::KeyOverrideEntry entry = *vial::EncodeKeyOverride(*file.keyOverrides[slot], version);
            co_await protocol.SetKeyOverride(static_cast<uint8_t>(slot), entry);
            if (co_await protocol.GetKeyOverride(static_cast<uint8_t>(slot)) != entry)
                progress.notKept.push_back("key override " + std::to_string(slot));
            else
                std::erase(left.keyOverrides, slot);
            ++progress.done;
        }
        for (size_t slot : changes.altRepeatKeys)
        {
            const vial::AltRepeatKeyEntry entry = *vial::EncodeAltRepeatKey(*file.altRepeatKeys[slot], version);
            co_await protocol.SetAltRepeatKey(static_cast<uint8_t>(slot), entry);
            if (co_await protocol.GetAltRepeatKey(static_cast<uint8_t>(slot)) != entry)
                progress.notKept.push_back("alt repeat key " + std::to_string(slot));
            else
                std::erase(left.altRepeatKeys, slot);
            ++progress.done;
        }

        for (uint16_t qsid : changes.qmkSettings)
        {
            const uint32_t value = file.qmkSettings.at(qsid);
            co_await protocol.SetQmkSetting(qsid, value);
            if (co_await protocol.GetQmkSetting(qsid, QmkSettingWidth(qsid)) != value)
                progress.notKept.push_back("QMK setting " + std::to_string(qsid));
            else
                std::erase(left.qmkSettings, qsid);
            ++progress.done;
        }
    }
}
