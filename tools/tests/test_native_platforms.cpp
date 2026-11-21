// SPDX-License-Identifier: GPL-3.0-or-later
#include "utils/ownfoil_catalog.hpp"
#include "utils/ownfoil_query.hpp"
#include <algorithm>
#include <cassert>
#include <memory>
#include <mutex>
#include <stop_token>
#include <unordered_map>

using namespace sphaira::ownfoil::api;
namespace index = sphaira::ownfoil::index;
using s64 = std::int64_t;

struct Config { std::string user, pass, artwork_search_url; };
struct ShopApp {
    std::string app_id, platform, name;
    bool platform_learned{};
};

std::vector<ShopApp> catalog, search_hits;
std::vector<Category> requests;
bool incomplete_response{};

bool FetchNativePage(const std::string&, const Config&, std::stop_token, const CatalogQuery& query,
    std::vector<ShopApp>& rows, s64& total, std::string&, bool = false) {
    requests.push_back(query.category);
    if (query.category == Category::Favorites) {
        // A server is free to reorder an IN query. The client must restore the display order.
        for (auto id = query.app_ids.rbegin(); id != query.app_ids.rend(); ++id)
            for (const auto& game : catalog)
                if (game.app_id == *id) rows.push_back(game);
        total = static_cast<s64>(rows.size());
        return true;
    }
    const auto& source = query.category == Category::All ? catalog : search_hits;
    total = static_cast<s64>(source.size());
    if (incomplete_response) return true;
    const auto first = (query.page - 1) * query.page_size;
    for (auto i = first; i < std::min(first + query.page_size, total); ++i) rows.push_back(source[i]);
    return true;
}

#include "production_native_groups.inc"

int main() {
    for (int i = 0; i < 450; ++i)
        catalog.push_back({std::to_string(i), i % 17 == 0 ? "NES" : "Nintendo Switch", "Mario"});
    catalog.push_back({"0557200006110000", "", "Mario"});
    search_hits = catalog;
    // The anchors do not match the search. They occur only in the full library.
    catalog.push_back({"0557200000010000", "", "1942"});
    catalog.push_back({"0557200003870000", "", "Linus Spacehead's Cosmic Crusade"});

    CatalogQuery query;
    query.category = Category::All;
    query.platform = "NES";
    query.page_size = 10;
    Config config{"user", "", "https://ninmagic.com/juegos"};
    std::vector<ShopApp> rows;
    std::string error;
    s64 total{};
    const auto fetch = [&] {
        rows.clear();
        requests.clear();
        return FetchScopedNativePage("test-shop", config, {}, query, rows, total, error);
    };
    assert(fetch() && total == 30 && rows.size() == 10 && requests.size() == 2);
    query.category = Category::Search;
    query.search = "mario";
    assert(fetch() && total == 28 && rows.front().app_id == "0" && requests.size() == 4);
    query.page = 2;
    assert(fetch() && rows.size() == 10 && rows.front().app_id == "170" && requests.size() == 1);
    query.page = 3;
    assert(fetch() && rows.size() == 8 && rows.back().app_id == "0557200006110000" && rows.back().platform == "NES");
    query.page = 4;
    assert(fetch() && rows.empty() && requests.empty());
    query.page = 1;
    query.platform = "Nintendo Switch";
    assert(fetch() && total == 423 && rows.front().app_id == "1" && requests.size() == 1);
    query.platform = "__unknown__";
    assert(fetch() && total == 0 && rows.empty());

    // A different profile must never receive a cached list from the first account.
    config.user = "another";
    incomplete_response = true;
    assert(!fetch() && !error.empty());
    std::stop_source stopped;
    stopped.request_stop();
    requests.clear();
    assert(!FetchScopedNativePage("test-shop", config, stopped.get_token(), query, rows, total, error));
    assert(requests.empty());
}
