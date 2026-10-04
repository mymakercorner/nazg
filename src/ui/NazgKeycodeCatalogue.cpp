// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycodeCatalogue.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <utility>
#include <variant>

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/qmk/NazgQmkKeycodes.h"
#include "ui/NazgBoardDescription.h"

namespace nazg
{
    namespace
    {
        // Builds the groups of one board: every keycode is checked against the board's version,
        // so a group lists only what the board can store, and an empty one is dropped later.
        class Builder
        {
        public:
            Builder(const Keyboard& keyboard, const LegendSettings& legends)
                : m_Version(keyboard.keycodeVersion),
                  m_Legends(legends),
                  m_Custom(CustomKeycodeWordsOf(keyboard)),
                  m_Lighting(LightingSystemsOf(keyboard))
            {
            }

            // A keycode the board can store, or nothing.
            void Add(std::vector<Keycode>& keycodes, const Keycode& keycode) const
            {
                if (EncodeQmkKeycode(keycode, m_Version))
                    keycodes.push_back(keycode);
            }

            // By QMK name; the NamedKey views the static table's name, so it never dangles.
            void AddNamed(std::vector<Keycode>& keycodes, std::string_view name) const
            {
                if (const QmkKeycode* row = FindQmkKeycodeByName(name, m_Version))
                    Add(keycodes, NamedKey{ row->name });
            }

            CatalogueGroup Group(std::string title, std::string more, std::initializer_list<std::string_view> names) const
            {
                CatalogueGroup group{ std::move(title), std::move(more), {} };
                for (std::string_view name : names)
                    AddNamed(group.keycodes, name);
                return group;
            }

            CatalogueGroup Group(std::string title, std::string more, const std::vector<Keycode>& keycodes) const
            {
                CatalogueGroup group{ std::move(title), std::move(more), {} };
                for (const Keycode& keycode : keycodes)
                    Add(group.keycodes, keycode);
                return group;
            }

            // A group per header, in the order the headers first come: its title is the header the
            // tiles then leave out -- "Ctrl↔Caps", whose keys read Swap, Unswap, On/Off.
            std::vector<CatalogueGroup> ByHeader(std::initializer_list<std::string_view> names, std::string_view more) const
            {
                std::vector<CatalogueGroup> groups;
                for (std::string_view name : names)
                {
                    std::vector<Keycode> one;
                    AddNamed(one, name);
                    if (one.empty())
                        continue;

                    const std::string header = LegendFor(one.front(), Context()).header.words.full;
                    auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.title == header; });
                    if (group == groups.end())
                    {
                        groups.push_back({ header, std::string(more), {} });
                        group = groups.end() - 1;
                    }
                    group->keycodes.push_back(one.front());
                }
                return groups;
            }

            LegendContext Context() const
            {
                return { m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither, m_Lighting, m_Custom };
            }

            QmkKeycodeVersion Version() const { return m_Version; }

