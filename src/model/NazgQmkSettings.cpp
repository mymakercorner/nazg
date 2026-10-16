// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgQmkSettings.h"

#include <algorithm>
#include <cmath>

namespace nazg
{
    size_t QmkSettingWidth(uint16_t qsid)
    {
        // qmk_settings.h's qmk_settings_t: a bit setting reads one byte, whatever its field's size.
        switch (qsid)
        {
        case QmkSetting::GraveEscape:
        case QmkSetting::AutoShift:
        case QmkSetting::OneShotTapToggle:
        case QmkSetting::Tapping:
        case QmkSetting::TappingToggle:
        case QmkSetting::PermissiveHold:
        case QmkSetting::HoldOnOtherKey:
        case QmkSetting::RetroTapping:
        case QmkSetting::ChordalHold:
            return 1;
        case QmkSetting::Magic:
            return 4;
        default:
            return qsid >= QmkSetting::ComboTerm && qsid <= QmkSetting::FlowTap ? 2 : 0;
        }
    }

    bool QmkSettingBit(const QmkSettings& settings, uint16_t qsid, int bit)
    {
        const auto found = settings.find(qsid);
        return found != settings.end() && ((found->second >> bit) & 1) != 0;
    }

    uint32_t WithBit(uint32_t value, int bit, bool on)
    {
        return on ? value | (1u << bit) : value & ~(1u << bit);
    }

    HoldDecision HoldDecisionOf(const QmkSettings& settings)
    {
        bool permissive    = false;
        bool otherKeyPress = false;
        if (settings.count(QmkSetting::Tapping) != 0)
        {
            permissive    = QmkSettingBit(settings, QmkSetting::Tapping, TappingBit::PermissiveHold);
            otherKeyPress = !QmkSettingBit(settings, QmkSetting::Tapping, TappingBit::NotHoldOnOtherKey);
        }
        else
        {
            permissive    = QmkSettingBit(settings, QmkSetting::PermissiveHold, 0);
            otherKeyPress = QmkSettingBit(settings, QmkSetting::HoldOnOtherKey, 0);
        }
        return otherKeyPress ? HoldDecision::OtherKeyPress : permissive ? HoldDecision::Permissive : HoldDecision::WaitForTerm;
    }

    QmkSettings HoldDecisionWrites(const QmkSettings& settings, HoldDecision decision)
    {
        const bool permissive    = decision == HoldDecision::Permissive;
        const bool otherKeyPress = decision == HoldDecision::OtherKeyPress;
        if (const auto tapping = settings.find(QmkSetting::Tapping); tapping != settings.end())
        {
            uint32_t value = WithBit(tapping->second, TappingBit::PermissiveHold, permissive);
            value          = WithBit(value, TappingBit::NotHoldOnOtherKey, !otherKeyPress);
            return { { QmkSetting::Tapping, value } };
        }
        QmkSettings writes;
        if (settings.count(QmkSetting::PermissiveHold) != 0)
            writes[QmkSetting::PermissiveHold] = permissive ? 1 : 0;
        if (settings.count(QmkSetting::HoldOnOtherKey) != 0)
            writes[QmkSetting::HoldOnOtherKey] = otherKeyPress ? 1 : 0;
        return writes;
    }

