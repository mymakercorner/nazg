// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycapLegend.h"

#include <algorithm>
#include <array>
#include <variant>

#include "adapters/qmk/NazgQmkKeycodes.h"

namespace nazg
{
    namespace
    {
        // The legend set: every standard key that is not a character position, with its words
        // (docs/research_material/short-forms.md, "Standard keys") and how it is placed. A key
        // not listed here -- any QMK feature -- is a command, printed by its QMK label for now.
        //
        // Cylindrical words are GMK's, mixed case. Spherical ones are SA's (ui-design.md,
        // "Spherical text rules"), listed only where they are not the cylindrical words in
        // capitals: capitals are wider, so SA abbreviates sooner and differently. Modifiers --
        // Ctrl, Shift, Alt, the GUI key -- are named in code, by the modifier-names setting.
        struct NamedEntry
        {
            std::string_view key;
            PlacementClass   placement = PlacementClass::Modifier;
            std::string_view full;               // empty: QMK's label
            std::string_view shortForm;
            std::string_view sphericalFull;      // empty: `full` in capitals
            std::string_view sphericalShort;     // empty: `shortForm` in capitals
            Placement        place = Placement::ByClass;
            ArrowDirection   arrow = ArrowDirection::None;

            // The numpad's second legend, what the key does with Num Lock off: text, or an
            // arrow (ui-design.md, "The numpad").
            std::string_view second;
            ArrowDirection   secondArrow = ArrowDirection::None;
        };

