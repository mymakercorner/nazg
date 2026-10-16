// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// settings.json: the settings there and back, what a document lacks or holds wrong, an earlier
// build's imgui.ini read once and left alone, keys a newer build wrote kept, a newer format not
// written over, and an unreadable file kept aside -- against files in a temporary folder.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "settings/NazgSettings.h"

#include "files/NazgFiles.h"

#include "TestSupport.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

using nazg::AppSettings;
using nazg::SettingsFile;
using Json = nlohmann::ordered_json;

namespace
{
    // The [Nazg][Settings] section of a real imgui.ini (Rico's, 2026-10-16), behind one of
    // ImGui's own sections, which is not Nazg's.
    constexpr char c_Ini[] = "[Window][Keyboard]\r\n"
                             "Pos=17,125\r\n"
                             "Size=1658,861\r\n"
                             "\r\n"
                             "[Nazg][Settings]\r\n"
                             "HostLayout=us_international\r\n"
                             "ModifierNames=windows\r\n"
                             "Theme=light\r\n"
                             "Keycaps=outlined\r\n"
                             "Legends=cylindrical\r\n"
                             "AdvancedTools=1\r\n"
                             "ViaDefinition=C:/boards/phoenix.json\r\n"
                             "Window=91,101,2450,1229\r\n"
                             "Maximized=0\r\n"
                             "BoardHeight=668\r\n"
                             "ColumnWidth=164\r\n"
                             "ColumnFolded=0\r\n"
                             "MoveToNextKey=0\r\n"
                             "HostLayoutChosen=1\r\n";

    // A folder of its own for one case, deleted when the case ends.
    struct TemporaryFolder
    {
        fs::path path;

        TemporaryFolder()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("nazg-settings-test-" + std::to_string(stamp));
            fs::create_directories(path);
        }

