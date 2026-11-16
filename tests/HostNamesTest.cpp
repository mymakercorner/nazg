// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// QMK's international keycode names (ui/NazgHostNames.h): every name in the table reads as a
// keycode, a keycode finds its layout's name, and a name typed is read on the host layout chosen,
// else where every layout having it agrees (tester-feedback.md, 19).
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "ui/NazgHostNames.h"

#include "TestSupport.h"
#include "adapters/qmk/NazgQmkExpression.h"

#include <set>
#include <string>

namespace qmk = nazg::qmk;

namespace Mod = nazg::Mod;

namespace
{
    // TestSupport's Check() with a description built at run time.
    void Check(bool condition, const std::string& description)
    {
        ::Check(condition, description.c_str());
    }

    const nazg::HostLayout& Layout(const char* id)
    {
        return *nazg::FindHostLayout(id);
    }

    void TestTable()
    {
        std::printf("the table\n");

        // Every layout has names, and every expression reads back as a keycode, in the oldest
        // keycode version Nazg speaks as in the latest: basic keycodes and modifiers only.
        size_t names = 0;
        for (const nazg::HostLayout& layout : nazg::HostLayouts())
        {
            const auto table = nazg::HostNamesOf(layout.id);
            Check(!table.empty(), std::string(layout.id) + " has names");
            std::set<std::string_view> seen;
            for (const nazg::HostName& entry : table)
            {
                ++names;
                Check(seen.insert(entry.name).second, std::string(entry.name) + " once in " + std::string(layout.id));
                for (qmk::KeycodeVersion version : { qmk::KeycodeVersion::Legacy, qmk::c_LatestKeycodeVersion })
                    Check(qmk::ParseExpression(entry.expression, version).has_value(),
                          std::string(entry.name) + " = " + std::string(entry.expression) + " reads");
            }
        }
        Check(names == 6582, "6582 names, as generated");
        Check(nazg::HostNamesOf("nowhere").empty(), "an unknown layout has none");
    }

    void TestNameOf()
    {
        std::printf("a keycode's name\n");

        const nazg::HostLayout& german = Layout("german");
        Check(nazg::HostNameOf(nazg::ModifiedKey{ Mod::LeftShift, "KC_7" }, german) == "DE_SLSH", "LSFT(KC_7) is DE_SLSH");
        Check(nazg::HostNameOf(nazg::ModifiedKey{ Mod::RightAlt, "KC_Q" }, german) == "DE_AT", "RALT(KC_Q) is DE_AT");
        Check(nazg::HostNameOf(nazg::NamedKey{ "KC_Y" }, german) == "DE_Z", "KC_Y is DE_Z");
        Check(nazg::HostNameOf(nazg::NamedKey{ "KC_ENT" }, german).empty(), "Enter has no German name");
        Check(nazg::HostNameOf(nazg::ModifiedKey{ Mod::LeftAlt, "KC_L" }, Layout("german_mac_iso")) == "DE_AT",
              "on German (Mac ISO) DE_AT is LALT(KC_L)");
        Check(nazg::HostNameOf(nazg::ModifiedKey{ Mod::RightAlt, "KC_Q" }, Layout("german_mac_iso")).empty(),
              "QMK names Option's left side only");
        Check(nazg::HostNameOf(nazg::NamedKey{ "KC_Q" }, Layout("french")) == "FR_A", "KC_Q is FR_A on French");
        Check(nazg::HostNameOf(nazg::NamedKey{ "KC_A" }, Layout("canadian_french")) == "FR_A",
              "and KC_A is FR_A on Canadian French");
        Check(nazg::HostNameOf(nazg::ModifiedKey{ Mod::LeftShift, "KC_7" }, Layout("us")) == "KC_AMPR",
              "US's shifted names: KC_AMPR");
    }

    void TestReading()
    {
        std::printf("a name typed\n");

        const nazg::HostLayout& german = Layout("german");
        const nazg::HostLayout& us     = Layout("us");
        const auto              v      = qmk::c_LatestKeycodeVersion;

        nazg::HostNameReading own = nazg::ReadHostName(" de_slsh ", german, v);
        Check(own.keycode == nazg::Keycode{ nazg::ModifiedKey{ Mod::LeftShift, "KC_7" } } && own.note.empty() && own.known,
              "the host layout's own name, any case, any spaces: read, nothing to say");

        nazg::HostNameReading euro = nazg::ReadHostName("CH_EURO", german, v);
        Check(euro.keycode == nazg::Keycode{ nazg::ModifiedKey{ Mod::RightAlt, "KC_E" } }, "another layout's, all agreeing: read");
        Check(euro.note == "CH_EURO is RALT(KC_E) on Swiss De and Swiss Fr -- the host layout is German",
              "and said which, and the host layout: " + euro.note);

        nazg::HostNameReading a = nazg::ReadHostName("FR_A", us, v);
        Check(!a.keycode && a.known, "FR_A under US: known, not read");
        Check(a.note == "FR_A differs between layouts: KC_A on Canadian French; "
                        "KC_Q on French, French AFNOR and French (Mac ISO) -- choose one as the host layout",
              "the layouts named with what each makes of it: " + a.note);

        // The host layout's own meaning wins, whatever the others say.
        nazg::HostNameReading frenchA = nazg::ReadHostName("FR_A", Layout("canadian_french"), v);
        Check(frenchA.keycode == nazg::Keycode{ nazg::NamedKey{ "KC_A" } } && frenchA.note.empty(),
              "FR_A on Canadian French is its KC_A");

        nazg::HostNameReading nothing = nazg::ReadHostName("XX_NOPE", german, v);
        Check(!nothing.keycode && !nothing.known && nothing.note.empty(), "no layout's name: not known");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestTable();
    TestNameOf();
    TestReading();

    return TestResult();
}
