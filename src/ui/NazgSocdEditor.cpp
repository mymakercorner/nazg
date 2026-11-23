// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSocdEditor.h"

#include <algorithm>
#include <cfloat>
#include <iterator>
#include <utility>

#include "imgui.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // The mockup's measures, in its pixels at a 14 px line: a card's picture -- its lanes' tops and
        // height, what is sent under them -- and the test's.
        constexpr float c_Line          = 14.0f;
        constexpr float c_LabelWidth    = 27.0f;
        constexpr float c_LaneHeight    = 12.0f;
        constexpr float c_LanesApart    = 14.0f;
        constexpr float c_SentTop       = 30.0f;
        constexpr float c_SentHeight    = 14.0f;
        constexpr float c_PictureHeight = 46.0f;
        constexpr float c_CardPad       = 7.0f;
        constexpr float c_CardGap       = 6.0f;

        constexpr float c_TestLane   = 22.0f;
        constexpr float c_TestApart  = 30.0f;
        constexpr float c_TestSent   = 61.0f;
        constexpr float c_TestHeight = 78.0f;

        // How long the test's strip shows, in seconds.
        constexpr double c_TrailTime = 4.0;

        constexpr const char* c_Names[] = { "Rappy Snappy", "Last Key Priority", "Absolute Priority", "Neutral" };

        ImU32 FirstColour() { return BoardColours::Highlighted(); }
        ImU32 SecondColour() { return BoardColours::HighlightedSecond(); }

        ImU32 Faint(ImU32 colour, float alpha)
        {
            ImVec4 faint = ImGui::ColorConvertU32ToFloat4(colour);
            faint.w *= alpha;
            return ImGui::ColorConvertFloat4ToU32(faint);
        }

        // Black or white, whichever reads on `colour`.
        ImU32 InkOn(ImU32 colour)
        {
            const ImVec4 c = ImGui::ColorConvertU32ToFloat4(colour);
            return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z > 0.55f ? IM_COL32(20, 20, 22, 255) : IM_COL32_WHITE;
        }

        // A name short enough for a lane's label or what is sent, whole characters of UTF-8.
        std::string Short(const std::string& name, float room, ImFont* font, float size)
        {
            std::string shown = name;
            while (!shown.empty() && font->CalcTextSizeA(size, FLT_MAX, 0.0f, shown.c_str()).x > room)
            {
                do
                    shown.pop_back();
                while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80);
                // A lead byte left alone by the cut goes with its character.
                if (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0xC0)
                    shown.pop_back();
            }
            return shown;
        }

        void TextIn(ImDrawList* list, float size, ImVec2 min, ImVec2 max, float alignX, ImU32 colour, const std::string& text)
        {
            ImFont*           font  = ImGui::GetFont();
            const std::string shown = Short(text, max.x - min.x, font, size);
            const ImVec2      extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, shown.c_str());
            list->AddText(font, size, ImVec2(min.x + (max.x - min.x - extent.x) * alignX, (min.y + max.y - extent.y) / 2), colour,
                          shown.c_str());
        }

        // What a card draws: the two keys down, from a hundredth of the picture to another and how deep;
        // under them what is sent, 1 the first key and 2 the second. The first key held, the other pressed
        // meanwhile; then the other way round. Rappy Snappy's first moment is a light press and a deep
        // one; its second, and every other, is at the bottom.
        struct Down
        {
            float from;
            float to;
            float depth;
        };
        struct SentPart
        {
            float from;
            float to;
            int   key;
        };
        struct Scene
        {
            Down                  first[2];
            Down                  second[2];
            std::vector<SentPart> sent;
        };

        Scene SceneOf(keychron::SocdType type)
        {
            const bool deep = keychron::SocdChoiceOf(type) == keychron::SocdChoice::RappySnappy;
            Scene      scene{ { { 2, 46, deep ? 0.55f : 1.0f }, { 68, 86, 1 } }, { { 16, 32, 1 }, { 54, 98, 1 } }, {} };
            switch (type)
            {
            case keychron::SocdType::LastWins:
                scene.sent = { { 2, 16, 1 }, { 16, 32, 2 }, { 32, 46, 1 }, { 54, 68, 2 }, { 68, 86, 1 }, { 86, 98, 2 } };
                break;
            case keychron::SocdType::FirstKeyWins:
                scene.sent = { { 2, 46, 1 }, { 54, 68, 2 }, { 68, 86, 1 }, { 86, 98, 2 } };
                break;
            case keychron::SocdType::Neither:
                scene.sent = { { 2, 16, 1 }, { 32, 46, 1 }, { 54, 68, 2 }, { 86, 98, 2 } };
                break;
            default:   // the deeper, and the second key winning: the same to see
                scene.sent = { { 2, 16, 1 }, { 16, 32, 2 }, { 32, 46, 1 }, { 54, 98, 2 } };
                break;
            }
            return scene;
        }

        void DrawPicture(ImDrawList* list, ImVec2 at, float width, float px, const SocdPairView& pair, keychron::SocdType type)
        {
            const Scene scene  = SceneOf(type);
            const ImU32 text   = ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 muted  = ImGui::GetColorU32(ColourOf(PanelColour::Muted));
            const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
            const float size   = ImGui::GetFontSize() * 0.68f;
            const float left   = at.x + c_LabelWidth * px;
            const auto  x      = [&](float hundredth) { return left + hundredth / 100.0f * (at.x + width - left); };

            const auto lane = [&](const Down (&downs)[2], float top, ImU32 colour, const std::string& name)
            {
                TextIn(list, size, ImVec2(at.x, top), ImVec2(left - 4 * px, top + c_LaneHeight * px), 1.0f, text, name);
                list->AddLine(ImVec2(left, top), ImVec2(at.x + width, top), border);
                for (const Down& down : downs)
                    list->AddRectFilled(ImVec2(x(down.from), top), ImVec2(x(down.to), top + c_LaneHeight * px * down.depth),
                                        Faint(colour, 0.5f));
            };
            lane(scene.first, at.y, FirstColour(), pair.first);
            lane(scene.second, at.y + c_LanesApart * px, SecondColour(), pair.second);

            const float top = at.y + c_SentTop * px, bottom = top + c_SentHeight * px;
            TextIn(list, size, ImVec2(at.x, top), ImVec2(left - 4 * px, bottom), 1.0f, muted, "sent");
            for (const SentPart& part : scene.sent)
            {
                const ImU32  colour = part.key == 1 ? FirstColour() : SecondColour();
                const ImVec2 min(x(part.from), top), max(x(part.to), bottom);
                list->AddRectFilled(min, max, colour, 2 * px);
                TextIn(list, size, min, max, 0.5f, InkOn(colour), part.key == 1 ? pair.first : pair.second);
            }
        }
    }

    namespace
    {
        // A pair's item: a frame high, its caps a little lower, the arrows' room between them.
        float CapHeight() { return ImGui::GetFrameHeight() - 6.0f * ImGui::GetFontSize() / c_Line; }

        float CapWidth(const std::string& name)
        {
            return std::max(CapHeight(), ImGui::CalcTextSize(name.c_str()).x + ImGui::GetFontSize() * 0.75f);
        }

        float ArrowsWidth() { return ImGui::GetFontSize() * 1.7f; }
        float RemoveWidth() { return ImGui::GetFrameHeight() * 0.8f; }

        // Two arrows, right over left, as wide as a letter or so, centred at `middle`.
        void DrawArrows(ImDrawList* list, ImVec2 middle, ImU32 colour)
        {
            const float unit  = ImGui::GetFontSize();
            const float half  = unit * 0.42f, apart = unit * 0.15f, head = unit * 0.15f, thick = std::max(1.0f, unit * 0.09f);
            for (const float side : { -1.0f, 1.0f })
            {
                // Above, pointing right; below, pointing left.
                const float y = middle.y + side * apart, tip = middle.x - side * half;
                list->AddLine(ImVec2(middle.x - half, y), ImVec2(middle.x + half, y), colour, thick);
                list->AddLine(ImVec2(tip, y), ImVec2(tip + side * head, y - head), colour, thick);
                list->AddLine(ImVec2(tip, y), ImVec2(tip + side * head, y + head), colour, thick);
            }
        }

        void DrawCap(ImDrawList* list, ImVec2 min, const std::string& name)
        {
            const ImVec2 max(min.x + CapWidth(name), min.y + CapHeight());
            const float  rounding = ImGui::GetFontSize() * 0.3f;
            if (name.empty())
            {
                // Waited for: outlined as the DKS editor's row waiting for its key.
                list->AddRect(min, max, BoardColours::Selected(), rounding, 0, 2.0f * ImGui::GetFontSize() / c_Line);
                return;
            }
            list->AddRectFilled(min, max, BoardColours::Fill(KeyFill::Alpha, 0.0f).face, rounding);
            list->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border), rounding);
            TextIn(list, ImGui::GetFontSize(), min, max, 0.5f, BoardColours::Legend(LegendInk::Legend, KeyFill::Alpha), name);
        }
    }

    float SocdPairItemWidth(const std::string& first, const std::string& second, bool hasRemove)
    {
        return CapWidth(first) + ArrowsWidth() + CapWidth(second) + 2.0f * ImGui::GetStyle().FramePadding.x +
               (hasRemove ? RemoveWidth() : 0.0f);
    }

    SocdPairItemEvents DrawSocdPairItem(const std::string& first, const std::string& second, bool isShown, bool hasRemove)
    {
        SocdPairItemEvents events;
        const ImGuiStyle&  style  = ImGui::GetStyle();
        const float        height = ImGui::GetFrameHeight();
        const float        whole  = SocdPairItemWidth(first, second, hasRemove);
        const float        pair   = whole - (hasRemove ? RemoveWidth() : 0.0f);
        ImDrawList*        list   = ImGui::GetWindowDrawList();
        const ImVec2       min    = ImGui::GetCursorScreenPos();
        const ImVec2       max(min.x + whole, min.y + height);

        events.clicked = ImGui::InvisibleButton("##pair", ImVec2(pair, height));
        events.hovered = ImGui::IsItemHovered();
        if (isShown || events.hovered)
            list->AddRectFilled(min, max, ImGui::GetColorU32(isShown ? ImGuiCol_Header : ImGuiCol_FrameBgHovered),
                                style.FrameRounding);
        if (isShown)
            list->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark), style.FrameRounding, 0,
                          1.5f * ImGui::GetFontSize() / c_Line);

        const float  top     = min.y + (height - CapHeight()) / 2;
        const float  arrows  = min.x + style.FramePadding.x + CapWidth(first);
        const ImU32  violet  = BoardColours::Category(CommandCategory::Behaviour, CategoryUse::Text, KeyFill::Alpha, 0.0f)
                                  .value_or(ImGui::GetColorU32(ImGuiCol_Text));
        DrawCap(list, ImVec2(min.x + style.FramePadding.x, top), first);
        DrawArrows(list, ImVec2(arrows + ArrowsWidth() / 2, min.y + height / 2), violet);
        DrawCap(list, ImVec2(arrows + ArrowsWidth(), top), second);

        if (hasRemove)
        {
            ImGui::SameLine(0.0f, 0.0f);
            events.removed   = ImGui::InvisibleButton("##remove", ImVec2(RemoveWidth(), height));
            const bool over  = ImGui::IsItemHovered();
            if (over)
                ImGui::SetTooltip("Remove this pair");
            const ImVec2 middle(min.x + pair + RemoveWidth() / 2 - style.FramePadding.x / 4, min.y + height / 2);
            const float  arm = ImGui::GetFontSize() * 0.2f;
            const ImU32  ink = over ? ImGui::GetColorU32(ColourOf(PanelColour::Error)) : ImGui::GetColorU32(ColourOf(PanelColour::Muted));
            const float  thick = std::max(1.0f, ImGui::GetFontSize() * 0.1f);
            list->AddLine(ImVec2(middle.x - arm, middle.y - arm), ImVec2(middle.x + arm, middle.y + arm), ink, thick);
            list->AddLine(ImVec2(middle.x - arm, middle.y + arm), ImVec2(middle.x + arm, middle.y - arm), ink, thick);
        }
        return events;
    }

    std::string SocdBriefly(const SocdPairView& pair)
    {
        const std::optional<keychron::SocdChoice> choice = keychron::SocdChoiceOf(pair.type);
        if (!choice)
            return pair.type == keychron::SocdType::DeeperWins
                       ? std::string("the deeper key")
                       : "a choice Nazg does not know (" + std::to_string(static_cast<unsigned>(pair.type)) + ")";
        std::string text = c_Names[static_cast<size_t>(*choice)];
        if (choice == keychron::SocdChoice::AbsolutePriority)
            text += ", " + (pair.type == keychron::SocdType::SecondKeyWins ? pair.second : pair.first) + " wins";
        return text;
    }

    std::string SocdWords(const SocdPairView& pair)
    {
        // Past 3.83 mm on both, where a board's travel reaches it.
        const bool reaches = pair.fullTravel * 6 + 5 > keychron::c_SocdBottomSixtieths;
        switch (pair.type)
        {
        case keychron::SocdType::DeeperThenFirst:
            return "While both are down, the one pressed deeper is sent. Both at the bottom, it is the one held first.";
        case keychron::SocdType::DeeperWins:
            return std::string("While both are down, the one pressed deeper is sent. Both at the bottom, ") +
                   (reaches ? "both are." : "it is the one held first.") +
                   " The Launcher has no choice for it: choose one to replace it.";
        case keychron::SocdType::LastWins:
            return "While both are down, the last one pressed is sent. Let go of it, and the other is sent again.";
        case keychron::SocdType::FirstKeyWins:
        case keychron::SocdType::SecondKeyWins:
        {
            const bool second = pair.type == keychron::SocdType::SecondKeyWins;
            return "While both are down, " + (second ? pair.second : pair.first) + " is always sent. Let go of it, and " +
                   (second ? pair.first : pair.second) + " is sent again.";
        }
        case keychron::SocdType::Neither:
            return "While both are down, neither is sent. Let go of one, and the other is sent again.";
        case keychron::SocdType::Off: break;
        }
        return "A choice Nazg does not know (" + std::to_string(static_cast<unsigned>(pair.type)) +
               "). Nazg leaves it as it is unless another is chosen here.";
    }

    std::optional<keychron::SocdType> DrawSocdEditor(const SocdPairView& pair, const std::string& warning)
    {
        std::optional<keychron::SocdType> chosen;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float       px    = ImGui::GetFontSize() / c_Line;
        ImDrawList*       list  = ImGui::GetWindowDrawList();

        const std::optional<keychron::SocdChoice> current = keychron::SocdChoiceOf(pair.type);
        const float gap    = c_CardGap * px;
        const float width  = std::max(96.0f * px, (ImGui::GetContentRegionAvail().x - 3 * gap) / 4);
        const float header = ImGui::GetFontSize() + 4 * px;
        const float height = header + (c_PictureHeight + 2 * c_CardPad - 4.0f) * px;

        for (size_t index = 0; index < std::size(c_Names); ++index)
        {
            const auto               choice = static_cast<keychron::SocdChoice>(index);
            const keychron::SocdType type   = keychron::SocdTypeFor(choice, pair.type);
            const bool               isOn   = current == choice;
            if (index)
                ImGui::SameLine(0.0f, gap);

            ImGui::PushID(static_cast<int>(index));
            if (ImGui::InvisibleButton("##choice", ImVec2(width, height)) && !isOn)
                chosen = type;
            const bool   hovered = ImGui::IsItemHovered();
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            ImGui::PopID();

            const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
            list->AddRectFilled(min, max, ImGui::GetColorU32(isOn ? ImGuiCol_Header : ImGuiCol_FrameBg), style.FrameRounding);
            list->AddRect(min, max, isOn || hovered ? accent : ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0,
                          isOn ? 2 * px : 1.0f);
            list->PushClipRect(min, max, true);
            TextIn(list, ImGui::GetFontSize() * 0.93f, ImVec2(min.x + c_CardPad * px, min.y + 3 * px),
                   ImVec2(max.x - 3 * px, min.y + 3 * px + header - 2 * px), 0.0f, ImGui::GetColorU32(ImGuiCol_Text),
                   c_Names[index]);
            DrawPicture(list, ImVec2(min.x + c_CardPad * px, min.y + header + 5 * px), width - 2 * c_CardPad * px, px, pair, type);
            list->PopClipRect();
        }
        ImGui::Spacing();

        // Which key wins, then what it does.
        if (current == keychron::SocdChoice::AbsolutePriority)
        {
            const std::pair<keychron::SocdType, const std::string*> winners[] = {
                { keychron::SocdType::FirstKeyWins, &pair.first },
                { keychron::SocdType::SecondKeyWins, &pair.second },
            };
            for (const auto& [type, name] : winners)
            {
                const bool isOn = pair.type == type;
                if (isOn)
                    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button((*name + " wins##" + (type == keychron::SocdType::FirstKeyWins ? "first" : "second")).c_str()) &&
                    !isOn)
                    chosen = type;
                if (isOn)
                    ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, type == keychron::SocdType::FirstKeyWins ? 0.0f : style.ItemSpacing.x);
            }
            ImGui::AlignTextToFramePadding();
        }
        ImGui::PushTextWrapPos(0.0f);
        ColouredText(PanelColour::Muted, "%s", SocdWords(pair).c_str());
        if (!warning.empty())
            ColouredText(PanelColour::Warning, "%s", warning.c_str());
        ImGui::PopTextWrapPos();
        return chosen;
    }

    void SocdTrail::Add(Sample sample)
    {
        // One before the strip's start is kept, for the shape that enters it.
        const double from = sample.time - c_TrailTime - 1.0;
        samples.erase(samples.begin(), std::find_if(samples.begin(), samples.end(),
                                                    [&](const Sample& old) { return old.time >= from; }));
        samples.push_back(sample);
    }

    void DrawSocdTest(const SocdTestInput& input)
    {
        const float  px     = ImGui::GetFontSize() / c_Line;
        const float  size   = ImGui::GetFontSize() * 0.86f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float  room   = ImGui::GetContentRegionAvail().x;
        const float  words  = std::max(ImGui::CalcTextSize("held back").x, ImGui::CalcTextSize("0.00 mm").x);
        const float  left   = origin.x + ImGui::GetFontSize() * 2.6f;
        const float  right  = origin.x + std::max(room - words - 8 * px, ImGui::GetFontSize() * 8.0f);
        ImDrawList*  list   = ImGui::GetWindowDrawList();
        ImGui::Dummy(ImVec2(room, c_TestHeight * px));

        const ImU32  text   = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32  muted  = ImGui::GetColorU32(ColourOf(PanelColour::Muted));
        const ImU32  border = ImGui::GetColorU32(ImGuiCol_Border);
        const double now    = ImGui::GetTime();
        const auto   x      = [&](double time)
        { return std::max(left, right - static_cast<float>((now - time) / c_TrailTime) * (right - left)); };

        static const std::vector<SocdTrail::Sample> c_None;
        const std::vector<SocdTrail::Sample>&       samples = input.trail ? input.trail->samples : c_None;

        // A lane: the key's travel hanging from its line, how far it is now and what becomes of it.
        const auto lane = [&](float top, ImU32 colour, const std::string& name, float SocdTrail::Sample::*depth,
                              const std::string& travel, bool sent, bool heldBack)
        {
            const float y = origin.y + top * px, high = c_TestLane * px;
            TextIn(list, ImGui::GetFontSize(), ImVec2(origin.x, y), ImVec2(left - 6 * px, y + high), 1.0f, text, name);
            list->AddLine(ImVec2(left, y), ImVec2(right, y), border);
            const ImU32 fill = Faint(colour, 0.5f);
            // Quads side by side: anti-aliased, their edges would show as stripes.
            const ImDrawListFlags flags = list->Flags;
            list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (size_t index = 0; index < samples.size(); ++index)
            {
                const double until = index + 1 < samples.size() ? samples[index + 1].time : now;
                const float  down  = std::min(1.0f, samples[index].*depth);
                const float  next  = index + 1 < samples.size() ? std::min(1.0f, samples[index + 1].*depth) : down;
                const float  x0 = x(samples[index].time), x1 = x(until);
                if (x1 <= left || (down <= 0.0f && next <= 0.0f))
                    continue;
                list->AddQuadFilled(ImVec2(x0, y), ImVec2(x1, y), ImVec2(x1, y + high * next), ImVec2(x0, y + high * down), fill);
            }
            list->Flags = flags;
            const float at = right + 8 * px;
            TextIn(list, size, ImVec2(at, y - 2 * px), ImVec2(at + words, y + high / 2), 0.0f, muted, travel);
            if (sent || heldBack)
                TextIn(list, size, ImVec2(at, y + high / 2 - 1 * px), ImVec2(at + words, y + high + 2 * px), 0.0f,
                       sent ? ImGui::GetColorU32(ColourOf(PanelColour::Success)) : text, sent ? "sent" : "held back");
        };
        lane(2.0f, FirstColour(), input.pair.first, &SocdTrail::Sample::first, input.firstTravel, input.firstSent,
             input.firstHeldBack);
        lane(2.0f + c_TestApart, SecondColour(), input.pair.second, &SocdTrail::Sample::second, input.secondTravel,
             input.secondSent, input.secondHeldBack);

        // What was sent, in runs, named where there is room.
        const float top = origin.y + c_TestSent * px, bottom = top + 16 * px;
        TextIn(list, size, ImVec2(origin.x, top), ImVec2(left - 6 * px, bottom), 1.0f, muted, "sent");
        list->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), border);
        const auto runs = [&](bool SocdTrail::Sample::*sent, ImU32 colour, const std::string& name)
        {
            for (size_t index = 0; index < samples.size();)
            {
                if (!(samples[index].*sent))
                {
                    ++index;
                    continue;
                }
                size_t end = index;
                while (end < samples.size() && samples[end].*sent)
                    ++end;
                const ImVec2 min(x(samples[index].time), top), max(end < samples.size() ? x(samples[end].time) : right, bottom);
                if (max.x > left)
                {
                    list->AddRectFilled(min, ImVec2(std::max(max.x, min.x + 1.0f), max.y), colour, 2 * px);
                    if (max.x - min.x > ImGui::GetFontSize() * 0.9f)
                        TextIn(list, size, min, max, 0.5f, InkOn(colour), name);
                }
                index = end;
            }
        };
        runs(&SocdTrail::Sample::firstSent, FirstColour(), input.pair.first);
        runs(&SocdTrail::Sample::secondSent, SecondColour(), input.pair.second);
    }
}
