// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/shop_style.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace sphaira::ui::shop::message {

inline constexpr Rect Box{132.f, 105.f, 1016.f, 510.f};
inline constexpr Rect Body{174.f, 191.f, 920.f, 300.f};
inline constexpr Rect CancelButton{648.f, 540.f, 212.f, 54.f};
inline constexpr Rect OkButton{886.f, 540.f, 212.f, 54.f};
inline constexpr float FontSize{24.f};
inline constexpr float LineHeight{30.f};
inline constexpr int VisibleLines{static_cast<int>(Body.h / LineHeight)};

inline auto Wrap(NVGcontext* vg, std::string_view text) -> std::vector<std::string_view> {
    if (text.empty()) return {};
    std::vector<std::string_view> lines;
    nvgSave(vg);
    nvgFontFace(vg, "Standard");
    nvgFontSize(vg, FontSize);
    const char* next = text.data();
    const char* end = next + text.size();
    NVGtextRow rows[16];

    while (next < end) {
        const auto count = nvgTextBreakLines(vg, next, end, Body.w, rows, 16);
        if (!count) break;
        for (int i = 0; i < count; ++i) {
            lines.emplace_back(rows[i].start, static_cast<std::size_t>(rows[i].end - rows[i].start));
        }
        next = rows[count - 1].next;
    }

    nvgRestore(vg);
    return lines;
}

inline void Draw(NVGcontext* vg, std::span<const std::string_view> lines, int top) {
    Fill(vg, {0.f, 0.f, 1280.f, 720.f}, nvgRGBA(0, 0, 0, 176), 0.f);
    Panel(vg, Box, Cyan);
    Text(vg, Box.x + Box.w / 2.f, 146.f, 34.f, White, "Bienvenido a NinMagic Shop", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
    Line(vg, Body.x, 174.f, Body.x + Body.w, 174.f, Border);

    nvgSave(vg);
    nvgIntersectScissor(vg, Body.x, Body.y, Body.w, Body.h);
    nvgFontFace(vg, "Standard");
    nvgFontSize(vg, FontSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, White);

    const int count = static_cast<int>(lines.size());
    const int bottom = std::min(count, top + VisibleLines);
    for (int i = top; i < bottom; ++i) {
        const auto line = lines[i];
        if (!line.empty()) {
            nvgText(vg, Body.x, Body.y + (i - top) * LineHeight, line.data(), line.data() + line.size());
        }
    }
    nvgRestore(vg);

    if (count > VisibleLines) {
        const float x = Body.x + Body.w + 14.f;
        const float thumb = std::max(16.f, Body.h * VisibleLines / count);
        const float offset = (Body.h - thumb) * top / (count - VisibleLines);
        Fill(vg, {x, Body.y, 4.f, Body.h}, Border, 2.f);
        Fill(vg, {x, Body.y + offset, 4.f, thumb}, Cyan, 2.f);
        Text(vg, Body.x, 567.f, 20.f, Muted, "↑ ↓ o desliza para leer más");
    }

    Line(vg, Body.x, 523.f, Body.x + Body.w, 523.f, Border);
    Button(vg, CancelButton, "B  Cancelar");
    Button(vg, OkButton, "A  OK", true, true);
}

} // namespace sphaira::ui::shop::message
