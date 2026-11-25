// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include <cstddef>

namespace sphaira::ui::shop::layout {
// A single coordinate system for the Switch renderer, touch and desktop QA.
inline constexpr Rect Sidebar{24,108,204,480};
inline constexpr Rect Grid{240,180,686,400};
inline constexpr Rect Card{244,184,130,94};
inline constexpr Rect ListCard{244,184,678,34};
inline constexpr Rect Search{244,140,394,37};
inline constexpr Rect Sections{646,140,123,37};
inline constexpr Rect ViewMode{777,140,145,37};
inline constexpr Rect Detail{936,103,320,450};
inline constexpr Rect Downloads{362,602,176,32};
inline constexpr Rect Settings{552,602,176,32};
inline constexpr Rect UpdateApp{742,602,176,32};
inline constexpr Rect Storage{1044,602,196,32};
inline constexpr Rect ProfileUpdate{49,550,214,42};
inline constexpr Rect PlatformViewport{38,159,176,422};
inline constexpr std::size_t Columns{5};
inline constexpr std::size_t VisibleCards{20}, VisibleListRows{10};
inline constexpr std::size_t VisiblePlatforms{11};
inline constexpr float ColumnGap{7}, RowGap{6}, ListRowGap{5};
inline constexpr float GridScrollX{925}, GridScrollY{184}, GridScrollHeight{396};
inline constexpr float PlatformStep{38};

inline constexpr Rect PlatformRow(std::size_t row) {
    return {38,163+float(row)*PlatformStep,176,33};
}
inline constexpr Rect CardAt(std::size_t index) {
    return {Card.x+float(index%Columns)*(Card.w+ColumnGap),
        Card.y+float(index/Columns)*(Card.h+RowGap),Card.w,Card.h};
}
inline constexpr Rect ListAt(std::size_t index) {
    return {ListCard.x, ListCard.y + float(index) * (ListCard.h + ListRowGap), ListCard.w, ListCard.h};
}
}
