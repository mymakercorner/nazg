// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "adapters/via/NazgDefinitionCheck.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include <nlohmann/json.hpp>

#include "adapters/via/NazgViaCustomMenu.h"
#include "model/NazgKeyboard.h"

namespace nazg
{
    namespace
    {
        // The controls a `menus` entry writes, as the file has them: an entry with a type is
        // one, a slice or submenu the sum of its content.
        size_t TypedEntries(const nlohmann::json& entry)
        {
            if (!entry.is_object())
                return 0;
            if (entry.contains("type"))
                return entry["type"] != "color-palette" ? 1 : 0;
            size_t count = 0;
            if (const auto content = entry.find("content"); content != entry.end() && content->is_array())
                for (const nlohmann::json& each : *content)
                    count += TypedEntries(each);
            return count;
        }

        std::string Position(const DefinitionKey& key)
        {
            return std::to_string(key.row) + "," + std::to_string(key.column);
        }

        void CheckMatrix(const KeyboardDefinition& definition, std::vector<std::string>& problems)
        {
            for (const DefinitionKey& key : definition.keys)
                if (key.IsSwitch() && (key.row >= definition.matrixRows || key.column >= definition.matrixColumns))
                {
                    problems.push_back("key " + Position(key) + " is outside the " +
                                       std::to_string(definition.matrixRows) + " x " +
                                       std::to_string(definition.matrixColumns) + " matrix");
                    return;   // one says it; a shifted matrix would list every key
                }
        }

        // Every choice of every option group, the others at choice 0, as the Layout section
        // would show it.
        void CheckLayoutOptions(const KeyboardDefinition& definition, std::vector<std::string>& problems)
        {
            std::vector<int> choices;   // per group, how many
            for (const DefinitionKey& key : definition.keys)
                if (key.layoutIndex >= 0)
                {
                    if (choices.size() <= static_cast<size_t>(key.layoutIndex))
                        choices.resize(key.layoutIndex + 1, 0);
                    choices[key.layoutIndex] = std::max(choices[key.layoutIndex], key.layoutOption + 1);
                }

            for (size_t group = 0; group < choices.size(); ++group)
                for (int choice = 0; choice < choices[group]; ++choice)
                {
                    std::vector<uint8_t> selection(choices.size(), 0);
                    selection[group] = static_cast<uint8_t>(choice);
                    try
                    {
                        for (const DefinitionKey& key : PlaceKeys(definition, selection))
                            if (!std::isfinite(key.x) || !std::isfinite(key.y))
                            {
                                problems.push_back("layout option " + std::to_string(group) + ", choice " +
                                                   std::to_string(choice) + ": a key lands nowhere");
                                return;
                            }
                    }
                    catch (const std::exception& failure)
                    {
                        problems.push_back("layout option " + std::to_string(group) + ", choice " +
                                           std::to_string(choice) + ": " + failure.what());
                        return;
                    }
                }
        }

        // Every control the file writes is one Nazg draws, and every condition one it reads.
        size_t CheckMenus(const std::vector<uint8_t>& bytes, const KeyboardDefinition& definition,
                          std::vector<std::string>& problems)
        {
            size_t                written = 0;
            const nlohmann::json  document = nlohmann::json::parse(bytes);
            const nlohmann::json* menus    = document.contains("menus") && document["menus"].is_array()
                                                 ? &document["menus"]
                                                 : nullptr;
            if (menus != nullptr)
                for (const nlohmann::json& entry : *menus)
                    written += TypedEntries(entry);

            size_t                   drawn = 0;
            std::vector<std::string> conditions;
            for (const CustomMenu& menu : definition.customMenus)
            {
                conditions.insert(conditions.end(), menu.conditions.begin(), menu.conditions.end());
                for (const CustomSubmenu& submenu : menu.submenus)
                {
                    conditions.insert(conditions.end(), submenu.conditions.begin(), submenu.conditions.end());
                    for (const CustomControl& control : submenu.controls)
                    {
                        ++drawn;
                        conditions.insert(conditions.end(), control.conditions.begin(), control.conditions.end());
                    }
                }
            }

            if (drawn != written)
                problems.push_back(std::to_string(written) + " menu controls in the file, " + std::to_string(drawn) +
                                   " read");
            for (const std::string& condition : conditions)
                if (!IsReadableCondition(condition))
                    problems.push_back("a menu condition Nazg cannot read: " + condition);
            return drawn;
        }
    }

    DefinitionCheck CheckDefinition(const std::vector<uint8_t>& bytes)
    {
        DefinitionCheck check;
        if (bytes.size() > c_MaxDefinitionBytes)
        {
            check.problems.push_back(std::to_string(bytes.size()) + " bytes, over the limit of " +
                                     std::to_string(c_MaxDefinitionBytes));
            return check;
        }

        try
        {
            check.definition = ParseDefinition(bytes);
        }
        catch (const std::exception& failure)
        {
            check.problems.push_back(failure.what());
            return check;
        }

        const KeyboardDefinition& definition = *check.definition;
        if (definition.name.empty())
            check.problems.push_back("the board has no name");
        if (definition.vendorId == 0 && definition.productId == 0)
            check.problems.push_back("the VID:PID is 0x0000:0x0000");

        CheckMatrix(definition, check.problems);
        CheckLayoutOptions(definition, check.problems);
        try
        {
            check.menuControls = CheckMenus(bytes, definition, check.problems);
        }
        catch (const std::exception& failure)
        {
            check.problems.push_back(std::string("its menus: ") + failure.what());
        }
        return check;
    }
}