        constexpr NamedEntry c_Named[] = {
            // On cylindrical sets Esc sits with the function row, at the left, where other 1u
            // modifiers are centred both ways (Rico, from his GMK Dolch R5).
            { .key = "KC_ESC", .full = "Esc", .place = Placement::MiddleLeft },
            { .key = "KC_ENT", .full = "Enter" },
            { .key = "KC_BSPC", .full = "Backspace", .shortForm = "Bksp" },
            { .key = "KC_TAB", .full = "Tab" },
            { .key = "KC_SPC", .placement = PlacementClass::Blank, .full = "Space" },
            { .key = "KC_CAPS", .full = "Caps Lock", .shortForm = "Caps" },
            { .key = "KC_PSCR", .full = "Print Screen", .shortForm = "Prt Sc", .sphericalFull = "PRINT", .sphericalShort = "PRT" },
            { .key = "KC_SCRL", .full = "Scroll Lock", .shortForm = "Scr Lk", .sphericalFull = "SCRLK", .sphericalShort = "SCR" },
            { .key = "KC_PAUS", .full = "Pause" },
            { .key = "KC_INS", .full = "Insert", .shortForm = "Ins", .sphericalFull = "INS" },
            { .key = "KC_HOME", .full = "Home" },
            { .key = "KC_PGUP", .full = "Page Up", .shortForm = "Pg Up", .sphericalFull = "PGUP" },
            { .key = "KC_DEL", .full = "Delete", .shortForm = "Del", .sphericalFull = "DEL" },
            { .key = "KC_END", .full = "End" },
            { .key = "KC_PGDN", .full = "Page Down", .shortForm = "Pg Dn", .sphericalFull = "PGDN" },
            // NUM LOCK, not SA's NMLK: other spherical profiles print it whole, and Rico prefers it.
            { .key = "KC_NUM", .full = "Num Lock", .shortForm = "Num Lk", .sphericalShort = "NUM", .place = Placement::MiddleLeft },
            { .key = "KC_APP", .full = "Menu" },
            // POWER in capitals at the letters' weight is wider than a 1u key.
            { .key = "KC_KB_POWER", .full = "Power", .sphericalShort = "PWR" },
            { .key = "KC_EXEC", .full = "Execute", .shortForm = "Exec" },
            { .key = "KC_SLCT", .full = "Select", .shortForm = "Sel" },
            { .key = "KC_KB_VOLUME_UP", .full = "Vol +" },
            { .key = "KC_KB_VOLUME_DOWN", .full = "Vol −" },
            { .key = "KC_LCAP", .full = "Lock Caps", .shortForm = "L Caps" },
            { .key = "KC_LNUM", .full = "Lock Num", .shortForm = "L Num" },
            { .key = "KC_LSCR", .full = "Lock Scroll", .shortForm = "L Scr" },
            { .key = "KC_ERAS", .full = "Alt Erase", .shortForm = "Erase" },
            { .key = "KC_SYRQ", .full = "SysRq" },
            { .key = "KC_CNCL", .full = "Cancel", .shortForm = "Cncl" },
            { .key = "KC_CLR", .full = "Clear", .shortForm = "Clr" },
            { .key = "KC_RETN", .full = "Return", .shortForm = "Ret" },
            { .key = "KC_SEPR", .full = "Separator", .shortForm = "Sep" },
            { .key = "KC_CLAG", .full = "Clear Again", .shortForm = "Clr Agn" },
            { .key = "KC_CRSL", .full = "CrSel" },

            // Arrows are drawn, on both families; the words are for lists.
            { .key = "KC_UP", .placement = PlacementClass::Arrow, .full = "Up", .arrow = ArrowDirection::Up },
            { .key = "KC_DOWN", .placement = PlacementClass::Arrow, .full = "Down", .arrow = ArrowDirection::Down },
            { .key = "KC_LEFT", .placement = PlacementClass::Arrow, .full = "Left", .arrow = ArrowDirection::Left },
            { .key = "KC_RGHT", .placement = PlacementClass::Arrow, .full = "Right", .arrow = ArrowDirection::Right },

            // The numpad. Its operators print as mathematics on cylindrical sets, as GMK does --
            // ÷, ×, the true minus -- and as / and * on spherical ones, as SA does; the top row
            // at the left, centred vertically; the tall + and Enter centred.
            { .key = "KC_PSLS", .placement = PlacementClass::Numpad, .full = "÷", .sphericalFull = "/", .place = Placement::MiddleLeft },
            { .key = "KC_PAST", .placement = PlacementClass::Numpad, .full = "×", .sphericalFull = "*", .place = Placement::MiddleLeft },
            { .key = "KC_PMNS", .placement = PlacementClass::Numpad, .full = "−", .place = Placement::MiddleLeft },
            { .key = "KC_PPLS", .placement = PlacementClass::Numpad, .full = "+", .place = Placement::Centre },
            { .key = "KC_PENT", .full = "Enter", .place = Placement::Centre },
            { .key = "KC_PEQL", .placement = PlacementClass::Numpad, .full = "=" },
            { .key = "KC_PCMM", .placement = PlacementClass::Numpad, .full = "," },
            { .key = "KC_KP_EQUAL_AS400", .placement = PlacementClass::Numpad, .full = "=" },
            { .key = "KC_P0", .placement = PlacementClass::Numpad, .full = "0", .second = "Ins" },
            { .key = "KC_P1", .placement = PlacementClass::Numpad, .full = "1", .second = "End" },
            { .key = "KC_P2", .placement = PlacementClass::Numpad, .full = "2", .secondArrow = ArrowDirection::Down },
            { .key = "KC_P3", .placement = PlacementClass::Numpad, .full = "3", .second = "Pg Dn" },
            { .key = "KC_P4", .placement = PlacementClass::Numpad, .full = "4", .secondArrow = ArrowDirection::Left },
            { .key = "KC_P5", .placement = PlacementClass::Numpad, .full = "5" },
            { .key = "KC_P6", .placement = PlacementClass::Numpad, .full = "6", .secondArrow = ArrowDirection::Right },
            { .key = "KC_P7", .placement = PlacementClass::Numpad, .full = "7", .second = "Home" },
            { .key = "KC_P8", .placement = PlacementClass::Numpad, .full = "8", .secondArrow = ArrowDirection::Up },
            { .key = "KC_P9", .placement = PlacementClass::Numpad, .full = "9", .second = "Pg Up" },
            { .key = "KC_PDOT", .placement = PlacementClass::Numpad, .full = ".", .second = "Del" },
        };

        // The positions whose legend is a character the host layout types.
        constexpr std::string_view c_CharacterKeys[] = {
            "KC_A", "KC_B", "KC_C", "KC_D", "KC_E", "KC_F", "KC_G", "KC_H", "KC_I", "KC_J", "KC_K", "KC_L", "KC_M",
            "KC_N", "KC_O", "KC_P", "KC_Q", "KC_R", "KC_S", "KC_T", "KC_U", "KC_V", "KC_W", "KC_X", "KC_Y", "KC_Z",
            "KC_1", "KC_2", "KC_3", "KC_4", "KC_5", "KC_6", "KC_7", "KC_8", "KC_9", "KC_0",
            "KC_MINS", "KC_EQL", "KC_LBRC", "KC_RBRC", "KC_BSLS", "KC_NUHS", "KC_SCLN", "KC_QUOT", "KC_GRV",
            "KC_COMM", "KC_DOT", "KC_SLSH", "KC_NUBS",
        };

