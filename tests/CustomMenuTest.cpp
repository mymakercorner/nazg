// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins a board's own menus (adapters/via/NazgViaCustomMenu.h, the parser's CustomMenu): what the
// definition's controls become, the width of each value, VIA's showIf conditions as pelpi reads
// them, the constraints tying sliders together as the VIA app resolves them, and the bytes on the
// wire -- the extra numbers after the value id sent before the value, and echoed before it.
//
// The menus are cut from real definitions in VIA's registry: cipulot's EC Theca, the FAve65S, the
// Lily (via-vial-commands.md, "A board's own menus on the wire").
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/via/NazgKeyboardDefinition.h"
#include "adapters/via/NazgViaCustomMenu.h"

#include "FakeDeviceChannel.h"
#include "TestSupport.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using nazg::CustomControl;
using nazg::CustomValues;
using nazg::KeyboardDefinition;
using nazg::Task;
using nazg::ViaProtocol;
using Type = nazg::CustomControl::Type;

namespace
{
    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the call did not complete synchronously");
        return task.TakeResult();
    }

    void Run(Task<void> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the call did not complete synchronously");
        task.TakeResult();
    }

    bool Starts(const std::vector<uint8_t>& frame, std::vector<uint8_t> bytes)
    {
        if (frame.size() != nazg::c_ViaReportSize)
            return false;
        for (size_t at = 0; at < bytes.size(); ++at)
            if (frame[at] != bytes[at])
                return false;
        for (size_t at = bytes.size(); at < frame.size(); ++at)
            if (frame[at] != 0)
                return false;
        return true;
    }

    KeyboardDefinition Parse(const char* menus)
    {
        const std::string text = std::string(R"({"matrix":{"rows":1,"cols":1},"layouts":{"keymap":[["0,0"]]},"menus":)") +
                                 menus + "}";
        return nazg::ParseDefinition(std::vector<uint8_t>(text.begin(), text.end()));
    }

    // The EC Theca's Switch Configuration, its DKS slot 0 cut to five lines, and the FAve65S's
    // lighting -- as their definitions write them.
    const char c_Menus[] = R"json([
        {"label":"Switch Configuration","content":[
            {"label":"Actuation","content":[
                {"label":"Mode","type":"dropdown","options":["APC","Rapid Trigger"],"content":["id_actuation_mode",0,1]},
                {"showIf":"{id_actuation_mode} == 0","content":[
                    {"label":"Actuation threshold (1-1023)","type":"range","options":[1,1023],
                     "constraints":[{"operator":">=","reference":"id_apc_release_threshold","offset":50,"onViolation":"push"}],
                     "content":["id_apc_actuation_threshold",0,2]},
                    {"label":"Release threshold (1-1023)","type":"range","options":[1,1023],
                     "constraints":[{"operator":"<=","reference":"id_apc_actuation_threshold","offset":-50,"onViolation":"push"}],
                     "content":["id_apc_release_threshold",0,3]},
                    {"label":"Apply & save changes","type":"button","options":[0],"content":["id_save_threshold_data",0,4]}]}]},
            {"label":"Calibration","content":[
                {"label":"Board Calibration","type":"toggle","content":["id_bottoming_calibration",0,8]}]}]},
        "qmk_rgblight",
        {"showIf":"{id_firmware_version} >= 6","label":"DKS","content":[
            {"label":"DKS General Settings","content":[
                {"label":"Enable Dynamic Keystrokes","type":"toggle","content":["id_dks_enabled",11,1]}]},
            {"showIf":"{id_dks_enabled} == 1","label":"DKS 0 - DKS(0)","content":[
                {"label":"Output 1 keycode","type":"keycode","content":["id_dks_keycode_0_0",11,3,0,0]},
                {"showIf":"{id_dks_keycode_0_0.0} != 0 || {id_dks_keycode_0_0.1} != 0",
                 "label":"Output 1 - Shallow press","type":"dropdown",
                 "options":["None","Press","Release","Tap","Release and press again"],"content":["id_dks_action[0][0][0]",11,4,0,0,0]}]}]},
        {"label":"Lighting","content":[{"label":"Backlight","content":[
            {"label":"Effect","type":"dropdown","content":["id_qmk_rgb_matrix_effect",3,2],
             "options":[["All Off",0],["Solid Color",1],["Gradient Up/Down",3]]},
            {"label":"Color","type":"color","content":["id_qmk_rgb_matrix_color",3,4]},
            {"label":"Firmware Version","type":"label","content":["id_firmware_version_text",0,26]},
            {"label":"Note","type":"label","content":["Changing protocol briefly restarts the USB connection."]},
            {"label":"Swap","type":"toggle","options":[[255,0],[254,0]],"content":["id_swap",0,9]},
            {"label":"Unknown","type":"color-palette","content":["id_palette",0,10]},
            {"label":"Bound to nothing","type":"range","options":[0,255],"content":["id_nothing"]}]}]}
    ])json";

    void TestParse()
    {
        std::printf("the controls, as the definition writes them\n");

        const KeyboardDefinition definition = Parse(c_Menus);
        const auto&              menus      = definition.customMenus;
        Check(menus.size() == 3, "three menus, the standard one's id aside");
        if (menus.size() != 3)
            return;

        const auto& actuation = menus[0].submenus.at(0);
        Check(menus[0].label == "Switch Configuration" && menus[0].submenus.size() == 2 && actuation.label == "Actuation" &&
                  actuation.controls.size() == 4,
              "a menu's submenus, and a slice's lines flattened into its submenu");
        const CustomControl& threshold = actuation.controls[1];
        Check(threshold.type == Type::Range && threshold.name == "id_apc_actuation_threshold" && threshold.channel == 0 &&
                  threshold.valueId == 2 && threshold.extra.empty() && threshold.minimum == 1 && threshold.maximum == 1023,
              "a range: its value's name, channel, value id and limits");
        Check(threshold.conditions == std::vector<std::string>{ "{id_actuation_mode} == 0" },
              "a slice's condition handed down to its lines");
        Check(threshold.constraints.size() == 1 && threshold.constraints[0].reference == "id_apc_release_threshold" &&
                  threshold.constraints[0].offset == 50 && threshold.constraints[0].push &&
                  threshold.constraints[0].operation == nazg::RangeConstraint::Operation::GreaterOrEqual,
              "a constraint: kept 50 above the release threshold, pushing it");
        Check(actuation.controls[3].type == Type::Button && actuation.controls[3].on == std::vector<uint8_t>{ 0 },
              "a button sends its one option");
        Check(actuation.controls[0].options.size() == 2 && actuation.controls[0].options[1].first == "Rapid Trigger" &&
                  actuation.controls[0].options[1].second == 1,
              "a dropdown of names: each worth its place");

        Check(menus[1].conditions == std::vector<std::string>{ "{id_firmware_version} >= 6" } &&
                  menus[1].submenus.at(1).conditions == std::vector<std::string>{ "{id_dks_enabled} == 1" },
              "a menu's and a submenu's own conditions");
        const CustomControl& output = menus[1].submenus.at(1).controls.at(0);
        Check(output.type == Type::Keycode && output.channel == 11 && output.valueId == 3 &&
                  output.extra == std::vector<uint8_t>{ 0, 0 },
              "the numbers after the value id: sent with it");

        const auto& lighting = menus[2].submenus.at(0).controls;
        Check(lighting.size() == 5, "a control of a type Nazg does not know, or bound to nothing, is left out");
        if (lighting.size() != 5)
            return;
        Check(lighting[0].options.size() == 3 && lighting[0].options[0].second == 0 && lighting[0].options[2].second == 3,
              "a dropdown of [name, value] pairs: each its value");
        Check(lighting[1].type == Type::Colour && lighting[1].channel == 3 && lighting[1].valueId == 4, "a colour");
        Check(lighting[2].type == Type::Label && lighting[2].isBound && lighting[3].type == Type::Label &&
                  !lighting[3].isBound && lighting[3].text == "Changing protocol briefly restarts the USB connection.",
              "a label: bound to a value the board sends, or fixed text");
        Check(lighting[4].off == std::vector<uint8_t>{ 255, 0 } && lighting[4].on == std::vector<uint8_t>{ 254, 0 },
              "a toggle's own two states, several bytes each");
    }

    void TestWidths()
    {
        std::printf("each value's width\n");

        const KeyboardDefinition definition = Parse(c_Menus);
        const auto&              actuation  = definition.customMenus.at(0).submenus.at(0).controls;
        const auto&              lighting   = definition.customMenus.at(2).submenus.at(0).controls;

        Check(nazg::ValueWidth(actuation.at(1)) == 2, "a range past 255: two bytes");
        Check(nazg::ValueWidth(actuation.at(0)) == 1, "a dropdown: one");
        Check(nazg::ValueWidth(definition.customMenus.at(1).submenus.at(1).controls.at(0)) == 2, "a keycode: two");
        Check(nazg::ValueWidth(lighting.at(1)) == 2, "a colour: hue and saturation");
        Check(nazg::ValueWidth(lighting.at(4)) == 2, "a toggle: its longest state");
        Check(nazg::ValueWidth(lighting.at(2)) == 29, "a bound label: what the frame holds after [0x08][ch][id]");

        Check(nazg::RangeValueOf(actuation.at(1), { 0x02, 0x58 }) == 600 &&
                  nazg::RangeBytes(actuation.at(1), 600) == std::vector<uint8_t>{ 0x02, 0x58 },
              "a two-byte range: big-endian");
        Check(nazg::IsToggledOn(lighting.at(4), { 254, 0 }) && !nazg::IsToggledOn(lighting.at(4), { 255, 0 }) &&
                  !nazg::IsToggledOn(lighting.at(4), { 7, 7 }),
              "a toggle is on at its on state only");
        Check(nazg::TextOf({ '1', '.', '6', 0, 'x' }) == "1.6", "a label's text ends at a zero");
    }

    void TestConditions()
    {
        std::printf("showIf, as pelpi reads it\n");

        const CustomValues values{ { "id_dks_enabled", { 1 } },
                                   { "id_dks_keycode_0_0", { 0x00, 0x1A } },
                                   { "id_threshold", { 0x02, 0x58 } },
                                   { "id_mode", { 2 } } };
        const auto holds = [&](const char* condition, uint32_t firmware = 6)
        { return nazg::ConditionHolds(condition, values, firmware); };

        Check(holds("{id_dks_enabled} == 1") && !holds("{id_dks_enabled} == 0"), "a value compared");
        Check(holds("{id_dks_keycode_0_0.0} != 0 || {id_dks_keycode_0_0.1} != 0"), "a byte other than the first, by .n");
        Check(holds("{id_threshold} == 2"), "a two-byte value's {name} is its first byte, the high one");
        Check(holds("{id_firmware_version} >= 6") && !holds("{id_firmware_version} >= 6", 5),
              "the firmware version: the whole number");
        Check(holds("{id_firmware_version} >= 300", 0x01020304), "all 32 bits of it");
        Check(!holds("{id_unknown} != 0") && holds("{id_unknown} == 0"), "a value not known is 0");
        Check(holds("{id_mode} == 1 || ({id_mode} == 2 && ({id_dks_enabled} == 1 || {id_unknown} == 9))"), "brackets");
        Check(holds("{id_mode} == 2 && {id_dks_enabled}") && !holds("!{id_dks_enabled}") && holds("!{id_unknown}"),
              "a value alone is true when not 0; ! before one term");
        Check(holds("{id_mode} > 1 && {id_mode} < 3 && {id_mode} >= 2 && {id_mode} <= 2"), "every comparison");
        Check(holds("{id_mode} == 1 || {id_mode} == 2 && {id_dks_enabled} == 1") &&
                  !holds("{id_mode} == 2 && {id_dks_enabled} == 0 || {id_mode} == 1"),
              "|| splits before &&");
        Check(holds("{broken") && holds("{id_mode} == == 2") && holds("") && holds("{id_mode} ~ 2"),
              "a condition Nazg cannot read holds: the line shows");
        Check(!nazg::IsReadableCondition("{id_mode} == == 2") && nazg::IsReadableCondition("!({id_mode} == 2)"),
              "and is known to be one");
        Check(nazg::ConditionsHold({ "{id_dks_enabled} == 1", "{id_mode} == 2" }, values, 0) &&
                  !nazg::ConditionsHold({ "{id_dks_enabled} == 1", "{id_mode} == 3" }, values, 0),
              "every condition must hold");

        Check(nazg::NamesIn("{id_dks_keycode_0_0.0} != 0 || {id_dks_keycode_0_0.1} != 0 && {id_x[1][2]} == 1") ==
                  std::vector<std::string>{ "id_dks_keycode_0_0", "id_x[1][2]" },
              "the values a condition reads, each once, brackets in names kept");
    }

    // The EC Theca's DKS slot 0 thresholds: shallow press (0) at least 50 over the shallow release
    // (3) and 50 under the deep release (2), the deep press (1) 50 over the deep release, each pushing.
    void TestConstraints()
    {
        std::printf("sliders tied together\n");

        const KeyboardDefinition definition = Parse(R"json([{"label":"DKS","content":[{"label":"DKS 0","content":[
            {"label":"Shallow press","type":"range","options":[1,1023],"content":["t0",11,2,0,0],"constraints":[
                {"operator":">=","reference":"t3","offset":50,"onViolation":"push"},
                {"operator":"<=","reference":"t2","offset":-50,"onViolation":"push"}]},
            {"label":"Deep press","type":"range","options":[1,1023],"content":["t1",11,2,0,1],"constraints":[
                {"operator":">=","reference":"t2","offset":50,"onViolation":"push"}]},
            {"label":"Deep release","type":"range","options":[1,1023],"content":["t2",11,2,0,2],"constraints":[
                {"operator":">=","reference":"t0","offset":50,"onViolation":"push"},
                {"operator":"<=","reference":"t1","offset":-50,"onViolation":"push"}]},
            {"label":"Shallow release","type":"range","options":[1,1023],"content":["t3",11,2,0,3],"constraints":[
                {"operator":"<=","reference":"t0","offset":-50,"onViolation":"push"}]},
            {"label":"Stopped","type":"range","options":[0,100],"content":["s",0,1],"constraints":[
                {"operator":"<","reference":"t3","offset":-250}]}]}]}])json");

        std::vector<const CustomControl*> ranges;
        for (const CustomControl& control : definition.customMenus.at(0).submenus.at(0).controls)
            ranges.push_back(&control);
        const auto at = [&](size_t index) { return *ranges.at(index); };

        CustomValues values{ { "t0", nazg::RangeBytes(at(0), 400) }, { "t1", nazg::RangeBytes(at(1), 800) },
                             { "t2", nazg::RangeBytes(at(2), 700) }, { "t3", nazg::RangeBytes(at(3), 300) },
                             { "s", { 20 } } };

        Check(nazg::ResolveRange(ranges, values, "t0", 500) == std::map<std::string, int>{ { "t0", 500 } },
              "a move within the rules changes that slider alone");
        Check(nazg::ResolveRange(ranges, values, "t0", 690) == std::map<std::string, int>{ { "t0", 690 }, { "t2", 740 } },
              "the shallow press past the deep release less 50 pushes it along");
        Check(nazg::ResolveRange(ranges, values, "t0", 790) ==
                  std::map<std::string, int>{ { "t0", 790 }, { "t1", 890 }, { "t2", 840 } },
              "and the deep press after it: each pushed slider's own rules too");
        Check(nazg::ResolveRange(ranges, values, "t3", 600) == std::map<std::string, int>{ { "t0", 650 }, { "t3", 600 } },
              "the shallow release up pushes the shallow press, which then breaks no rule of its own");
        Check(nazg::ResolveRange(ranges, values, "t1", 2000) == std::map<std::string, int>{ { "t1", 1023 } },
              "held within its own limits");
        Check(nazg::ResolveRange(ranges, values, "t1", 720) == std::map<std::string, int>{ { "t1", 720 }, { "t2", 670 } },
              "the deep press down pushes the deep release down");

        Check(nazg::RangeBounds(at(4), ranges, values) == std::pair<int, int>{ 0, 49 },
              "a constraint that does not push narrows the slider: under 300 - 250");
        Check(nazg::RangeBounds(at(0), ranges, values) == std::pair<int, int>{ 1, 1023 },
              "constraints that push leave the slider its limits");
    }

    void TestWire()
    {
        std::printf("the bytes on the wire\n");

        const KeyboardDefinition definition = Parse(c_Menus);
        const CustomControl&     threshold  = definition.customMenus.at(0).submenus.at(0).controls.at(1);
        const CustomControl&     output     = definition.customMenus.at(1).submenus.at(1).controls.at(0);
        const CustomControl&     button     = definition.customMenus.at(0).submenus.at(0).controls.at(3);
        const CustomControl&     firmware   = definition.customMenus.at(2).submenus.at(0).controls.at(2);

        FakeDeviceChannel channel;
        ViaProtocol       via(channel);

        channel.Reply({ 0x08, 0, 2, 0x02, 0x58 });
        Check(Run(nazg::ReadCustomValue(via, threshold)) == std::vector<uint8_t>{ 0x02, 0x58 } &&
                  Starts(channel.Requests().back(), { 0x08, 0, 2 }),
              "get [0x08][channel][value id]: the value after them, at its width");

        channel.Reply({ 0x08, 11, 3, 0, 0, 0x00, 0x1A });
        Check(Run(nazg::ReadCustomValue(via, output)) == std::vector<uint8_t>{ 0x00, 0x1A } &&
                  Starts(channel.Requests().back(), { 0x08, 11, 3, 0, 0 }),
              "the extra bytes sent after the value id, the value after their echo");

        channel.Reply({ 0x08, 0, 26, '1', '.', '6', 0 });
        Check(nazg::TextOf(Run(nazg::ReadCustomValue(via, firmware))) == "1.6", "a bound label's text");

        channel.Reply({ 0x07 });
        Run(nazg::WriteCustomValue(via, output, { 0x00, 0x04 }));
        Check(Starts(channel.Requests().back(), { 0x07, 11, 3, 0, 0, 0x00, 0x04 }),
              "set [0x07][channel][value id][extra][value]");

        channel.Reply({ 0x07 });
        Run(nazg::WriteCustomValue(via, button, button.on));
        Check(Starts(channel.Requests().back(), { 0x07, 0, 4, 0 }), "a button sends its value");

        channel.Reply({ 0x09 });
        Run(via.SaveCustomValues(11));
        Check(Starts(channel.Requests().back(), { 0x09, 11 }), "save [0x09][channel]");

        channel.ReplyUnhandled();
        bool refused = false;
        try
        {
            Run(nazg::ReadCustomValue(via, threshold));
        }
        catch (const nazg::ProtocolError&)
        {
            refused = true;
        }
        Check(refused, "a value the firmware does not have: refused, for the line to say");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestParse();
    TestWidths();
    TestConditions();
    TestConstraints();
    TestWire();

    return TestResult();
}
