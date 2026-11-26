// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
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

inline constexpr std::size_t VisibleQueuedGames = 10;

struct QueueScroll {
    std::size_t first{}, selected{};

    static constexpr auto LastOffset(std::size_t count) -> std::size_t {
        return count > VisibleQueuedGames ? count - VisibleQueuedGames : 0;
    }

    void Select(std::size_t index, std::size_t count) {
        selected = count ? std::min(index, count - 1) : 0;
        first = std::min(first, LastOffset(count));
        if (selected < first) first = selected;
        if (selected - first >= VisibleQueuedGames) first = selected - VisibleQueuedGames + 1;
    }

    void ScrollTo(std::size_t offset, std::size_t count) {
        first = std::min(offset, LastOffset(count));
        selected = count ? std::clamp(selected, first, std::min(first + VisibleQueuedGames - 1, count - 1)) : 0;
    }
};
}
