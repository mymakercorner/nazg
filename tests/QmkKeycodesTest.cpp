// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The QMK keycode table. It is committed data produced by hand rather than generated in
// the build, so these checks are what stands between a bad regeneration and a board
// whose keys are silently misnamed.
//
// Two kinds of check: the table's own invariants (sorted, one keycode per value per
// version, per-version counts matching QMK's spec), and the specific traps from
// docs/research_material/keycodes.md, each pinned by value.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/qmk/NazgQmkKeycodes.h"

#include "TestSupport.h"

#include <iterator>
#include <set>
#include <string>
#include <string_view>

using nazg::FindQmkKeycode;
using nazg::FindQmkKeycodeByName;
using nazg::QmkKeycode;
using nazg::QmkKeycodeTable;
using nazg::QmkKeycodeVersion;
using nazg::QmkKeycodeVersionFromBcd;

namespace
{
    constexpr QmkKeycodeVersion c_AllVersions[] = {
        QmkKeycodeVersion::V0_0_1, QmkKeycodeVersion::V0_0_2, QmkKeycodeVersion::V0_0_3,
        QmkKeycodeVersion::V0_0_4, QmkKeycodeVersion::V0_0_5, QmkKeycodeVersion::V0_0_6,
        QmkKeycodeVersion::V0_0_7, QmkKeycodeVersion::V0_0_8, QmkKeycodeVersion::V0_0_9,
    };

    // Is this the keycode with this name? Tolerates nullptr so a failed lookup reads as
    // a failed check rather than a crash.
    bool Is(const QmkKeycode* keycode, std::string_view name)
    {
        return keycode != nullptr && name == keycode->name;
    }

    // The value a name has in a version, or -1 if it has none there.
    int ValueOf(std::string_view name, QmkKeycodeVersion version)
    {
        const QmkKeycode* keycode = FindQmkKeycodeByName(name, version);
        return keycode != nullptr ? keycode->value : -1;
    }

    void TestTableInvariants()
    {
        std::printf("table invariants\n");

        const auto table = QmkKeycodeTable();

        bool sorted = true;
        for (size_t i = 1; i < table.size(); ++i)
            if (table[i - 1].value > table[i].value)
                sorted = false;
        Check(sorted, "rows are sorted by value, which FindQmkKeycode's binary search needs");

        bool spansValid = true;
        for (const QmkKeycode& row : table)
            if (row.since >= row.removedIn)
                spansValid = false;
        Check(spansValid, "every row exists in at least one version");

        // Within one version, a value names one keycode and a name has one value.
        bool valuesUnique = true;
        bool namesUnique  = true;
        for (QmkKeycodeVersion version : c_AllVersions)
        {
            std::set<uint16_t>    values;
            std::set<std::string> names;

            for (const QmkKeycode& row : table)
            {
                if (!row.ExistsIn(version))
                    continue;

                valuesUnique &= values.insert(row.value).second;
                namesUnique  &= names.insert(row.name).second;
            }
        }
        Check(valuesUnique, "no value holds two keycodes in the same version");
        Check(namesUnique, "no name appears twice in the same version");

        bool labelsAscii = true;
        for (const QmkKeycode& row : table)
            for (const char* c = row.label; *c != '\0'; ++c)
                labelsAscii &= static_cast<unsigned char>(*c) < 0x80;
        Check(labelsAscii, "every label is plain ASCII");
    }

    // The per-version totals of QMK's merged spec. A regeneration that drops or
    // duplicates rows shows up here first.
    void TestVersionCounts()
    {
        std::printf("keycodes per version\n");

        const size_t expected[] = { 628, 695, 697, 719, 719, 732, 732, 736, 815 };

        for (size_t i = 0; i < std::size(c_AllVersions); ++i)
        {
            size_t count = 0;
            for (const QmkKeycode& row : QmkKeycodeTable())
                if (row.ExistsIn(c_AllVersions[i]))
                    ++count;

            const std::string description = std::string("0.0.") + std::to_string(i + 1) + " has " +
                                            std::to_string(expected[i]) + " keycodes";
            Check(count == expected[i], description.c_str());
        }
    }

    void TestLookups()
    {
        std::printf("lookups\n");

        const QmkKeycodeVersion latest = nazg::c_LatestQmkKeycodeVersion;

        const QmkKeycode* a = FindQmkKeycode(0x0004, latest);
        Check(Is(a, "KC_A") && std::string_view(a->label) == "A", "0x0004 is KC_A, labelled A");

        Check(Is(FindQmkKeycode(0x0028, latest), "KC_ENT"),
              "names are QMK's short keymap form, KC_ENT rather than KC_ENTER");
        Check(Is(FindQmkKeycode(0x0001, latest), "KC_TRNS"),
              "KC_TRNS, not the _______ alias that comes first in QMK's list");

        // Haptic: the gap in VIA's picker that started this. Unchanged since 0.0.1.
        bool hapticEverywhere = true;
        for (QmkKeycodeVersion version : c_AllVersions)
            hapticEverywhere &= Is(FindQmkKeycode(0x7C42, version), "HF_TOGG");
        Check(hapticEverywhere, "0x7C42 is HF_TOGG in every version");

        Check(FindQmkKeycode(0x0002, latest) == nullptr, "an unassigned value finds nothing");
        Check(FindQmkKeycode(0x5221, latest) == nullptr, "nor does MO(1) -- ranges are not table entries");

        Check(ValueOf("HF_TOGG", latest) == 0x7C42, "a name finds its value");
        Check(FindQmkKeycodeByName("NOT_A_KEYCODE", latest) == nullptr, "an unknown name finds nothing");

        Check(std::string_view(FindQmkKeycode(0x7110, latest)->label) == "C#1" &&
              std::string_view(FindQmkKeycode(0x7000, latest)->label) == "Swap LCtl<->Caps",
              "QMK's non-ASCII labels read naturally in ASCII");
        Check(std::string_view(FindQmkKeycode(0x0031, latest)->label) == "\\" &&
              std::string_view(FindQmkKeycode(0x700E, latest)->label) == "Swap \\<->Bspc",
              "the backslash key's label is one backslash, raw literal or not");
    }