        const NamedEntry* FindNamed(std::string_view key)
        {
            for (const NamedEntry& entry : c_Named)
                if (entry.key == key)
                    return &entry;
            return nullptr;
        }

        bool IsCharacterKey(std::string_view key)
        {
            for (std::string_view character : c_CharacterKeys)
                if (character == key)
                    return true;
            return false;
        }

        std::string_view QmkLabel(std::string_view key)
        {
            // Names are the newest QMK spelling, so the latest table always has them --
            // except keycodes QMK has since removed, which fall back to the name itself.
            const QmkKeycode* keycode = FindQmkKeycodeByName(key, c_LatestQmkKeycodeVersion);
            return keycode != nullptr && keycode->label[0] != '\0' ? std::string_view(keycode->label) : key;
        }

        // Capitals for spherical sets. The standard keys' words are ASCII; a word with other
        // letters -- Eisū -- is left as written rather than half capitalised.
        std::string Capitals(std::string_view text)
        {
            std::string upper(text);
            if (std::any_of(upper.begin(), upper.end(), [](char c) { return (static_cast<unsigned char>(c) & 0x80) != 0; }))
                return upper;
            for (char& c : upper)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return upper;
        }

        // More than a character and an accent: a word.
        bool IsWord(std::string_view text)
        {
            const auto points = std::count_if(text.begin(), text.end(), [](char c)
                                              { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; });
            return points > 2;
        }

        KeycapLegend WordsLegend(PlacementClass placement, Words cylindrical, Words spherical)
        {
            KeycapLegend legend;
            legend.placement   = placement;
            legend.cylindrical = std::move(cylindrical);
            legend.spherical   = std::move(spherical);
            return legend;
        }

        // A character fit to print alone. Apple's logo (U+F8FF), on nine Mac layouts, is in no
        // bundled font: left off (Rico, 2026-10-03). A combining accent alone has nothing to sit
        // on: printed on a dotted circle, as Unicode's charts show one.
        std::string Printable(std::string_view text)
        {
            if (text.find("\xEF\xA3\xBF") != std::string_view::npos)
                return {};

            const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
            const bool combining =
                text.size() >= 2 && ((byte(0) == 0xCC && byte(1) >= 0x80) || (byte(0) == 0xCD && byte(1) <= 0xAF));
            return combining ? "◌" + std::string(text) : std::string(text);
        }

        KeycapLegend CharacterLegend(std::string_view key, const LegendContext& context)
        {
            // KC_NUHS and KC_NUBS a layout does not describe take KC_BSLS's legend
            // (keycodes.md): the OS treats them as the same key.
            const HostLegend* host = context.layout.Find(key);
            if (host == nullptr && (key == "KC_NUHS" || key == "KC_NUBS"))
                host = context.layout.Find("KC_BSLS");

            KeycapLegend legend;
            legend.placement = PlacementClass::Character;
            if (host == nullptr)
            {
                legend.plain = std::string(QmkLabel(key));
                return legend;
            }

            // A few positions type no character, and the table names them instead -- QMK's words:
            // Henkan, Hanja, "layer 3". Set in words, as a modifier is; anything else the
            // position has is left to hover.
            if (IsWord(host->plain))
                return WordsLegend(PlacementClass::Modifier, { std::string(host->plain), "" },
                                   { Capitals(host->plain), "" });

            const auto character = [](std::string_view text) { return IsWord(text) ? std::string() : Printable(text); };
            legend.plain   = character(host->plain);
            legend.shifted = character(host->shifted);
            legend.altgr   = character(host->altgr);
            if (context.layout.PrintsFourthLevel())
                legend.shiftAltgr = character(host->shiftAltgr);
            return legend;
        }

        // The modifiers with a keycode per side, and that side. Right Alt is not one: Alt Gr
        // already says which Alt it is.
        std::optional<KeySide> SideOfModifier(std::string_view key)
        {
            if (key == "KC_LCTL" || key == "KC_LSFT" || key == "KC_LALT" || key == "KC_LGUI")
                return KeySide::Left;
            if (key == "KC_RCTL" || key == "KC_RSFT" || key == "KC_RGUI")
                return KeySide::Right;
            return std::nullopt;
        }

