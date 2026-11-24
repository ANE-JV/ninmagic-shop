// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include <cstddef>

namespace sphaira::ui::shop::layout {
// A single coordinate system for the Switch renderer, touch and desktop QA.
inline constexpr Rect Sidebar{24,108,204,508};
inline constexpr Rect Grid{240,180,686,433};
inline constexpr Rect Card{244,184,130,102};
inline constexpr Rect ListCard{244,184,678,37};
inline constexpr Rect Search{244,140,394,37};
inline constexpr Rect Sections{646,140,123,37};
inline constexpr Rect ViewMode{777,140,145,37};
inline constexpr Rect Detail{936,103,320,450};
inline constexpr Rect Downloads{38,444,176,35};
inline constexpr Rect Settings{38,483,176,35};
inline constexpr Rect UpdateApp{38,522,176,35};
inline constexpr Rect ProfileUpdate{49,550,214,42};
inline constexpr Rect PlatformViewport{38,159,176,278};
inline constexpr std::size_t Columns{5};
inline constexpr std::size_t VisibleCards{20}, VisibleListRows{10};
inline constexpr std::size_t VisiblePlatforms{6};
inline constexpr float ColumnGap{7}, RowGap{6}, ListRowGap{6};
inline constexpr float GridScrollX{925}, GridScrollY{184}, GridScrollHeight{428};
inline constexpr float PlatformStep{43};

inline constexpr Rect PlatformRow(std::size_t row) {
    return {38,169+float(row)*PlatformStep,176,37};
}
inline constexpr Rect CardAt(std::size_t index) {
    return {Card.x+float(index%Columns)*(Card.w+ColumnGap),
        Card.y+float(index/Columns)*(Card.h+RowGap),Card.w,Card.h};
}
inline constexpr Rect ListAt(std::size_t index) {
    return {ListCard.x, ListCard.y + float(index) * (ListCard.h + ListRowGap), ListCard.w, ListCard.h};
}
}