    // The three places QMK gave an existing value a new meaning. A table keyed on value
    // alone gets each of these wrong for some board.
    void TestReusedValues()
    {
        std::printf("reused values\n");

        Check(Is(FindQmkKeycode(0x7110, QmkKeycodeVersion::V0_0_1), "MI_C"), "0x7110 is MI_C in 0.0.1");
        Check(Is(FindQmkKeycode(0x7110, QmkKeycodeVersion::V0_0_2), "MI_Cs1"), "and MI_Cs1 from 0.0.2");
        Check(ValueOf("MI_C", QmkKeycodeVersion::V0_0_2) == 0x7103, "MI_C itself moved to 0x7103");

        Check(Is(FindQmkKeycode(0x7C20, QmkKeycodeVersion::V0_0_5), "OU_AUTO"), "0x7C20 is OU_AUTO up to 0.0.5");
        Check(FindQmkKeycode(0x7C20, QmkKeycodeVersion::V0_0_6) == nullptr, "and unassigned from 0.0.6");
        Check(ValueOf("OU_AUTO", QmkKeycodeVersion::V0_0_6) == 0x7780, "where OU_AUTO lives at 0x7780");

        Check(Is(FindQmkKeycode(0x74F0, QmkKeycodeVersion::V0_0_8), "QK_STENO_BOLT"),
              "0x74F0 is QK_STENO_BOLT in 0.0.8");
        Check(Is(FindQmkKeycode(0x74F0, QmkKeycodeVersion::V0_0_9), "ST_X7"),
              "and ST_X7 in 0.0.9 -- a new keycode, not a rename");
        Check(ValueOf("QK_STENO_BOLT", QmkKeycodeVersion::V0_0_9) == -1,
              "QK_STENO_BOLT no longer exists in 0.0.9");
    }

    // Renames keep the name, so the same key reads the same on an old and a new board.
    void TestRenames()
    {
        std::printf("renames\n");

        Check(Is(FindQmkKeycode(0x7820, QmkKeycodeVersion::V0_0_1), "UG_TOGG") &&
              Is(FindQmkKeycode(0x7820, QmkKeycodeVersion::V0_0_9), "UG_TOGG"),
              "RGB_TOG (0.0.1) and UG_TOGG (0.0.9) are one keycode");
        Check(Is(FindQmkKeycode(0x00CD, QmkKeycodeVersion::V0_0_4), "MS_UP"),
              "KC_MS_UP before 0.0.5 is already MS_UP");
        Check(Is(FindQmkKeycode(0x7000, QmkKeycodeVersion::V0_0_1), "CL_SWAP"),
              "the 0.0.2 magic renames chain back to 0.0.1");
    }

    // Writing: a keycode newer than the board has no value there.
    void TestNotYetInVersion()
    {
        std::printf("not yet in this version\n");

        Check(ValueOf("QK_LLCK", QmkKeycodeVersion::V0_0_5) == -1, "Layer Lock cannot be stored on a 0.0.5 board");
        Check(ValueOf("QK_LLCK", QmkKeycodeVersion::V0_0_6) != -1, "but can from 0.0.6");
        Check(ValueOf("QK_REP", QmkKeycodeVersion::V0_0_2) == -1 &&
              ValueOf("QK_REP", QmkKeycodeVersion::V0_0_3) != -1,
              "Repeat Key arrives in 0.0.3");
    }

    void TestVersionFromBcd()
    {
        std::printf("version from BCD\n");

        Check(QmkKeycodeVersionFromBcd(0x00000001) == QmkKeycodeVersion::V0_0_1, "0x01 is 0.0.1");
        Check(QmkKeycodeVersionFromBcd(0x00000009) == QmkKeycodeVersion::V0_0_9, "0x09 is 0.0.9");
        Check(!QmkKeycodeVersionFromBcd(0x00000000).has_value(), "0.0.0 has no table");
        Check(!QmkKeycodeVersionFromBcd(0x00000010).has_value(), "0.0.10 (BCD 0x10) is newer than this build");
        Check(!QmkKeycodeVersionFromBcd(0x0000000A).has_value(), "0x0A is not valid BCD");
        Check(!QmkKeycodeVersionFromBcd(0x00010000).has_value(), "0.1.0 is newer than this build");

        Check(std::string_view(nazg::QmkKeycodeVersionName(QmkKeycodeVersion::V0_0_7)) == "0.0.7",
              "versions print as QMK writes them");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestTableInvariants();
    TestVersionCounts();
    TestLookups();
    TestReusedValues();
    TestRenames();
    TestNotYetInVersion();
    TestVersionFromBcd();

    return TestResult();
}
