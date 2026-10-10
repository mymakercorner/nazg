// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMacro.h"

namespace nazg
{
    std::vector<size_t> PairPressesAndReleases(const Macro& macro)
    {
        std::vector<size_t> partner(macro.size());
        std::vector<size_t> open;
        for (size_t index = 0; index < macro.size(); ++index)
        {
            partner[index] = index;
            if (macro[index].kind == MacroStep::Kind::Press)
            {
                open.push_back(index);
            }
            else if (macro[index].kind == MacroStep::Kind::Release)
            {
                for (size_t at = open.size(); at-- > 0;)
                    if (macro[open[at]].key == macro[index].key)
                    {
                        partner[index]    = open[at];
                        partner[open[at]] = index;
                        open.erase(open.begin() + static_cast<std::ptrdiff_t>(at));
                        break;
                    }
            }
        }
        return partner;
    }
}