    std::optional<std::string_view> MagicKeycode(std::string_view key, uint32_t magic)
    {
        const auto on = [magic](int bit) { return ((magic >> bit) & 1) != 0; };
        const bool noGui = on(MagicBit::NoGui);
        std::string_view sent = key;
        if (key == "KC_CAPS")
            sent = on(MagicBit::SwapControlCapsLock) || on(MagicBit::CapsLockToControl) ? "KC_LCTL" : key;
        else if (key == "KC_LCTL")
            sent = on(MagicBit::SwapControlCapsLock) ? "KC_CAPS" : on(MagicBit::SwapLeftControlGui) ? (noGui ? "KC_NO" : "KC_LGUI") : key;
        else if (key == "KC_LALT")
            sent = on(MagicBit::SwapLeftAltGui) ? (noGui ? "KC_NO" : "KC_LGUI") : key;
        else if (key == "KC_LGUI")
            sent = on(MagicBit::SwapLeftAltGui) ? "KC_LALT" : on(MagicBit::SwapLeftControlGui) ? "KC_LCTL" : noGui ? "KC_NO" : key;
        else if (key == "KC_RCTL")
            sent = on(MagicBit::SwapRightControlGui) ? (noGui ? "KC_NO" : "KC_RGUI") : key;
        else if (key == "KC_RALT")
            sent = on(MagicBit::SwapRightAltGui) ? (noGui ? "KC_NO" : "KC_RGUI") : key;
        else if (key == "KC_RGUI")
            sent = on(MagicBit::SwapRightAltGui) ? "KC_RALT" : on(MagicBit::SwapRightControlGui) ? "KC_RCTL" : noGui ? "KC_NO" : key;
        else if (key == "KC_GRV")
            sent = on(MagicBit::SwapGraveEscape) ? "KC_ESC" : key;
        else if (key == "KC_ESC")
            sent = on(MagicBit::SwapGraveEscape) ? "KC_GRV" : key;
        else if (key == "KC_BSLS")
            sent = on(MagicBit::SwapBackslashBksp) ? "KC_BSPC" : key;
        else if (key == "KC_BSPC")
            sent = on(MagicBit::SwapBackslashBksp) ? "KC_BSLS" : key;
        if (sent == key)
            return std::nullopt;
        return sent;
    }

    std::vector<char> GuessHands(const std::vector<via::DefinitionKey>& keys)
    {
        std::vector<char> hands(keys.size(), '*');
        if (keys.empty())
            return hands;

        std::vector<float> centres;
        for (const via::DefinitionKey& key : keys)
            centres.push_back(key.x + key.width / 2.0f);
        std::vector<float> sorted = centres;
        std::sort(sorted.begin(), sorted.end());
        const float middle = (sorted.front() + sorted.back()) / 2.0f;

        // Symmetric: every centre has its mirror image, within 0.02u.
        constexpr float c_Tolerance = 0.02f;
        bool symmetric = true;
        for (float centre : sorted)
        {
            const float mirrored = 2.0f * middle - centre;
            const auto  at       = std::lower_bound(sorted.begin(), sorted.end(), mirrored - c_Tolerance);
            if (at == sorted.end() || std::fabs(*at - mirrored) > c_Tolerance)
            {
                symmetric = false;
                break;
            }
        }

        // The space bar: the one key 6u wide or more.
        std::vector<size_t> wide;
        for (size_t index = 0; index < keys.size(); ++index)
            if (keys[index].width >= 6.0f)
                wide.push_back(index);
        const bool hasSpaceBar = wide.size() == 1;

        float split = middle;
        if (!symmetric && !hasSpaceBar)
        {
            float widest = 0.0f;
            for (size_t index = 0; index + 1 < sorted.size(); ++index)
                if (sorted[index + 1] - sorted[index] > widest)
                {
                    widest = sorted[index + 1] - sorted[index];
                    split  = (sorted[index + 1] + sorted[index]) / 2.0f;
                }
        }

        for (size_t index = 0; index < keys.size(); ++index)
        {
            const via::DefinitionKey& key = keys[index];
            float sign = centres[index] - split;
            if (!symmetric && hasSpaceBar)
            {
                const via::DefinitionKey& space = keys[wide[0]];
                const float x0 = space.x + space.width / 2.0f - 0.05f;
                const float y0 = space.y + space.height / 2.0f - 1.0f;
                sign = (centres[index] - x0) - (key.y + key.height / 2.0f - y0) / 3.0f;
            }
            const bool isSpaceBar = hasSpaceBar && index == wide[0];   // either hand, however split
            hands[index] = isSpaceBar || std::fabs(sign) <= 0.02f ? '*' : sign < 0.0f ? 'L' : 'R';
        }
        return hands;
    }
}