        struct FamilyWords
        {
            Words cylindrical;
            Words spherical;
        };

        std::optional<FamilyWords> ModifierWords(std::string_view key, ModifierNames names)
        {
            const bool mac = names == ModifierNames::Mac;

            if (key == "KC_LCTL" || key == "KC_RCTL")
                return FamilyWords{ { "Control", "Ctrl" }, { "CTRL", "" } };
            if (key == "KC_LSFT" || key == "KC_RSFT")
                return FamilyWords{ { "Shift", "" }, { "SHIFT", "" } };
            if (key == "KC_LALT" || (key == "KC_RALT" && mac))
                return mac ? FamilyWords{ { "Option", "Opt" }, { "OPTION", "OPT" } }
                           : FamilyWords{ { "Alt", "" }, { "ALT", "" } };
            if (key == "KC_RALT")
                return FamilyWords{ { "Alt Gr", "" }, { "ALT GR", "" } };
            if (key == "KC_LGUI" || key == "KC_RGUI")
            {
                switch (names)
                {
                case ModifierNames::Mac:   return FamilyWords{ { "Cmd", "" }, { "CMD", "" } };
                case ModifierNames::Linux: return FamilyWords{ { "Super", "" }, { "SUPER", "" } };
                default:                   return FamilyWords{ { "Win", "" }, { "WIN", "" } };
                }
            }
            return std::nullopt;
        }

        // A modifier prints as its physical keycap would -- Ctrl on either side -- and names its
        // side only where its keycode's side is not where the key sits: Right Control on the
        // left half, "R CTRL" on spherical sets.
        void NameSide(FamilyWords& words, KeySide side)
        {
            const std::string_view sideName = side == KeySide::Left ? "Left" : "Right";
            const std::string_view initial  = side == KeySide::Left ? "L" : "R";

            Words& cylindrical = words.cylindrical;
            Words& spherical   = words.spherical;

            const std::string cylindricalShort =
                cylindrical.shortForm.empty() ? cylindrical.full : cylindrical.shortForm;
            const std::string sphericalShort = spherical.shortForm.empty() ? spherical.full : spherical.shortForm;

            cylindrical = { std::string(sideName) + " " + cylindrical.full,
                            std::string(initial) + " " + cylindricalShort };
            spherical   = { std::string(initial) + " " + sphericalShort, "" };
        }

        // A command, until command keys get their own legends: its QMK label, the same words on
        // both families -- command keys leave the family's case.
        KeycapLegend CommandLegend(std::string header, std::string words)
        {
            KeycapLegend legend = WordsLegend(PlacementClass::Command, { words, "" }, { words, "" });
            legend.header       = std::move(header);
            return legend;
        }

