// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

// A render-only view of the production TitleMenu state. This header deliberately
// owns no network, installation policy, console API or application lifetime.
namespace sphaira::ui::shop {
inline auto DisplayGenre(std::string_view genres) -> std::string {
    static constexpr std::pair<std::string_view, std::string_view> translations[] = {
        {"Action", "Acción"}, {"Adventure", "Aventura"}, {"Arcade", "Arcade"}, {"Board Game", "Juego de mesa"},
        {"Education", "Educativo"}, {"Fighting", "Lucha"}, {"First-Person Shooter", "Disparos en primera persona"},
        {"Fitness", "Ejercicio"}, {"Lifestyle", "Estilo de vida"}, {"Music", "Música"},
        {"Multiplayer", "Multijugador"}, {"Party", "Fiesta"}, {"Platformer", "Plataformas"},
        {"Puzzle", "Puzles"}, {"Racing", "Carreras"}, {"Role-Playing", "Rol"}, {"RPG", "Rol"},
        {"Shooter", "Disparos"}, {"Simulation", "Simulación"}, {"Sports", "Deportes"},
        {"Strategy", "Estrategia"}, {"Training", "Entrenamiento"}
    };
    std::string result;
    while (!genres.empty()) {
        const auto comma = genres.find(',');
        auto item = genres.substr(0, comma);
        const auto begin = item.find_first_not_of(" \t");
        if (begin != std::string_view::npos) {
            item = item.substr(begin, item.find_last_not_of(" \t") - begin + 1);
            for (const auto& [source, translated] : translations) {
                if (item == source) { item = translated; break; }
            }
            if (!result.empty()) result += ", ";
            result += item;
        }
        if (comma == std::string_view::npos) break;
        genres.remove_prefix(comma + 1);
    }
    return result;
}

struct DetailPanelState {
    const char* title{"Selecciona un juego"};
    const char* note{"Marca el contenido que quieres instalar"};
    const char* footer{""};
    const char* metadata{""};
    const char* genre{""};
    const char* description{""};
    int artwork{};
    bool ready{};
    std::array<bool, 3> selected{};
    std::array<bool, 3> available{};
    const char* primary_label{"Instalar paquete"};
    bool primary_enabled{};
    bool queue_enabled{};
    bool focused{};
    int action{3};
};
inline auto DetailActionBounds(Rect bounds, int action) -> Rect {
    const auto sx = bounds.w / 376.f, sy = bounds.h / 505.f;
    if (action < 3) return {bounds.x + 28.f * sx, bounds.y + (254.f + action * 29.f) * sy, bounds.w - 56.f * sx, 29.f * sy};
    return {bounds.x + 18.f * sx, bounds.y + (action == 3 ? 371.f : 433.f) * sy,
        bounds.w - 36.f * sx, (action == 3 ? 48.f : 44.f) * sy};
}
// Limit metadata to complete lines. The final ellipsis trims whole UTF-8
// codepoints, so long translated titles cannot spill into the next control.
inline void DetailText(NVGcontext* vg, Rect bounds, float size, NVGcolor colour,
        const char* value, int max_lines = 1, bool bold = false) {
    if (!value || !*value) return;
    nvgSave(vg);
    nvgIntersectScissor(vg, bounds.x, bounds.y, bounds.w, bounds.h);
    nvgFontFace(vg, bold && nvgFindFont(vg, "ShopBold") >= 0 ? "ShopBold" : "Standard");
    nvgFontSize(vg, size); nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, colour);
    NVGtextRow rows[4]{};
    const auto count = nvgTextBreakLines(vg, value, nullptr, bounds.w, rows, std::min(max_lines, 4));
    for (int i = 0; i < count; ++i) {
        auto line = std::string(rows[i].start, rows[i].end);
        if (i == max_lines - 1 && rows[i].next && *rows[i].next) {
            const std::string suffix = "…";
            while (!line.empty() && nvgTextBounds(vg, 0, 0, (line + suffix).c_str(), nullptr, nullptr) > bounds.w) {
                auto last = line.size() - 1;
                while (last && (static_cast<unsigned char>(line[last]) & 0xc0) == 0x80) --last;
                line.resize(last);
            }
            line += suffix;
        }
        nvgText(vg, bounds.x, bounds.y + float(i) * size * 1.15f, line.c_str(), nullptr);
    }
    nvgRestore(vg);
}
inline void DrawDetailPanel(NVGcontext* vg, Rect bounds, const DetailPanelState& state) {
    const auto sx = bounds.w / 376.f, sy = bounds.h / 505.f;
    const auto scale = std::min(sx, sy);
    Panel(vg, bounds, Violet, false, 11.f);
    nvgSave(vg); nvgIntersectScissor(vg, bounds.x + 1.f, bounds.y + 1.f, bounds.w - 2.f, bounds.h - 2.f);
    const Rect hero{bounds.x + 1.f, bounds.y + 1.f, bounds.w - 2.f, bounds.h - 2.f};
    if (state.artwork) ImageCover(vg, hero, state.artwork, 9.f);
    else {
        Fill(vg, hero, PanelTop);
    }
    nvgBeginPath(vg); nvgRect(vg, hero.x, hero.y, hero.w, hero.h);
    nvgFillPaint(vg, nvgLinearGradient(vg, hero.x, hero.y, hero.x, hero.y + hero.h,
        nvgRGBA(3, 12, 24, 165), nvgRGBA(3, 12, 24, 242))); nvgFill(vg);
    const auto area = [&](float y, float height) -> Rect {
        return {bounds.x + 30.f * sx, bounds.y + y * sy, bounds.w - 50.f * sx, height * sy};
    };
    Fill(vg, {bounds.x + 17.f * sx, bounds.y + 21.f * sy, 5.f * sx, 29.f * sy}, Cyan, 0.f);
    DetailText(vg, area(18.f, 60.f), 25.f * scale, White, state.title, 2, true);
    DetailText(vg, area(82.f, 19.f), 15.f * scale, Muted, state.metadata);
    if (state.genre && *state.genre) {
        DetailText(vg, area(106.f, 20.f), 15.f * scale, Cyan, "Género", 1, true);
        auto genre = area(106.f, 20.f);
        genre.x += 55.f * sx;
        genre.w -= 55.f * sx;
        DetailText(vg, genre, 16.f * scale, White, state.genre);
    }
    DetailText(vg, area(134.f, 79.f), 16.f * scale, White, state.description, 4);
    const Rect contents{bounds.x + 16.f * sx, bounds.y + 226.f * sy, bounds.w - 32.f * sx, 119.f * sy};
    Fill(vg, contents, nvgRGBA(3, 12, 24, 160), 8.f);
    Outline(vg, contents, Border, 1.f, 8.f);
    nvgBeginPath(vg); nvgRoundedRect(vg, contents.x + 1.f, contents.y + 1.f, contents.w - 2.f, 28.f * sy, 7.f);
    nvgFillPaint(vg, nvgLinearGradient(vg, contents.x, contents.y, contents.x + contents.w, contents.y,
        nvgRGBA(116, 44, 205, 105), nvgRGBA(49, 36, 82, 155))); nvgFill(vg);
    DrawIcon(vg, Icon::Queue, bounds.x + 32.f * sx, bounds.y + 232.f * sy, 17.f * scale, White);
    Text(vg, bounds.x + 59.f * sx, bounds.y + 240.f * sy, 15.f * scale, White, "CONTENIDO DEL SERVIDOR", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    const char* labels[] = {"Juego base", "Última actualización", "DLC disponibles"};
    for (int row = 0; row < 3; ++row) {
        const auto area = DetailActionBounds(bounds, row);
        const auto focused = state.focused && state.action == row;
        if (focused) Fill(vg, area, nvgRGBA(0, 238, 252, 17), 6.f);
        Checkbox(vg, {bounds.x + 35.f * sx, bounds.y + (259.f + row * 29.f) * sy, 20.f * scale, 20.f * scale},
            state.ready && state.selected[row], state.available[row], focused);
        Text(vg, bounds.x + 66.f * sx, bounds.y + (269.f + row * 29.f) * sy, 17.f * scale,
            state.available[row] ? White : Muted, labels[row]);
    }
    DetailText(vg, area(351.f, 17.f), 12.f * scale, Muted, state.note);
    Button(vg, DetailActionBounds(bounds, 3), state.primary_label, state.focused && state.action == 3, state.primary_enabled,
        Icon::Download, true, 22.f * scale);
    Button(vg, DetailActionBounds(bounds, 4), "Añadir a la cola", state.focused && state.action == 4, false,
        Icon::Plus, true, 20.f * scale);
    Outline(vg, DetailActionBounds(bounds, 4), Cyan, 1.5f);
    if (!state.primary_enabled) Fill(vg, DetailActionBounds(bounds, 3), nvgRGBA(3, 12, 24, 105), 9.f);
    if (!state.queue_enabled) Fill(vg, DetailActionBounds(bounds, 4), nvgRGBA(3, 12, 24, 90), 9.f);
    Text(vg, bounds.x + bounds.w * .5f, bounds.y + 487.f * sy, 13.f * scale, Muted, state.footer, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg); Outline(vg, bounds, Violet, 1.f, 11.f);
}
} // namespace sphaira::ui::shop