        private:
            QmkKeycodeVersion     m_Version;
            const LegendSettings& m_Legends;
            std::vector<Words>    m_Custom;
            uint8_t               m_Lighting;
        };

        std::string Numbered(std::string_view prefix, int number)
        {
            return std::string(prefix) + std::to_string(number);
        }

        // The keycodes every board with a system gets, by the policy (ui-design.md, "Which lighting
        // keycodes a board gets"): Backlight's own; then, on new firmware, Underglow's and RGB
        // Matrix's apart; on old or unknown firmware one set -- the UG_* values -- for both, and the
        // RGB_M modes on old firmware only, where they work. LED Matrix is never offered: no
        // definition can declare it.
        std::vector<CatalogueGroup> LightingGroups(const Builder& builder, const Keyboard& keyboard)
        {
            using namespace LightingSystem;

            const uint8_t          systems  = LightingSystemsOf(keyboard);
            const LightingFirmware firmware = LightingFirmwareOf(keyboard);
            const bool             glow     = (systems & Underglow) != 0;
            const bool             matrix   = (systems & RgbMatrix) != 0;

            std::vector<CatalogueGroup> groups;
            if ((systems & Backlight) != 0)
                groups.push_back(builder.Group("Backlight", "", { "BL_TOGG", "BL_ON", "BL_OFF", "BL_STEP", "BL_UP", "BL_DOWN",
                                                                  "BL_BRTG" }));

            // The actions a set has; one the version lacks -- UG_ON, say -- is skipped.
            const auto set = [&](std::string title, std::string more, std::string_view prefix)
            {
                std::vector<Keycode> keycodes;
                for (std::string_view action : { "TOGG", "ON", "OFF", "NEXT", "PREV", "HUEU", "HUED", "SATU", "SATD", "VALU",
                                                 "VALD", "SPDU", "SPDD", "FLGN", "FLGP" })
                    builder.AddNamed(keycodes, std::string(prefix) + std::string(action));
                groups.push_back({ std::move(title), std::move(more), std::move(keycodes) });
            };

            if (firmware == LightingFirmware::New)
            {
                if (glow)
                    set("Underglow", "", "UG_");
                if (matrix)
                    set("RGB Matrix", "", "RM_");
            }
            else if (glow || matrix)
            {
                if (glow && matrix)
                    set("Underglow and RGB Matrix", "one set of keycodes drives both on this firmware", "UG_");
                else if (glow)
                    set("Underglow", "", "UG_");
                else
                    set("RGB Matrix", "through the underglow keycodes on this firmware", "UG_");

                // Old firmware only: dead on new, so not offered while unknown. Plain, Breathe,
                // Rainbow and Swirl reach an RGB Matrix too; the rest the underglow only.
                if (firmware == LightingFirmware::Old)
                    groups.push_back(glow ? builder.Group("Modes", "the RGB_M modes", { "RGB_M_P", "RGB_M_B", "RGB_M_R",
                                                                                        "RGB_M_SW", "RGB_M_SN", "RGB_M_K",
                                                                                        "RGB_M_X", "RGB_M_G", "RGB_M_T" })
                                          : builder.Group("Modes", "the RGB_M modes that reach an RGB Matrix",
                                                          { "RGB_M_P", "RGB_M_B", "RGB_M_R", "RGB_M_SW" }));
            }
            return groups;
        }
    }

    std::vector<CatalogueTab> BuildKeycodeCatalogue(const Keyboard& keyboard, const LegendSettings& legends)
    {
        const Builder      builder(keyboard, legends);
        const BoardReport& report = keyboard.report;
        const uint8_t      layers = keyboard.keymap.Layers();

        // On Vial the board says what it has, as vial-gui reads it; elsewhere nothing says, so
        // everything is offered.
        const auto absent = [&](std::string_view name)
        {
            if (!report.isVial)
                return false;
            if (name == "CW_TOGG")
                return !report.capsWord;
            if (name == "QK_LLCK")
                return !report.layerLock;
            if (name == "QK_REP" || name == "QK_AREP")
                return report.altRepeatKeyCount == 0;
            return false;
        };
        const auto named = [&](std::initializer_list<std::string_view> names)
        {
            std::vector<Keycode> keycodes;
            for (std::string_view name : names)
                if (!absent(name))
                    builder.AddNamed(keycodes, name);
            return keycodes;
        };

        std::vector<CatalogueTab> tabs;

        // Keys: what a stock keyboard has, Shift and a key in one click, the rarer HID keys last.
        {
            CatalogueTab tab{ "keys", "Keys", CommandCategory::None, {} };
            tab.groups.push_back(builder.Group("Letters", "", { "KC_A", "KC_B", "KC_C", "KC_D", "KC_E", "KC_F", "KC_G", "KC_H",
                                                               "KC_I", "KC_J", "KC_K", "KC_L", "KC_M", "KC_N", "KC_O", "KC_P",
                                                               "KC_Q", "KC_R", "KC_S", "KC_T", "KC_U", "KC_V", "KC_W", "KC_X",
                                                               "KC_Y", "KC_Z" }));
            tab.groups.push_back(builder.Group("Digits", "", { "KC_1", "KC_2", "KC_3", "KC_4", "KC_5", "KC_6", "KC_7", "KC_8",
                                                              "KC_9", "KC_0" }));
            tab.groups.push_back(builder.Group("Punctuation", "the last two are ISO's",
                                               { "KC_GRV", "KC_MINS", "KC_EQL", "KC_LBRC", "KC_RBRC", "KC_BSLS", "KC_SCLN",
                                                 "KC_QUOT", "KC_COMM", "KC_DOT", "KC_SLSH", "KC_NUBS", "KC_NUHS" }));

            // VIA's symbols, for a symbol layer: S(KC_1) is "!" in one click.
            std::vector<Keycode> shifted;
            for (std::string_view name : { "KC_1", "KC_2", "KC_3", "KC_4", "KC_5", "KC_6", "KC_7", "KC_8", "KC_9", "KC_0",
                                           "KC_GRV", "KC_MINS", "KC_EQL", "KC_LBRC", "KC_RBRC", "KC_BSLS", "KC_SCLN",
                                           "KC_QUOT", "KC_COMM", "KC_DOT", "KC_SLSH" })
                if (const QmkKeycode* row = FindQmkKeycodeByName(name, builder.Version()))
                    shifted.push_back(ModifiedKey{ Mod::LeftShift, row->name });
            tab.groups.push_back(builder.Group("Shifted", "Shift and the key, in one", shifted));

            tab.groups.push_back(builder.Group("Editing and spaces", "", { "KC_ESC", "KC_TAB", "KC_ENT", "KC_BSPC", "KC_SPC",
                                                                          "KC_DEL", "KC_INS", "KC_CAPS" }));
            tab.groups.push_back(builder.Group("Modifiers", "", { "KC_LSFT", "KC_LCTL", "KC_LALT", "KC_LGUI", "KC_RSFT",
                                                                 "KC_RCTL", "KC_RALT", "KC_RGUI", "KC_APP" }));
            tab.groups.push_back(builder.Group("Navigation", "", { "KC_HOME", "KC_END", "KC_PGUP", "KC_PGDN", "KC_UP",
                                                                  "KC_DOWN", "KC_LEFT", "KC_RGHT", "KC_PSCR", "KC_SCRL",
                                                                  "KC_PAUS" }));

            std::vector<Keycode> function;
            for (int key = 1; key <= 24; ++key)
                builder.AddNamed(function, Numbered("KC_F", key));
            tab.groups.push_back({ "Function keys", "", std::move(function) });

            tab.groups.push_back(builder.Group("Numpad", "", { "KC_NUM", "KC_PSLS", "KC_PAST", "KC_PMNS", "KC_PPLS", "KC_PENT",
                                                              "KC_P1", "KC_P2", "KC_P3", "KC_P4", "KC_P5", "KC_P6", "KC_P7",
                                                              "KC_P8", "KC_P9", "KC_P0", "KC_PDOT", "KC_PEQL", "KC_PCMM" }));

            std::vector<Keycode> international;
            for (int key = 1; key <= 9; ++key)
                builder.AddNamed(international, Numbered("KC_INT", key));
            for (int key = 1; key <= 9; ++key)
                builder.AddNamed(international, Numbered("KC_LNG", key));
            tab.groups.push_back({ "International", "Japanese and Korean keys first", std::move(international) });

            tab.groups.push_back(builder.Group("Rarer keys", "HID keys few systems act on",
                                               { "KC_UNDO", "KC_CUT", "KC_COPY", "KC_PSTE", "KC_FIND", "KC_AGIN", "KC_STOP",
                                                 "KC_SLCT", "KC_MENU", "KC_EXEC", "KC_HELP", "KC_KB_POWER", "KC_KB_MUTE",
                                                 "KC_KB_VOLUME_UP", "KC_KB_VOLUME_DOWN", "KC_LCAP", "KC_LNUM", "KC_LSCR",
                                                 "KC_SYRQ", "KC_ERAS", "KC_CNCL", "KC_CLR", "KC_PRIR", "KC_RETN", "KC_SEPR",
                                                 "KC_OUT", "KC_OPER", "KC_CLAG", "KC_CRSL", "KC_EXSL", "KC_KP_EQUAL_AS400" }));
            tab.groups.push_back(builder.Group("Nothing", "", { "KC_TRNS", "KC_NO" }));
            tabs.push_back(std::move(tab));
        }

        // Layers: the seven operations for each layer the board has.
        {
            CatalogueTab tab{ "layers", "Layers", CommandCategory::Behaviour, {} };
            const struct
            {
                LayerOp     op;
                const char* title;
                const char* more;
            } c_Operations[] = {
                { LayerOp::Momentary, "Hold", "hold the layer while the key is down" },
                { LayerOp::Toggle, "Toggle", "turn the layer on or off" },
                { LayerOp::To, "To", "go to the layer, turning the others off" },
                { LayerOp::TapToggle, "Tap tog", "hold, or tap several times to toggle" },
                { LayerOp::OneShot, "Once", "the layer for the next key only" },
                { LayerOp::Default, "Base", "make it the default layer, until unplugged" },
                { LayerOp::PersistentDefault, "Set base", "make it the default layer, kept after unplugging" },
            };
            for (const auto& operation : c_Operations)
            {
                std::vector<Keycode> keycodes;
                for (uint8_t layer = 0; layer < layers; ++layer)
                    keycodes.push_back(LayerKey{ operation.op, layer });
                tab.groups.push_back(builder.Group(operation.title, operation.more, keycodes));
            }

            std::vector<Keycode> oneShot;
            for (uint8_t mods : { Mod::LeftCtrl, Mod::LeftShift, Mod::LeftAlt, Mod::LeftGui, Mod::RightCtrl, Mod::RightShift,
                                  Mod::RightAlt, Mod::RightGui, uint8_t(Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt),
                                  uint8_t(Mod::LeftCtrl | Mod::LeftShift | Mod::LeftAlt | Mod::LeftGui) })
                oneShot.push_back(OneShotModKey{ mods });
            tab.groups.push_back(builder.Group("One-shot modifiers", "held for the next key only", oneShot));
            tab.groups.push_back(builder.Group("Layer lock and tri layer", "", named({ "QK_LLCK", "TL_LOWR", "TL_UPPR" })));
            tabs.push_back(std::move(tab));
        }

        // Media & mouse: what goes to the computer (Rico: one tab, as Vial's "App, Media and Mouse").
        {
            CatalogueTab tab{ "media", "Media & mouse", CommandCategory::Host, {} };
            tab.groups.push_back(builder.Group("Media", "", { "KC_MPLY", "KC_MSTP", "KC_MPRV", "KC_MNXT", "KC_MRWD", "KC_MFFD",
                                                             "KC_EJCT", "KC_MSEL" }));
            tab.groups.push_back(builder.Group("Volume", "", { "KC_MUTE", "KC_VOLD", "KC_VOLU" }));
            tab.groups.push_back(builder.Group("Screen", "", { "KC_BRID", "KC_BRIU" }));
            tab.groups.push_back(builder.Group("System", "", { "KC_PWR", "KC_SLEP", "KC_WAKE" }));
            tab.groups.push_back(builder.Group("Apps", "", { "KC_CALC", "KC_MAIL", "KC_MYCM", "KC_CPNL", "KC_ASST", "KC_MCTL",
                                                            "KC_LPAD" }));
            tab.groups.push_back(builder.Group("Web", "", { "KC_WBAK", "KC_WFWD", "KC_WREF", "KC_WSTP", "KC_WHOM", "KC_WSCH",
                                                           "KC_WFAV" }));
            tab.groups.push_back(builder.Group("Mouse move", "", { "MS_UP", "MS_DOWN", "MS_LEFT", "MS_RGHT" }));
            tab.groups.push_back(builder.Group("Mouse buttons", "", { "MS_BTN1", "MS_BTN2", "MS_BTN3", "MS_BTN4", "MS_BTN5",
                                                                     "MS_BTN6", "MS_BTN7", "MS_BTN8" }));
            tab.groups.push_back(builder.Group("Mouse wheel", "", { "MS_WHLU", "MS_WHLD", "MS_WHLL", "MS_WHLR" }));
            tab.groups.push_back(builder.Group("Mouse speed", "while held", { "MS_ACL0", "MS_ACL1", "MS_ACL2" }));
            tabs.push_back(std::move(tab));
        }

        tabs.push_back({ "lighting", "Lighting", CommandCategory::Board, LightingGroups(builder, keyboard) });

        // Features: QMK features that change how keys behave.
        {
            CatalogueTab tab{ "features", "Features", CommandCategory::Behaviour, {} };
            tab.groups.push_back(builder.Group("Keys that behave", "",
                                               named({ "CW_TOGG", "QK_REP", "QK_AREP", "QK_GESC", "QK_LEAD", "QK_LOCK" })));
            tab.groups.push_back(builder.Group("Space Cadet", "a modifier held, a bracket tapped",
                                               { "SC_LSPO", "SC_RSPC", "SC_LCPO", "SC_RCPC", "SC_LAPO", "SC_RAPC", "SC_SENT" }));
            tab.groups.push_back(builder.Group("Auto Shift", "", { "AS_TOGG", "AS_ON", "AS_OFF", "AS_UP", "AS_DOWN", "AS_RPT" }));
            tab.groups.push_back(builder.Group("Autocorrect", "", { "AC_TOGG", "AC_ON", "AC_OFF" }));
            tab.groups.push_back(builder.Group("Combos", "", { "CM_TOGG", "CM_ON", "CM_OFF" }));
            tab.groups.push_back(builder.Group("Key overrides", "", { "KO_TOGG", "KO_ON", "KO_OFF" }));
            tab.groups.push_back(builder.Group("One shot keys", "", { "OS_TOGG", "OS_ON", "OS_OFF" }));
            tab.groups.push_back(builder.Group("Tap term", "", { "DT_UP", "DT_DOWN", "DT_PRNT" }));
            tab.groups.push_back(builder.Group("Swap hands", "", { "SH_TOGG", "SH_ON", "SH_OFF", "SH_MON", "SH_MOFF", "SH_TT",
                                                                  "SH_OS" }));
            tab.groups.push_back(builder.Group("Secure", "unlocking the board for its configurator",
                                               { "SE_LOCK", "SE_UNLK", "SE_TOGG", "SE_REQ" }));
            tabs.push_back(std::move(tab));
        }

        // Macros: keys that play what is set elsewhere, as many as the board has -- tap dances
        // here too, by the macros (Rico), though drawn in Behaviour's violet.
        {
            CatalogueTab         tab{ "macros", "Macros", CommandCategory::Host, {} };
            std::vector<Keycode> macros;
            for (uint8_t index = 0; index < report.macroCount; ++index)
                macros.push_back(MacroKey{ index });
            tab.groups.push_back(builder.Group("Macros", "recorded in the Macros section", macros));

            std::vector<Keycode> dances;
            for (uint8_t index = 0; index < report.tapDanceCount; ++index)
                dances.push_back(TapDanceKey{ index });
            tab.groups.push_back(builder.Group("Tap dances", "set in the Tap dance section", dances));

            tab.groups.push_back(builder.Group("Dynamic macros", "recorded on the keyboard",
                                               { "DM_REC1", "DM_REC2", "DM_RSTP", "DM_PLY1", "DM_PLY2" }));
            tabs.push_back(std::move(tab));
        }

        // Special: the settings kept on the board (Rico: VIA's word) -- QMK's Magic keycodes, a
        // group per swap, then audio, haptic, the Unicode input mode, output and Bluetooth.
        {
            CatalogueTab tab{ "special", "Special", CommandCategory::Board, {} };
            for (CatalogueGroup& group :
                 builder.ByHeader({ "CL_SWAP", "CL_NORM", "CL_TOGG", "CG_SWAP", "CG_NORM", "CG_TOGG", "CG_LSWP", "CG_LNRM",
                                    "CG_RSWP", "CG_RNRM", "AG_SWAP", "AG_NORM", "AG_TOGG", "AG_LSWP", "AG_LNRM", "AG_RSWP",
                                    "AG_RNRM", "EC_SWAP", "EC_NORM", "EC_TOGG", "GE_SWAP", "GE_NORM", "BS_SWAP", "BS_NORM",
                                    "BS_TOGG", "CL_CAPS", "CL_CTRL", "GU_TOGG", "GU_ON", "GU_OFF", "NK_TOGG", "NK_ON", "NK_OFF",
                                    "EH_LEFT", "EH_RGHT" },
                                  "kept on the board until changed again"))
                tab.groups.push_back(std::move(group));
            tab.groups.push_back(builder.Group("Audio", "", { "AU_TOGG", "AU_ON", "AU_OFF", "AU_NEXT", "AU_PREV" }));
            tab.groups.push_back(builder.Group("Clicky", "", { "CK_TOGG", "CK_ON", "CK_OFF", "CK_UP", "CK_DOWN", "CK_RST" }));
            tab.groups.push_back(builder.Group("Music mode", "", { "MU_TOGG", "MU_ON", "MU_OFF", "MU_NEXT" }));
            tab.groups.push_back(builder.Group("Velocikey", "the lighting's speed follows the typing", { "VK_TOGG" }));
            tab.groups.push_back(builder.Group("Haptic", "", { "HF_TOGG", "HF_ON", "HF_OFF", "HF_NEXT", "HF_PREV", "HF_FDBK",
                                                              "HF_BUZZ", "HF_CONT", "HF_CONU", "HF_COND", "HF_DWLU", "HF_DWLD",
                                                              "HF_RST" }));
            tab.groups.push_back(builder.Group("Unicode", "the input mode the computer uses",
                                               { "UC_NEXT", "UC_PREV", "UC_WIN", "UC_WINC", "UC_MAC", "UC_LINX", "UC_BSD",
                                                 "UC_EMAC" }));
            tab.groups.push_back(builder.Group("Output", "USB or wireless", { "OU_AUTO", "OU_USB", "OU_BT", "OU_2P4G", "OU_NONE",
                                                                             "OU_NEXT", "OU_PREV" }));
            tab.groups.push_back(builder.Group("Bluetooth", "", { "BT_NEXT", "BT_PREV", "BT_UNPR", "BT_PRF1", "BT_PRF2",
                                                                 "BT_PRF3", "BT_PRF4", "BT_PRF5" }));
            tabs.push_back(std::move(tab));
        }

        // Custom, on every board (Rico): the keys the firmware's own code handles -- the board's,
        // by the names its definition gives them or else numbered, then the keymap's.
        {
            CatalogueTab         tab{ "custom", "Custom", CommandCategory::Board, {} };
            const size_t         namedCount = std::min<size_t>(keyboard.definition.customKeycodes.size(), 32);
            std::vector<Keycode> board;
            for (size_t index = 0; index < (namedCount > 0 ? namedCount : 32); ++index)
                builder.AddNamed(board, Numbered("QK_KB_", static_cast<int>(index)));
            tab.groups.push_back(namedCount > 0 ? CatalogueGroup{ "This board's own keys", "named by its definition", board }
                                                : CatalogueGroup{ "Board", "what the board's firmware code makes of them",
                                                                  board });

            std::vector<Keycode> user;
            for (int index = 0; index < 32; ++index)
                builder.AddNamed(user, Numbered("QK_USER_", index));
            tab.groups.push_back({ "User", "what a keymap's own code makes of them", std::move(user) });
            tabs.push_back(std::move(tab));
        }

        // Devices: the board acting as another device.
        {
            CatalogueTab tab{ "devices", "Devices", CommandCategory::Host, {} };
            tab.groups.push_back(builder.Group("MIDI", "the basic set; the rest by search or the expression box",
                                               { "MI_ON", "MI_OFF", "MI_TOGG", "MI_C", "MI_Cs", "MI_D", "MI_Ds", "MI_E", "MI_F",
                                                 "MI_Fs", "MI_G", "MI_Gs", "MI_A", "MI_As", "MI_B", "MI_OCTD", "MI_OCTU",
                                                 "MI_TRSD", "MI_TRSU", "MI_VELD", "MI_VELU", "MI_CHND", "MI_CHNU", "MI_AOFF",
                                                 "MI_SUST" }));
            tab.groups.push_back(builder.Group("Sequencer", "", { "SQ_ON", "SQ_OFF", "SQ_TOGG", "SQ_TMPD", "SQ_TMPU", "SQ_RESD",
                                                                 "SQ_RESU", "SQ_SALL", "SQ_SCLR" }));
            tab.groups.push_back(builder.Group(
                "Steno", "the protocol, then the chord keys",
                { "QK_STENO_BOLT", "QK_STENO_GEMINI", "QK_STENO_COMB", "ST_FN", "ST_N1", "ST_N2", "ST_N3", "ST_N4", "ST_N5",
                  "ST_N6", "ST_N7", "ST_N8", "ST_N9", "ST_NA", "ST_NB", "ST_NC", "ST_S1", "ST_S2", "ST_S3", "ST_TL", "ST_KL",
                  "ST_PL", "ST_WL", "ST_HL", "ST_RL", "ST_A", "ST_O", "ST_ST1", "ST_ST2", "ST_ST3", "ST_ST4", "ST_E", "ST_U",
                  "ST_FR", "ST_RR", "ST_PR", "ST_BR", "ST_LR", "ST_GR", "ST_TR", "ST_SR", "ST_DR", "ST_ZR" }));

            std::vector<Keycode> joystick;
            for (int button = 0; button < 32; ++button)
                builder.AddNamed(joystick, Numbered("JS_", button));
            tab.groups.push_back({ "Joystick", "", std::move(joystick) });

            std::vector<Keycode> programmable;
            for (int button = 1; button <= 32; ++button)
                builder.AddNamed(programmable, Numbered("PB_", button));
            tab.groups.push_back({ "Programmable buttons", "", std::move(programmable) });
            tabs.push_back(std::move(tab));
        }

        // Firmware, last and apart: the keys that can hurt.
        tabs.push_back({ "firmware", "Firmware", CommandCategory::Firmware,
                         { builder.Group("Firmware", "rebooting, erasing settings",
                                         { "QK_BOOT", "QK_RBT", "EE_CLR", "DB_TOGG", "QK_MAKE" }) } });

        // A group the board has nothing for goes, and a tab left with none.
        for (CatalogueTab& tab : tabs)
            std::erase_if(tab.groups, [](const CatalogueGroup& group) { return group.keycodes.empty(); });
        std::erase_if(tabs, [](const CatalogueTab& tab) { return tab.groups.empty(); });
        return tabs;
    }

    std::string SearchTextOf(const Keycode& keycode, QmkKeycodeVersion version, const LegendContext& context)
    {
        std::string text = FormatKeycode(keycode);
        if (const auto* named = std::get_if<NamedKey>(&keycode))
            if (const QmkKeycode* row = FindQmkKeycodeByName(named->name, version); row != nullptr && row->label[0] != '\0')
                text += std::string(" ") + row->label;

        const KeycapLegend legend = LegendFor(keycode, context);
        for (const std::string* words : { &legend.plain, &legend.shifted, &legend.cylindrical.full,
                                          &legend.cylindrical.shortForm, &legend.hold.words.full, &legend.header.words.full })
            if (!words->empty())
                text += " " + *words;

        std::transform(text.begin(), text.end(), text.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return text;
    }

    bool MatchesSearch(std::string_view text, std::string_view query)
    {
        const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        while (!query.empty() && isSpace(query.front()))
            query.remove_prefix(1);
        while (!query.empty() && isSpace(query.back()))
            query.remove_suffix(1);
        if (query.empty())
            return true;

        const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        return std::search(text.begin(), text.end(), query.begin(), query.end(),
                           [&](char a, char b) { return a == lower(b); }) != text.end();
    }
}
