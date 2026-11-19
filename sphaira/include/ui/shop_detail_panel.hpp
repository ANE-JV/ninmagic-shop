// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include <array>
#include <cstring>

// A render-only view of the production TitleMenu state. This header deliberately
// owns no network, installation policy, console API or application lifetime.
namespace sphaira::ui::shop {
struct DetailPanelState {
    const char* title{"Selecciona un juego"};
    const char* note{"Marca el contenido que quieres instalar"};
    const char* footer{""};
    int artwork{};
    bool ready{};
    std::array<bool, 3> selected{};
    std::array<bool, 3> available{};
    bool primary_enabled{};
    bool queue_enabled{};
    bool focused{};
    int action{3};
};
inline auto DetailActionBounds(Rect bounds, int action) -> Rect {
    const auto sx = bounds.w / 376.f, sy = bounds.h / 505.f;
    if (action < 3) return {bounds.x + 30.f * sx, bounds.y + (183.f + action * 36.f) * sy, bounds.w - 60.f * sx, 36.f * sy};
    return {bounds.x + 18.f * sx, bounds.y + (action == 3 ? 359.f : 426.f) * sy,
        bounds.w - 36.f * sx, (action == 3 ? 54.f : 46.f) * sy};
}
inline void DrawDetailPanel(NVGcontext* vg, Rect bounds, const DetailPanelState& state) {
    const auto sx = bounds.w / 376.f, sy = bounds.h / 505.f;
    const auto scale = std::min(sx, sy);
    Panel(vg, bounds, Violet, false, 11.f);
    nvgSave(vg); nvgIntersectScissor(vg, bounds.x + 1.f, bounds.y + 1.f, bounds.w - 2.f, bounds.h - 2.f);
    const Rect hero{bounds.x + 1.f, bounds.y + 1.f, bounds.w - 2.f, 124.f * sy};
    if (state.artwork) ImageCover(vg, hero, state.artwork, 9.f);
    else {
        Fill(vg, hero, PanelTop);
        DrawIcon(vg, Icon::Folder, hero.x + hero.w * .5f - 22.f, hero.y + 20.f, 44.f, Border);
    }
    nvgBeginPath(vg); nvgRect(vg, hero.x, hero.y + 25.f * sy, hero.w, 113.f * sy);
    nvgFillPaint(vg, nvgLinearGradient(vg, hero.x, hero.y + 30.f * sy, hero.x, hero.y + 133.f * sy,
        nvgRGBA(3, 12, 24, 0), nvgRGBA(3, 12, 24, 250))); nvgFill(vg);
    Fill(vg, {bounds.x + 17.f * sx, bounds.y + 96.f * sy, 5.f * sx, 22.f * sy}, Cyan, 0.f);
    nvgSave(vg); nvgIntersectScissor(vg, bounds.x + 29.f * sx, bounds.y + 91.f * sy, bounds.w - 42.f * sx, 48.f * sy);
    nvgFontFace(vg, nvgFindFont(vg, "ShopBold") >= 0 ? "ShopBold" : "Standard");
    nvgTextLineHeight(vg, 1.08f); nvgFontSize(vg, (std::strlen(state.title) > 34 ? 21.f : 24.f) * scale);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP); nvgFillColor(vg, White);
    nvgTextBox(vg, bounds.x + 30.f * sx, bounds.y + 93.f * sy, bounds.w - 42.f * sx, state.title, nullptr);
    nvgRestore(vg);
    const Rect contents{bounds.x + 16.f * sx, bounds.y + 140.f * sy, bounds.w - 32.f * sx, 205.f * sy};
    Panel(vg, contents, Border, false, 8.f);
    nvgBeginPath(vg); nvgRoundedRect(vg, contents.x + 1.f, contents.y + 1.f, contents.w - 2.f, 36.f * sy, 7.f);
    nvgFillPaint(vg, nvgLinearGradient(vg, contents.x, contents.y, contents.x + contents.w, contents.y,
        nvgRGBA(116, 44, 205, 105), nvgRGBA(49, 36, 82, 155))); nvgFill(vg);
    DrawIcon(vg, Icon::Queue, bounds.x + 40.f * sx, bounds.y + 148.f * sy, 22.f * scale, White);
    Text(vg, bounds.x + 72.f * sx, bounds.y + 160.f * sy, 17.f * scale, White, "CONTENIDO DEL SERVIDOR", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    const char* labels[] = {"Juego base", "Última actualización", "DLC disponibles"};
    for (int row = 0; row < 3; ++row) {
        const auto area = DetailActionBounds(bounds, row);
        const auto focused = state.focused && state.action == row;
        if (focused) Fill(vg, area, nvgRGBA(0, 238, 252, 17), 6.f);
        Checkbox(vg, {bounds.x + 40.f * sx, bounds.y + (190.f + row * 36.f) * sy, 22.f * scale, 22.f * scale},
            state.ready && state.selected[row], state.available[row], focused);
        Text(vg, bounds.x + 79.f * sx, bounds.y + (201.f + row * 36.f) * sy, 19.f * scale,
            state.available[row] ? White : Muted, labels[row]);
    }
    Line(vg, bounds.x + 30.f * sx, bounds.y + 302.f * sy, bounds.x + bounds.w - 30.f * sx, bounds.y + 302.f * sy, Border);
    Text(vg, bounds.x + bounds.w * .5f, bounds.y + 325.f * sy, 16.f * scale, Muted, state.note, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    Button(vg, DetailActionBounds(bounds, 3), "Instalar paquete", state.focused && state.action == 3, state.primary_enabled,
        Icon::Download, true, 23.f * scale);
    Button(vg, DetailActionBounds(bounds, 4), "Añadir a la cola", state.focused && state.action == 4, false,
        Icon::Plus, true, 21.f * scale);
    Outline(vg, DetailActionBounds(bounds, 4), Cyan, 1.5f);
    if (!state.primary_enabled) Fill(vg, DetailActionBounds(bounds, 3), nvgRGBA(3, 12, 24, 105), 9.f);
    if (!state.queue_enabled) Fill(vg, DetailActionBounds(bounds, 4), nvgRGBA(3, 12, 24, 90), 9.f);
    Text(vg, bounds.x + bounds.w * .5f, bounds.y + 487.f * sy, 13.f * scale, Muted, state.footer, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg); Outline(vg, bounds, Violet, 1.f, 11.f);
}
} // namespace sphaira::ui::shop
