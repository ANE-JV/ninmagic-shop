// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include <array>
#include <string_view>

namespace sphaira::ui::shop {
inline auto ConsoleLogoSlot(std::string_view platform) -> std::size_t {
    // Row-major slots in romfs:/theme/shop-console-logos.png.
    constexpr std::array<std::string_view, 32> labels{
        "__unknown__", "Nintendo Switch", "Ports", "Wii", "GameCube", "Nintendo 3DS / 2DS", "Nintendo DS", "Nintendo 64",
        "NES", "Super Nintendo", "Game Boy", "Game Boy Color", "Game Boy Advance", "PlayStation", "PSP", "Mega Drive",
        "Master System", "Game Gear", "Dreamcast", "Sega CD", "Sega 32X", "Sega Saturn", "ColecoVision", "Commodore 64",
        "Atari 7800", "Atari Jaguar", "PC Engine", "Arcade", "Neo Geo", "Neo Geo Pocket", "Wii U", "Nintendo Switch 2",
    };
    // Some servers return their display labels directly.
    if (platform == "Nintendo Wii") platform = "Wii";
    if (platform == "Nintendo Wii U") platform = "Wii U";
    if (platform == "Nintendo GameCube" || platform == "Gamecube") platform = "GameCube";
    if (platform == "Nintendo 3DS" || platform == "Nintendo 2DS") platform = "Nintendo 3DS / 2DS";
    const auto found = std::find(labels.begin(), labels.end(), platform);
    return found == labels.end() ? 0 : static_cast<std::size_t>(found - labels.begin());
}

inline void DrawConsoleLogo(NVGcontext* vg, Rect area, int atlas, std::string_view platform) {
    const auto slot = ConsoleLogoSlot(platform);
    if (atlas <= 0 || slot == 0) {
        DrawIcon(vg, Icon::Folder, area.x + 4.f, area.y + 4.f, area.w - 8.f, Muted);
        return;
    }
    constexpr std::size_t columns = 8, rows = 4;
    const float column = static_cast<float>(slot % columns), row = static_cast<float>(slot / columns);
    const auto paint = nvgImagePattern(vg, area.x - column * area.w, area.y - row * area.h,
        columns * area.w, rows * area.h, 0.f, atlas, 1.f);
    nvgBeginPath(vg);
    nvgRect(vg, area.x, area.y, area.w, area.h);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
}
}