        KeycapLegend NamedLegend(std::string_view key, const LegendContext& context)
        {
            if (key == "KC_NO")
                return {};

            if (std::optional<FamilyWords> words = ModifierWords(key, context.names))
            {
                const std::optional<KeySide> side = SideOfModifier(key);
                if (side && *side != context.side)
                    NameSide(*words, *side);
                return WordsLegend(PlacementClass::Modifier, std::move(words->cylindrical),
                                   std::move(words->spherical));
            }

            if (const NamedEntry* entry = FindNamed(key))
            {
                const std::string full(entry->full.empty() ? QmkLabel(key) : entry->full);
                const std::string shortForm(entry->shortForm);

                KeycapLegend legend =
                    WordsLegend(entry->placement, { full, shortForm },
                                { entry->sphericalFull.empty() ? Capitals(full) : std::string(entry->sphericalFull),
                                  entry->sphericalShort.empty() ? Capitals(shortForm)
                                                                : std::string(entry->sphericalShort) });
                legend.place       = entry->place;
                legend.arrow       = entry->arrow;
                legend.second      = std::string(entry->second);
                legend.secondArrow = entry->secondArrow;

                // A layout that types another character there -- a comma for the numpad's dot --
                // keeps the numpad's placement and second legend. The space bar stays blank,
                // as on real sets, whatever a layout puts on it.
                const HostLegend* host = context.layout.Find(key);
                if (host != nullptr && legend.placement == PlacementClass::Numpad && !host->plain.empty())
                    legend.cylindrical = legend.spherical = { Printable(host->plain), "" };
                return legend;
            }

            // Whatever the host layout gives a legend -- the Japanese layout's KC_INT1 too.
            if (IsCharacterKey(key) || context.layout.Find(key) != nullptr)
                return CharacterLegend(key, context);

            const std::string_view label = QmkLabel(key);

            // The F-keys, and QMK's INT n and LANG n in GMK's mixed case.
            if (key.size() >= 4 && key.substr(0, 4) == "KC_F" && key.find_first_not_of("0123456789", 4) == std::string_view::npos)
                return WordsLegend(PlacementClass::FunctionRow, { std::string(label), "" }, { std::string(label), "" });
            if (label.substr(0, 4) == "INT ")
                return WordsLegend(PlacementClass::Modifier, { "Int " + std::string(label.substr(4)), "" },
                                   { std::string(label), "" });
            if (label.substr(0, 5) == "LANG ")
                return WordsLegend(PlacementClass::Modifier, { "Lang " + std::string(label.substr(5)), "" },
                                   { std::string(label), "" });

            // The other basic keycodes -- Help, Undo, Mute -- are keys a stock keyboard can have:
            // modifiers in words. Anything QMK adds is a command.
            const QmkKeycode* keycode = FindQmkKeycodeByName(key, c_LatestQmkKeycodeVersion);
            if (keycode != nullptr && std::string_view(keycode->group) == "basic")
                return WordsLegend(PlacementClass::Modifier, { std::string(label), "" }, { Capitals(label), "" });

            if (key == "KC_TRNS")
                return CommandLegend({}, "Trans");
            return CommandLegend({}, std::string(label));
        }

        // Modifiers as a header, the shortest that reads (short-forms.md, rule 4 and
        // "Parameterised keycodes"): one by its name, two by short words, Meh and Hyper by
        // theirs, three by initials. Sides merge: a QMK modifier set holds one side only.
        std::string ModsName(uint8_t mods, ModifierNames names)
        {
            const bool ctrl  = (mods & (Mod::LeftCtrl | Mod::RightCtrl)) != 0;
            const bool shift = (mods & (Mod::LeftShift | Mod::RightShift)) != 0;
            const bool alt   = (mods & (Mod::LeftAlt | Mod::RightAlt)) != 0;
            const bool gui   = (mods & (Mod::LeftGui | Mod::RightGui)) != 0;
            const int  count = ctrl + shift + alt + gui;

            const bool       mac     = names == ModifierNames::Mac;
            const bool       altGr   = mods == Mod::RightAlt && !mac;
            const char*      guiName = mac ? "Cmd" : names == ModifierNames::Linux ? "Super" : "Win";

            if (count == 4)
                return "Hyper";
            if (count == 3 && !gui)
                return "Meh";
            if (count == 1)
                return ctrl ? "Ctrl" : shift ? "Shift" : altGr ? "Alt Gr" : alt ? (mac ? "Option" : "Alt") : guiName;

            const char* shortGui = mac ? "Cmd" : names == ModifierNames::Linux ? "Sup" : "Win";
            std::string text;
            const auto  add = [&](bool on, const char* word, const char* initial)
            {
                if (!on)
                    return;
                if (!text.empty())
                    text += ' ';
                text += count == 2 ? word : initial;
            };
            add(ctrl, "Ctrl", "C");
            add(alt, mac ? "Opt" : "Alt", mac ? "O" : "A");
            add(shift, "Sft", "S");
            add(gui, shortGui, "G");
            return text;
        }

        std::string LayerName(uint8_t layer)
        {
            return "L" + std::to_string(layer);
        }

        struct Legender
        {
            const LegendContext& context;

            KeycapLegend operator()(const NamedKey& k) const { return NamedLegend(k.name, context); }

            KeycapLegend operator()(const ModifiedKey& k) const
            {
                // LSFT(KC_1) is simply "!" -- the character Shift gives on this host.
                const bool        shiftOnly = k.mods == Mod::LeftShift || k.mods == Mod::RightShift;
                const HostLegend* host      = context.layout.Find(k.key);
                if (shiftOnly && host != nullptr && !host->shifted.empty())
                {
                    KeycapLegend legend;
                    legend.placement = PlacementClass::Character;
                    legend.plain     = Printable(host->shifted);
                    return legend;
                }

                KeycapLegend legend = NamedLegend(k.key, context);
                legend.header       = ModsName(k.mods, context.names) + "+";
                return legend;
            }

