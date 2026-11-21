// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>

namespace sphaira::ui::shop {
struct PageTarget { std::int64_t page{}, focus{}; };

inline auto AdjacentPage(std::int64_t page, std::int64_t page_count, std::int64_t page_size,
    std::int64_t columns, std::int64_t selected, bool bottom, bool top) -> std::optional<PageTarget> {
    const auto column = selected % columns;
    if (bottom && page + 1 < page_count) return PageTarget{page + 1, column};
    if (top && page > 0) return PageTarget{page - 1, page_size - columns + column};
    return {};
}
}
