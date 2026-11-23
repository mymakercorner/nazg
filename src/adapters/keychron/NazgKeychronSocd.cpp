// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeychronSocd.h"

namespace nazg::keychron
{
    std::optional<SocdChoice> SocdChoiceOf(SocdType type) noexcept
    {
        switch (type)
        {
        case SocdType::DeeperThenFirst: return SocdChoice::RappySnappy;
        case SocdType::LastWins: return SocdChoice::LastKeyPriority;
        case SocdType::FirstKeyWins:
        case SocdType::SecondKeyWins: return SocdChoice::AbsolutePriority;
        case SocdType::Neither: return SocdChoice::Neutral;
        case SocdType::Off:
        case SocdType::DeeperWins: break;
        }
        return std::nullopt;
    }

    SocdType SocdTypeFor(SocdChoice choice, SocdType now) noexcept
    {
        switch (choice)
        {
        case SocdChoice::RappySnappy: return SocdType::DeeperThenFirst;
        case SocdChoice::LastKeyPriority: return SocdType::LastWins;
        case SocdChoice::AbsolutePriority:
            return now == SocdType::SecondKeyWins ? SocdType::SecondKeyWins : SocdType::FirstKeyWins;
        case SocdChoice::Neutral: break;
        }
        return SocdType::Neither;
    }

    bool FitsSocdSlot(KeyPosition key) noexcept
    {
        return key.row < 8 && key.column < 32;
    }

    KeyPosition FirstKeyOf(const SocdSlot& slot) noexcept
    {
        return { slot.firstRow, slot.firstColumn };
    }

    KeyPosition SecondKeyOf(const SocdSlot& slot) noexcept
    {
        return { slot.secondRow, slot.secondColumn };
    }

    SocdSlot MakeSocdSlot(KeyPosition first, KeyPosition second, SocdType type) noexcept
    {
        SocdSlot slot;
        slot.firstRow     = first.row;
        slot.firstColumn  = first.column;
        slot.secondRow    = second.row;
        slot.secondColumn = second.column;
        slot.type         = type;
        return slot;
    }

    std::vector<uint8_t> SocdSlotsInUse(const Profile& profile)
    {
        std::vector<uint8_t> used;
        for (size_t slot = 0; slot < profile.socdSlots.size() && slot <= 0xFF; ++slot)
            if (!profile.socdSlots[slot].IsEmpty())
                used.push_back(static_cast<uint8_t>(slot));
        return used;
    }

    std::optional<uint8_t> FreeSocdSlot(const Profile& profile)
    {
        for (size_t slot = 0; slot < profile.socdSlots.size() && slot <= 0xFF; ++slot)
            if (profile.socdSlots[slot].IsEmpty())
                return static_cast<uint8_t>(slot);
        return std::nullopt;
    }

    std::optional<uint8_t> SocdSlotPairing(const Profile& profile, KeyPosition a, KeyPosition b)
    {
        for (const uint8_t slot : SocdSlotsInUse(profile))
        {
            const KeyPosition first  = FirstKeyOf(profile.socdSlots[slot]);
            const KeyPosition second = SecondKeyOf(profile.socdSlots[slot]);
            if ((first == a && second == b) || (first == b && second == a))
                return slot;
        }
        return std::nullopt;
    }

    void ApplySocdWrite(Profile& profile, const SocdWrite& write)
    {
        if (write.slot < profile.socdSlots.size())
            profile.socdSlots[write.slot] = write.pair.IsEmpty() ? SocdSlot{} : write.pair;
    }

    bool Holds(const Profile& profile, const SocdWrite& write)
    {
        return write.slot < profile.socdSlots.size() &&
               profile.socdSlots[write.slot] == (write.pair.IsEmpty() ? SocdSlot{} : write.pair);
    }

    // socd_action(), for one pair. What it holds back is masked out of what the keys' own modes say.
    SocdPlayer::Sent SocdPlayer::Step(SocdType type, Key first, Key second) noexcept
    {
        bool holdFirst = false, holdSecond = false, keepLast = false;
        if (type != SocdType::Off && first.pressed && second.pressed)
        {
            switch (type)
            {
            case SocdType::DeeperWins:
            case SocdType::DeeperThenFirst:
                if (first.sixtieths > c_SocdBottomSixtieths && second.sixtieths > c_SocdBottomSixtieths)
                    keepLast = type == SocdType::DeeperThenFirst;
                else if (m_Last == Last::Second && first.sixtieths > second.sixtieths)
                    holdSecond = true;
                else if (m_Last == Last::First && first.sixtieths < second.sixtieths)
                    holdFirst = true;
                else
                    keepLast = true;
                break;
            case SocdType::LastWins:
                // The one just pressed, the other having been sent before it.
                if (m_Sent.first && !m_Before[1].pressed)
                {
                    holdFirst = true;
                    m_Last    = Last::Second;
                }
                else if (m_Sent.second && !m_Before[0].pressed)
                {
                    holdSecond = true;
                    m_Last     = Last::First;
                }
                else
                {
                    keepLast = true;
                }
                break;
            case SocdType::FirstKeyWins:
                holdSecond = true;
                m_Last     = Last::First;
                break;
            case SocdType::SecondKeyWins:
                holdFirst = true;
                m_Last    = Last::Second;
                break;
            case SocdType::Neither:
                holdFirst = holdSecond = true;
                m_Last                 = Last::None;
                break;
            default: break;   // a type no source has: the firmware lets both through
            }
        }
        else if (first.pressed)
        {
            m_Last = Last::First;
        }
        else if (second.pressed)
        {
            m_Last = Last::Second;
        }

        if (keepLast)
        {
            holdSecond = holdSecond || m_Last == Last::First;
            holdFirst  = holdFirst || m_Last == Last::Second;
        }

        m_Before[0] = first;
        m_Before[1] = second;
        m_Sent      = { first.pressed && !holdFirst, second.pressed && !holdSecond };
        return m_Sent;
    }
}