            KeycapLegend operator()(const ModTapKey& k) const
            {
                KeycapLegend legend = NamedLegend(k.key, context);
                legend.header       = ModsName(k.mods, context.names);
                return legend;
            }

            KeycapLegend operator()(const LayerTapKey& k) const
            {
                KeycapLegend legend = NamedLegend(k.key, context);
                legend.header       = LayerName(k.layer);
                return legend;
            }

            KeycapLegend operator()(const SwapHandsTapKey& k) const
            {
                KeycapLegend legend = NamedLegend(k.key, context);
                legend.header       = "Swap";
                return legend;
            }

            KeycapLegend operator()(const LayerKey& k) const
            {
                static constexpr const char* c_Operations[] = { "Hold", "Toggle", "To", "Base", "Set base", "Once",
                                                                "Tap tog" };
                return CommandLegend(c_Operations[static_cast<size_t>(k.op)], LayerName(k.layer));
            }

            KeycapLegend operator()(const LayerModKey& k) const
            {
                return CommandLegend("Hold", LayerName(k.layer) + " " + ModsName(k.mods, context.names));
            }

            KeycapLegend operator()(const OneShotModKey& k) const
            {
                return CommandLegend("Once", ModsName(k.mods, context.names));
            }

            KeycapLegend operator()(const TapDanceKey& k) const
            {
                return CommandLegend("Dance", "TD " + std::to_string(k.index));
            }

            KeycapLegend operator()(const MacroKey& k) const
            {
                return CommandLegend("Macro", "M" + std::to_string(k.index));
            }

            KeycapLegend operator()(const UnknownKey& k) const
            {
                return CommandLegend({}, FormatKeycode(k));
            }
        };

        constexpr ModifierNames c_ModifierNames[] = { ModifierNames::Windows, ModifierNames::Mac,
                                                      ModifierNames::Linux };
    }

    const HostLegend* HostLayout::Find(std::string_view key) const noexcept
    {
        for (const HostLegend& legend : legends)
            if (legend.key == key)
                return &legend;

        return nullptr;
    }

    bool HostLayout::PrintsFourthLevel() const noexcept
    {
        return id == "bepo";
    }

    const HostLayout* FindHostLayout(std::string_view id) noexcept
    {
        for (const HostLayout& layout : HostLayouts())
            if (layout.id == id)
                return &layout;

        return nullptr;
    }

    const HostLayout& UsHostLayout() noexcept
    {
        // The table always has "us"; a failure here is a broken regeneration, caught by
        // the tests before it could reach anyone.
        return *FindHostLayout("us");
    }

    std::span<const ModifierNames> AllModifierNames()
    {
        return c_ModifierNames;
    }

    std::string_view IdOf(ModifierNames names)
    {
        switch (names)
        {
        case ModifierNames::Mac:   return "mac";
        case ModifierNames::Linux: return "linux";
        default:                   return "windows";
        }
    }

    std::string_view NameOf(ModifierNames names)
    {
        switch (names)
        {
        case ModifierNames::Mac:   return "Mac";
        case ModifierNames::Linux: return "Linux";
        default:                   return "Windows";
        }
    }

    std::optional<ModifierNames> ModifierNamesFromId(std::string_view id)
    {
        for (ModifierNames names : c_ModifierNames)
            if (IdOf(names) == id)
                return names;
        return std::nullopt;
    }

    const HostLayout& LegendSettings::Layout() const noexcept
    {
        // A saved id this build does not know falls back to US rather than failing.
        const HostLayout* found = FindHostLayout(hostLayout);
        return found != nullptr ? *found : UsHostLayout();
    }

    KeycapLegend LegendFor(const Keycode& keycode, const LegendContext& context)
    {
        return std::visit(Legender{ context }, keycode);
    }

    std::string CaptionOf(const KeycapLegend& legend)
    {
        std::string caption = legend.placement == PlacementClass::Character
                                  ? (legend.shifted.empty() ? legend.plain : legend.shifted + " " + legend.plain)
                                  : legend.cylindrical.full;
        if (!legend.header.empty())
            caption = caption.empty() ? legend.header : legend.header + " " + caption;
        return caption;
    }
}
