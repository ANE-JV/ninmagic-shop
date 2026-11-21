// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ownfoil::api {
enum class Category : std::int64_t { All, NewGames, Updates, Dlc, Search, Favorites };

inline bool MatchesPlatform(std::string_view selected, std::string_view actual) {
    return selected.empty() || (selected == "__unknown__" ? actual.empty() : selected == actual);
}

struct CatalogQuery {
    std::int64_t page{1}, page_size{};
    Category category{Category::All};
    std::vector<std::string> app_ids, title_ids;
    std::string search, platform;
    bool consolidated{};
    const char* order_field{"NAME"}; // fixed options shared with the native API
    bool descending{};

    bool SameSelection(const CatalogQuery& other) const {
        return category == other.category && consolidated == other.consolidated && descending == other.descending
            && std::string_view{order_field} == other.order_field && search == other.search && platform == other.platform
            && app_ids == other.app_ids && title_ids == other.title_ids;
    }
};
}
