// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSettings.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "files/NazgFiles.h"

namespace nazg
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        constexpr char c_FileName[]         = "settings.json";
        constexpr char c_UpdateSuffix[]     = ".tmp";
        constexpr char c_UnreadableSuffix[] = ".unreadable";

        // A member of `object` by name; null when there is none, or `object` is not an object.
        const Json* Find(const Json& object, const char* key)
        {
            if (!object.is_object())
                return nullptr;
            const auto found = object.find(key);
            return found == object.end() ? nullptr : &*found;
        }

        // Each kept as it was unless the document has it, and of the right type.
        void Read(const Json& object, const char* key, std::string& into)
        {
            if (const Json* value = Find(object, key); value != nullptr && value->is_string())
                into = value->get<std::string>();
        }

        void Read(const Json& object, const char* key, bool& into)
        {
            if (const Json* value = Find(object, key); value != nullptr && value->is_boolean())
                into = value->get<bool>();
        }

        void Read(const Json& object, const char* key, int& into)
        {
            if (const Json* value = Find(object, key); value != nullptr && value->is_number_integer())
                into = value->get<int>();
        }

        // Sizes: kept only above 0, as the layout cannot use anything else.
        void ReadSize(const Json& object, const char* key, float& into)
        {
            if (const Json* value = Find(object, key); value != nullptr && value->is_number() && value->get<float>() > 0.0f)
                into = value->get<float>();
        }

        // `update`'s members over `document`'s, into objects found in both: what `document` holds
        // that `update` does not name stays.
        void Overlay(Json& document, const Json& update)
        {
            for (const auto& [key, value] : update.items())
            {
                const auto found = document.find(key);
                if (found != document.end() && found->is_object() && value.is_object())
                    Overlay(*found, value);
                else
                    document[key] = value;
            }
        }

        std::string_view Trimmed(std::string_view line)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.remove_suffix(1);
            return line;
        }

        std::filesystem::path WithSuffix(const std::filesystem::path& path, const char* suffix)
        {
            std::filesystem::path result = path;
            result += suffix;
            return result;
        }
    }

    Json ToJson(const AppSettings& settings)
    {
        Json document = Json::object();
        document["appearance"] = { { "theme", settings.theme },
                                   { "keycaps", settings.keycapStyle },
                                   { "legends", settings.legendFamily } };
        document["keyboard"]   = { { "hostLayout", settings.legends.hostLayout },
                                   { "hostLayoutChosen", settings.hostLayoutChosen },
                                   { "modifierNames", std::string(IdOf(settings.legends.modifierNames)) } };
        document["keymap"]     = { { "moveToNextKey", settings.moveToNextKey } };
        document["workspace"]  = { { "boardHeight", settings.workspace.boardHeight },
                                   { "columnWidth", settings.workspace.columnWidth },
                                   { "columnFolded", settings.workspace.isColumnFolded } };
        document["tools"]      = { { "advanced", settings.advancedTools } };
        if (settings.window.width > 0)
            document["window"] = { { "x", settings.window.x },
                                   { "y", settings.window.y },
                                   { "width", settings.window.width },
                                   { "height", settings.window.height },
                                   { "maximized", settings.window.maximized } };
        if (!settings.legacyViaDefinitions.empty())
            document["legacyViaDefinitions"] = settings.legacyViaDefinitions;
        return document;
    }

    AppSettings FromJson(const Json& document, AppSettings defaults)
    {
        AppSettings settings = std::move(defaults);

        if (const Json* appearance = Find(document, "appearance"))
        {
            Read(*appearance, "theme", settings.theme);
            Read(*appearance, "keycaps", settings.keycapStyle);
            Read(*appearance, "legends", settings.legendFamily);
        }

        if (const Json* keyboard = Find(document, "keyboard"))
        {
            Read(*keyboard, "hostLayout", settings.legends.hostLayout);
            Read(*keyboard, "hostLayoutChosen", settings.hostLayoutChosen);
            std::string names;
            Read(*keyboard, "modifierNames", names);
            settings.legends.modifierNames = ModifierNamesFromId(names).value_or(settings.legends.modifierNames);
        }

        if (const Json* keymap = Find(document, "keymap"))
            Read(*keymap, "moveToNextKey", settings.moveToNextKey);

        if (const Json* workspace = Find(document, "workspace"))
        {
            ReadSize(*workspace, "boardHeight", settings.workspace.boardHeight);
            ReadSize(*workspace, "columnWidth", settings.workspace.columnWidth);
            Read(*workspace, "columnFolded", settings.workspace.isColumnFolded);
        }

        if (const Json* tools = Find(document, "tools"))
            Read(*tools, "advanced", settings.advancedTools);

        // A place without a size is no place.
        if (const Json* window = Find(document, "window"))
        {
            AppSettings::WindowPlace place;
            Read(*window, "x", place.x);
            Read(*window, "y", place.y);
            Read(*window, "width", place.width);
            Read(*window, "height", place.height);
            Read(*window, "maximized", place.maximized);
            if (place.width > 0 && place.height > 0)
                settings.window = place;
        }

        if (const Json* legacy = Find(document, "legacyViaDefinitions"); legacy != nullptr && legacy->is_array())
            for (const Json& path : *legacy)
                if (path.is_string())
                    settings.legacyViaDefinitions.push_back(path.get<std::string>());

        return settings;
    }

    AppSettings FromImGuiIni(std::string_view ini, AppSettings defaults)
    {
        AppSettings settings  = std::move(defaults);
        bool        inSection = false;

        while (!ini.empty())
        {
            const size_t           end  = ini.find('\n');
            const std::string_view line = Trimmed(ini.substr(0, end));
            ini.remove_prefix(end == std::string_view::npos ? ini.size() : end + 1);

            if (!line.empty() && line.front() == '[')
            {
                inSection = line == "[Nazg][Settings]";
                continue;
            }
            const size_t equals = line.find('=');
            if (!inSection || equals == std::string_view::npos)
                continue;

            const std::string_view key   = line.substr(0, equals);
            const std::string      value = std::string(line.substr(equals + 1));
            const bool             on    = value == "1";

            if (key == "HostLayoutChosen")
                settings.hostLayoutChosen = settings.hostLayoutChosen || on;
            else if (key == "HostLayout")
            {
                settings.legends.hostLayout = value;
                // A layout other than US, saved by a build before the question, counts as said.
                settings.hostLayoutChosen = settings.hostLayoutChosen || value != "us";
            }
            else if (key == "ModifierNames")
                settings.legends.modifierNames = ModifierNamesFromId(value).value_or(settings.legends.modifierNames);
            else if (key == "Theme")
                settings.theme = value;
            else if (key == "Keycaps")
                settings.keycapStyle = value;
            else if (key == "Legends")
                settings.legendFamily = value;
            else if (key == "AdvancedTools")
                settings.advancedTools = on;
            else if (key == "ViaDefinition")
                settings.legacyViaDefinitions.push_back(value);
            else if (key == "Window")
            {
                AppSettings::WindowPlace place;
                if (std::sscanf(value.c_str(), "%d,%d,%d,%d", &place.x, &place.y, &place.width, &place.height) == 4 &&
                    place.width > 0 && place.height > 0)
                {
                    place.maximized = settings.window.maximized;
                    settings.window = place;
                }
            }
            else if (key == "Maximized")
                settings.window.maximized = on;
            else if (key == "BoardHeight" || key == "ColumnWidth")
            {
                float size = 0.0f;
                if (std::sscanf(value.c_str(), "%f", &size) == 1 && size > 0.0f)
                    (key == "BoardHeight" ? settings.workspace.boardHeight : settings.workspace.columnWidth) = size;
            }
            else if (key == "ColumnFolded")
                settings.workspace.isColumnFolded = on;
            else if (key == "MoveToNextKey")
                settings.moveToNextKey = on;
        }
        return settings;
    }

    SettingsFile::SettingsFile(std::filesystem::path folder, std::vector<std::filesystem::path> legacyIniFiles)
        : m_Path(folder / c_FileName), m_LegacyIniFiles(std::move(legacyIniFiles))
    {
    }

    AppSettings SettingsFile::Load(const AppSettings& defaults)
    {
        m_Problem.clear();
        m_IsWritable = true;
        m_Document   = Json::object();

        std::error_code error;
        if (std::filesystem::exists(m_Path, error))
        {
            try
            {
                const std::optional<std::vector<uint8_t>> bytes = ReadWholeFile(m_Path);
                if (!bytes)
                    throw std::runtime_error("cannot open the file");

                Json document = Json::parse(bytes->begin(), bytes->end());
                const Json* format = Find(document, "format");
                if (format == nullptr || !format->is_number_integer() || format->get<int>() < 1)
                    throw std::runtime_error("no format number");
                if (format->get<int>() > c_Format)
                {
                    m_IsWritable = false;
                    m_Problem    = Utf8FromPath(m_Path) +
                                " was written by a newer Nazg: it is read as far as this one understands it, and "
                                "not changed";
                }

                m_Document = std::move(document);
                return FromJson(m_Document, defaults);
            }
            catch (const std::exception& failure)
            {
                // Kept aside before anything replaces it: what was in it may still matter to someone.
                std::filesystem::copy_file(m_Path, WithSuffix(m_Path, c_UnreadableSuffix),
                                           std::filesystem::copy_options::overwrite_existing, error);
                m_Problem = Utf8FromPath(m_Path) + " could not be read (" + failure.what() +
                            "): the defaults are used, and the file is kept as " + c_FileName + c_UnreadableSuffix;
                m_Document = Json::object();
                return defaults;
            }
        }

        // No settings.json yet: an earlier build's imgui.ini, once.
        for (const std::filesystem::path& ini : m_LegacyIniFiles)
        {
            const std::optional<std::vector<uint8_t>> bytes = ReadWholeFile(ini);
            if (!bytes)
                continue;

            const AppSettings migrated =
                FromImGuiIni(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), defaults);
            Save(migrated);
            return migrated;
        }
        return defaults;
    }

    bool SettingsFile::Save(const AppSettings& settings)
    {
        if (!m_IsWritable)
            return false;

        // What the file held, this build's settings over it; the optional members go when unset.
        Json document = m_Document.is_object() && !m_Document.empty() ? m_Document : Json{ { "format", c_Format } };
        document["format"] = c_Format;
        Overlay(document, ToJson(settings));
        if (settings.window.width <= 0)
            document.erase("window");
        if (settings.legacyViaDefinitions.empty())
            document.erase("legacyViaDefinitions");

        const std::string text = document.dump(2) + "\n";

        // Whole, then renamed over the old one: a crash leaves one file or the other.
        const std::filesystem::path update = WithSuffix(m_Path, c_UpdateSuffix);
        {
            std::ofstream stream(update, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.close();
            if (!stream)
            {
                m_Problem = "cannot write " + Utf8FromPath(update);
                return false;
            }
        }

        std::error_code failure;
        std::filesystem::rename(update, m_Path, failure);
        if (failure)
        {
            m_Problem = "cannot replace " + Utf8FromPath(m_Path) + ": " + failure.message();
            return false;
        }

        m_Document = std::move(document);
        m_Problem.clear();
        return true;
    }
}
