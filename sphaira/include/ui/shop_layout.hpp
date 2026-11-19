// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include <cstddef>

namespace sphaira::ui::shop::layout {
// A single coordinate system for the Switch renderer, touch and desktop QA.
inline constexpr Rect Sidebar{24,108,204,508};
inline constexpr Rect Grid{240,222,630,388};
inline constexpr Rect Card{244,226,118,186};
inline constexpr Rect Search{244,178,458,38};
inline constexpr Rect Sections{714,178,152,38};
inline constexpr Rect Detail{882,108,374,505};
inline constexpr Rect Downloads{38,454,176,40};
inline constexpr Rect Settings{38,501,176,40};
inline constexpr Rect PlatformViewport{38,159,176,278};
inline constexpr std::size_t Columns{5};
inline constexpr std::size_t VisibleCards{10};
inline constexpr std::size_t VisiblePlatforms{6};
inline constexpr float ColumnGap{8}, RowGap{8};
inline constexpr float GridScrollX{870}, GridScrollY{230}, GridScrollHeight{376};
inline constexpr float PlatformStep{43};

inline constexpr Rect PlatformRow(std::size_t row) {
    return {38,169+float(row)*PlatformStep,176,37};
}
inline constexpr Rect CardAt(std::size_t index) {
    return {Card.x+float(index%Columns)*(Card.w+ColumnGap),
        Card.y+float(index/Columns)*(Card.h+RowGap),Card.w,Card.h};
}
}
