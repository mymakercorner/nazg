// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSectionFactory.h"

#include <utility>

#include "ui/NazgAltRepeatKeySection.h"
#include "ui/NazgAudioSection.h"
#include "ui/NazgCombosSection.h"
#include "ui/NazgCustomMenuSection.h"
#include "ui/NazgDeviceSection.h"
#include "ui/NazgDiagnosticsSection.h"
#include "ui/NazgKeyOverridesSection.h"
#include "ui/NazgKeymapSection.h"
#include "ui/NazgLayoutSection.h"
#include "ui/NazgLightingSection.h"
#include "ui/NazgMacrosSection.h"
#include "ui/NazgPlaceholderSection.h"
#include "ui/NazgQmkSettingsSection.h"
#include "ui/NazgTapDanceSection.h"

namespace nazg
{
    std::unique_ptr<Section> MakeSection(PlannedSection planned, const SectionContext& context)
    {
        HidTransport&         transport = context.transport;
        const std::string&    path      = context.path;
        Keyboard&             keyboard  = context.keyboard;
        const LegendSettings& legends   = context.legends;

        switch (planned.kind)
        {
        case SectionKind::Keymap:
            return std::make_unique<KeymapSection>(transport, path, keyboard, legends, context.moveToNextKey,
                                                   context.advancedTools);
        case SectionKind::Layout:
            return std::make_unique<LayoutSection>(transport, path, keyboard, legends);
        case SectionKind::Macros:
            return std::make_unique<MacrosSection>(transport, path, keyboard, context.legends, context.hostLayoutChosen,
                                                   context.lock, context.advancedTools);
        case SectionKind::Lighting:
            return std::make_unique<LightingSection>(transport, path, keyboard, legends);
        case SectionKind::Audio:
            return std::make_unique<AudioSection>(transport, path, keyboard, legends);
        case SectionKind::TapDance:
            return std::make_unique<TapDanceSection>(transport, path, keyboard, legends, context.lock,
                                                     context.advancedTools);
        case SectionKind::Combos:
            return std::make_unique<CombosSection>(transport, path, keyboard, legends, context.lock,
                                                   context.advancedTools);
        case SectionKind::KeyOverrides:
            return std::make_unique<KeyOverridesSection>(transport, path, keyboard, legends, context.lock,
                                                         context.advancedTools);
        case SectionKind::AltRepeatKey:
            return std::make_unique<AltRepeatKeySection>(transport, path, keyboard, legends, context.lock,
                                                         context.advancedTools);
        case SectionKind::QmkSettings:
            return std::make_unique<QmkSettingsSection>(transport, path, keyboard, legends);
        case SectionKind::CustomMenu:
            return std::make_unique<CustomMenuSection>(transport, path, keyboard, legends, context.advancedTools,
                                                       planned.customMenu);
        case SectionKind::Device:
            return std::make_unique<DeviceSection>(transport, path, keyboard, legends, context.lock);
        case SectionKind::Diagnostics:
            return std::make_unique<DiagnosticsSection>(transport, path, keyboard, legends, context.safeMargins,
                                                        context.identity);
        default:
            return std::make_unique<PlaceholderSection>(std::move(planned));
        }
    }
}