        ~TemporaryFolder()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    };

    void WriteText(const fs::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << text;
    }

    std::string ReadText(const fs::path& path)
    {
        const auto bytes = nazg::ReadWholeFile(path);
        return bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
    }

    AppSettings Changed()
    {
        AppSettings settings;
        settings.legends.hostLayout       = "french";
        settings.legends.modifierNames    = nazg::ModifierNames::Mac;
        settings.theme                    = "light";
        settings.keycapStyle              = "flat";
        settings.legendFamily             = "spherical";
        settings.advancedTools            = true;
        settings.moveToNextKey            = true;
        settings.hostLayoutChosen         = true;
        settings.workspace.boardHeight    = 640.0f;
        settings.workspace.columnWidth    = 150.0f;
        settings.workspace.isColumnFolded = true;
        settings.window                   = { 10, 20, 1600, 900, true };
        settings.legacyViaDefinitions     = { "C:/a.json" };
        return settings;
    }

    bool Same(const AppSettings& a, const AppSettings& b)
    {
        return nazg::ToJson(a) == nazg::ToJson(b);
    }

    void TestRoundTrip()
    {
        std::printf("there and back\n");

        const AppSettings changed = Changed();
        Check(Same(nazg::FromJson(nazg::ToJson(changed), AppSettings{}), changed), "every setting comes back");

        const Json document = nazg::ToJson(AppSettings{});
        Check(!document.contains("window") && !document.contains("legacyViaDefinitions"),
              "a window never placed and no old paths: not written");
        Check(!document["workspace"].contains("spareWidth"), "a frame's numbers are not settings");
    }

    void TestWrongOrMissing()
    {
        std::printf("what a document lacks or holds wrong\n");

        const Json document = Json::parse(R"({ "appearance": { "theme": 5, "keycaps": "flat" },
                                               "tools": "yes",
                                               "keyboard": { "modifierNames": "amiga" },
                                               "workspace": { "boardHeight": -3 },
                                               "window": { "x": 1, "y": 2 } })");
        AppSettings defaults;
        defaults.legends.modifierNames = nazg::ModifierNames::Linux;
        const AppSettings read = nazg::FromJson(document, defaults);

        Check(read.theme == "dark" && read.keycapStyle == "flat", "a wrong type keeps the default, the right one is read");
        Check(!read.advancedTools, "a group of the wrong type is passed over");
        Check(read.legends.modifierNames == nazg::ModifierNames::Linux, "an unknown id keeps the platform's names");
        Check(read.workspace.boardHeight == 0.0f, "a size below zero is no size");
        Check(read.window.width == 0, "a place without a size is no place");
    }

    void TestImGuiIni()
    {
        std::printf("an earlier build's imgui.ini\n");

        const AppSettings read = nazg::FromImGuiIni(c_Ini, AppSettings{});
        Check(read.legends.hostLayout == "us_international" && read.hostLayoutChosen, "the host layout, said");
        Check(read.theme == "light" && read.keycapStyle == "outlined" && read.legendFamily == "cylindrical", "the look");
        Check(read.advancedTools && !read.moveToNextKey, "the switches");
        Check(read.window == AppSettings::WindowPlace{ 91, 101, 2450, 1229, false }, "the window's place");
        Check(read.workspace.boardHeight == 668.0f && read.workspace.columnWidth == 164.0f &&
                  !read.workspace.isColumnFolded,
              "the workspace");
        Check(read.legacyViaDefinitions == std::vector<std::string>{ "C:/boards/phoenix.json" }, "the old paths");

        const AppSettings french = nazg::FromImGuiIni("[Nazg][Settings]\nHostLayout=french\n", AppSettings{});
        Check(french.hostLayoutChosen, "a layout other than US, saved before the question, counts as said");

        const AppSettings other = nazg::FromImGuiIni("[Other][Settings]\nTheme=light\n", AppSettings{});
        Check(other.theme == "dark", "another section's lines are not Nazg's");
    }

    void TestNoFile()
    {
        std::printf("no file\n");

        TemporaryFolder folder;
        SettingsFile    file(folder.path, { folder.path / "imgui.ini" });
        const AppSettings read = file.Load(AppSettings{});

        Check(Same(read, AppSettings{}) && file.Problem().empty(), "the defaults, and nothing wrong");
        Check(!fs::exists(file.Path()), "nothing written until there is something to keep");
    }

    void TestMigration()
    {
        std::printf("migration from imgui.ini\n");

        TemporaryFolder folder;
        WriteText(folder.path / "imgui.ini", c_Ini);

        SettingsFile      file(folder.path, { folder.path / "missing.ini", folder.path / "imgui.ini" });
        const AppSettings read = file.Load(AppSettings{});

        Check(read.theme == "light" && read.window.width == 2450, "the settings an earlier build kept");
        Check(fs::exists(file.Path()), "settings.json written at once: the migration happens once");
        Check(ReadText(folder.path / "imgui.ini") == c_Ini, "imgui.ini left as it was");

        SettingsFile again(folder.path, { folder.path / "imgui.ini" });
        Check(Same(again.Load(AppSettings{}), read), "the next run reads settings.json");
    }

    void TestUnknownKeysKept()
    {
        std::printf("keys a newer build wrote\n");

        TemporaryFolder folder;
        SettingsFile    file(folder.path, {});
        WriteText(file.Path(), R"({ "format": 1, "future": { "x": 1 }, "appearance": { "theme": "light", "sparkle": true } })");

        AppSettings read = file.Load(AppSettings{});
        Check(read.theme == "light", "what it knows is read");

        read.theme = "dark";
        Check(file.Save(read), "and written");

        const Json written = Json::parse(ReadText(file.Path()));
        Check(written["appearance"]["theme"] == "dark", "with the change");
        Check(written["future"]["x"] == 1 && written["appearance"]["sparkle"] == true,
              "what it does not know kept, at the top and inside a group");
        Check(written.begin().key() == "format", "the format first");
    }

    void TestNewerFormat()
    {
        std::printf("a newer format\n");

        TemporaryFolder   folder;
        SettingsFile      file(folder.path, {});
        const std::string text = R"({ "format": 2, "appearance": { "theme": "light" } })";
        WriteText(file.Path(), text);

        const AppSettings read = file.Load(AppSettings{});
        Check(read.theme == "light" && !file.Problem().empty(), "read as far as understood, and said");
        Check(!file.Save(read) && ReadText(file.Path()) == text, "never written over");
    }

    void TestUnreadable()
    {
        std::printf("an unreadable file\n");

        TemporaryFolder   folder;
        SettingsFile      file(folder.path, { folder.path / "imgui.ini" });
        const std::string text = "{ not json";
        WriteText(file.Path(), text);
        WriteText(folder.path / "imgui.ini", c_Ini);

        const AppSettings read = file.Load(AppSettings{});
        Check(Same(read, AppSettings{}) && !file.Problem().empty(), "the defaults, and said -- not imgui.ini's");
        Check(ReadText(folder.path / "settings.json.unreadable") == text, "the file kept aside");
        Check(file.Save(read) && file.Problem().empty(), "and a good one written after");
    }

    void TestOptionalKeysGo()
    {
        std::printf("what is no longer there\n");

        TemporaryFolder folder;
        SettingsFile    file(folder.path, {});
        AppSettings     settings = Changed();
        Check(file.Save(settings), "written with the old paths");

        settings.legacyViaDefinitions.clear();
        Check(file.Save(settings), "written again once they were imported");
        Check(!Json::parse(ReadText(file.Path())).contains("legacyViaDefinitions"), "and gone from the file");
        Check(!fs::exists(folder.path / "settings.json.tmp"), "no temporary file left behind");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestRoundTrip();
    TestWrongOrMissing();
    TestImGuiIni();
    TestNoFile();
    TestMigration();
    TestUnknownKeysKept();
    TestNewerFormat();
    TestUnreadable();
    TestOptionalKeysGo();

    return TestResult();
}
