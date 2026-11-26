// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/shop_catalog_panel.hpp"
#include "ui/shop_navigation.hpp"
#include <span>

namespace sphaira::ui::shop {

inline constexpr Rect WaitingPanel{468, 147, 784, 472};
inline constexpr Rect WaitingViewport{486, 193, 754, 407};
inline constexpr Rect WaitingScrollbar{1236, 193, 4, 407};
inline constexpr float WaitingRowStep = 41.f;

inline constexpr auto WaitingRow(std::size_t index) -> Rect {
    return {WaitingViewport.x, WaitingViewport.y + WaitingRowStep * static_cast<float>(index), 738, 38};
}

struct TransferCard {
    std::string_view name;
    int image{};
    const char* status{"Preparando"};
    std::string amount, speed, remaining;
    bool file_progress{};
    float fraction{-1.f};
};

struct WaitingGame {
    std::string_view name;
    int image{};
};

inline void DrawActiveTransfer(NVGcontext* vg, const TransferCard& transfer) {
    const Rect panel{27, 147, 423, 472};
    const Rect cover{137, 243, 203, 203};
    Panel(vg, panel, Cyan, false, 15.f);
    if (transfer.name.empty()) {
        DrawIcon(vg, Icon::Download, 212, 308, 52, Cyan);
        Text(vg, 238.5f, 391, 21, White, "No hay juegos en curso", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
        return;
    }

    constexpr auto align_left = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
    constexpr auto align_right = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
    nvgFontFace(vg, nvgFindFont(vg, "ShopBold") >= 0 ? "ShopBold" : "Standard");
    nvgFontSize(vg, 18);
    const auto status_width = nvgTextBounds(vg, 0, 0, transfer.status, nullptr, nullptr);
    const Rect status{48, 165, status_width + 48.f, 32};
    Fill(vg, status, Alpha(Cyan, .13f), 16.f);
    Outline(vg, status, Alpha(Cyan, .65f), 1.f, 16.f);
    nvgBeginPath(vg);
    nvgCircle(vg, 64, 181, 4.f);
    nvgFillColor(vg, Cyan);
    nvgFill(vg);
    Text(vg, 77, 181, 18, Cyan, transfer.status, align_left, true);

    CatalogLabel(vg, {48, 201, 382, 37}, transfer.name, 29.f, White, true, 23.f);
    if (transfer.image > 0) ImageCover(vg, cover, transfer.image, 10.f);
    else {
        Fill(vg, cover, PanelTop, 10.f);
        DrawIcon(vg, Icon::Queue, 212, 318, 53, Muted);
    }
    Outline(vg, cover, Border, 1.f, 10.f);

    const Rect progress{48, 496, 380, 16};
    Fill(vg, progress, nvgRGB(24, 49, 72), 8.f);
    Outline(vg, progress, Border, 1.f, 8.f);
    if (transfer.fraction >= 0.f) {
        const auto fraction = std::clamp(transfer.fraction, 0.f, 1.f);
        const auto percent = std::to_string(static_cast<unsigned>(fraction * 100.f + .5f)) + " %";
        if (transfer.file_progress) Text(vg, 48, 459, 14, Muted, "Archivo actual");
        CatalogLabel(vg, {48, 466, 285, 26}, transfer.amount, 21.f, White, true, 15.f);
        Text(vg, 428, 476, 25, Cyan, percent.c_str(), align_right, true);
        if (fraction > 0.f) {
            const auto width = progress.w * fraction;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, progress.x, progress.y, width, progress.h, std::min(8.f, width * .5f));
            nvgFillPaint(vg, nvgLinearGradient(vg, progress.x, progress.y, progress.x + width, progress.y,
                nvgRGB(20, 175, 231), Cyan));
            nvgFill(vg);
        }
    } else {
        Text(vg, 48, 476, 18, Muted, "Calculando progreso…");
    }

    Line(vg, 48, 530, 428, 530, Border);
    Text(vg, 48, 552, 17, Muted, "Velocidad de descarga");
    CatalogLabel(vg, {48, 570, 174, 27}, transfer.speed, 25.f);
    Line(vg, 230, 547, 230, 596, Border);
    Text(vg, 249, 552, 16, Muted, transfer.file_progress ? "Tiempo del archivo aprox." : "Tiempo restante aprox.");
    CatalogLabel(vg, {249, 570, 179, 27}, transfer.remaining, 25.f);
}

inline void DrawWaitingGames(NVGcontext* vg, std::span<const WaitingGame> games, const QueueScroll& scroll, std::size_t count) {
    Panel(vg, WaitingPanel, Border, false, 15.f);
    Text(vg, 486, 171, 23, White, "En cola", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    if (games.empty()) {
        Text(vg, 860, 375, 21, Muted, "No hay juegos en cola", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }
    const auto range = std::to_string(scroll.first + 1) + "–" + std::to_string(scroll.first + games.size()) + " de " + std::to_string(count);
    Text(vg, 1239, 171, 20, Muted, range.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, true);

    for (std::size_t row{}; row < games.size(); ++row) {
        const auto position = scroll.first + row;
        const auto bounds = WaitingRow(row);
        const auto& entry = games[row];
        const bool focused = position == scroll.selected;
        Fill(vg, bounds, focused ? nvgRGB(14, 49, 65) : nvgRGB(11, 28, 45), 7.f);
        Outline(vg, bounds, Alpha(focused ? Cyan : Border, .74f), 1.f, 7.f);
        const auto number = std::to_string(position + 1);
        Text(vg, bounds.x + 22.f, bounds.y + 19.f, 17, focused ? Cyan : Muted, number.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
        const Rect cover{bounds.x + 44.f, bounds.y + 4.f, 30, 30};
        if (entry.image > 0) ImageCover(vg, cover, entry.image, 4.f);
        else DrawIcon(vg, Icon::Queue, cover.x + 4.f, cover.y + 4.f, 22.f, Muted);
        CatalogLabel(vg, {bounds.x + 88.f, bounds.y, bounds.w - 105.f, bounds.h}, entry.name, 19.f);
        if (focused) FocusRing(vg, bounds, 7.f);
    }

    if (count > VisibleQueuedGames) {
        const auto thumb = std::max(15.f, WaitingScrollbar.h * static_cast<float>(VisibleQueuedGames) / static_cast<float>(count));
        const auto offset = (WaitingScrollbar.h - thumb) * static_cast<float>(scroll.first) / static_cast<float>(QueueScroll::LastOffset(count));
        Fill(vg, WaitingScrollbar, Alpha(Cyan, .24f), 2.f);
        Fill(vg, {WaitingScrollbar.x, WaitingScrollbar.y + offset, WaitingScrollbar.w, thumb}, Cyan, 2.f);
    }
}

inline void DrawTransferPanel(NVGcontext* vg, const TransferCard& transfer, std::span<const WaitingGame> waiting,
        const QueueScroll& scroll, std::size_t count) {
    Line(vg, 39, 108, 30, 133, Cyan, 6);
    Text(vg, 53, 121, 35, White, "En curso", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    DrawActiveTransfer(vg, transfer);
    DrawWaitingGames(vg, waiting, scroll, count);
}

} // namespace sphaira::ui::shop
