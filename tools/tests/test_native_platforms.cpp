// SPDX-License-Identifier: GPL-3.0-or-later
#include "utils/ownfoil_browse.hpp"
#include "utils/ownfoil_catalog.hpp"
#include "utils/ownfoil_query.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stop_token>
#include <unordered_map>

namespace sphaira::ownfoil::api {
using s64 = std::int64_t;

struct Config { std::string user, pass, artwork_search_url; };
struct ShopApp {
    std::string app_id, platform, name;
    bool platform_learned{};
};

std::vector<ShopApp> catalog;
std::vector<Category> requests;
std::vector<index::File> directory_metadata;
size_t directory_requests{};
bool incomplete_response{};
bool stale_card_platform{};

bool FetchNativePage(const std::string& server, const Config&, std::stop_token, const CatalogQuery& query,
    std::vector<ShopApp>& rows, s64& total, std::string&, bool identities_only = false) {
    requests.push_back(query.category);
    if (query.category == Category::Favorites) {
        // A server is free to reorder an IN query. The client must restore the display order.
        for (auto id = query.app_ids.rbegin(); id != query.app_ids.rend(); ++id) {
            for (const auto& game : catalog) {
                if (game.app_id != *id) continue;
                auto row = game;
                if (stale_card_platform) row.platform = "Nintendo Switch";
                rows.push_back(std::move(row));
            }
        }
        total = static_cast<s64>(rows.size());
        return true;
    }
    // Ownfoil's server-side search uses SQLite LIKE, which does not fold accents.
    // The Switch should request the complete identity list without a search term.
    assert(query.category == Category::All && query.search.empty() && identities_only);
    total = static_cast<s64>(catalog.size());
    if (incomplete_response) return true;
    const auto first = (query.page - 1) * query.page_size;
    for (auto i = first; i < std::min(first + query.page_size, total); ++i) {
        auto row = catalog[i];
        if (const auto platform = sphaira::ownfoil::index::NeroMagicPlatform(server, row.app_id, row.name); !platform.empty()) {
            row.platform = platform;
        }
        rows.push_back(std::move(row));
    }
    return true;
}

auto FetchNativeDirectoryMetadata(const std::string&, const Config&, std::stop_token) -> std::vector<index::File> {
    ++directory_requests;
    return directory_metadata;
}

#include "production_native_groups.inc"

} // namespace sphaira::ownfoil::api

int main() {
    using namespace sphaira::ownfoil::api;
    for (int i = 0; i < 450; ++i)
        catalog.push_back({std::to_string(i), i % 17 == 0 ? "NES" : "Nintendo Switch", "Mario"});
    catalog.push_back({"0557200006110000", "", "Mario"});
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
    assert(fetch() && total == 28 && rows.front().app_id == "0" && requests.size() == 1);
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

    namespace index = sphaira::ownfoil::index;
    constexpr auto nero = "https://neromagic.switchip.es/";
    for (const auto& [id, platform] : {
        std::pair{"0510300000010000", "PSP"}, {"0510000000050000", "PSP"}, {"0510700000010000", "PlayStation"},
        {"0510700002D20739", "PlayStation"},
        {"0510800006870000", "Nintendo 3DS / 2DS"}, {"0527500000010000", "Nintendo DS"},
        {"0557200000010000", "NES"}, {"0585200002740000", "Super Nintendo"}, {"0512200000010000", "Nintendo 64"},
        {"0515800000010000", "Game Boy Advance"}, {"0557400000010000", "Game Boy"}, {"0526900000010000", "Game Boy Color"},
        {"0562500000010000", "Mega Drive"}, {"0559800000010000", "Neo Geo"}, {"0574800000030000", "Sega 32X"},
        {"0591900000030000", "Atari 7800"}, {"0592000000050000", "PC Engine"}, {"0592100000300000", "Atari Jaguar"}}) {
        assert(index::NeroMagicPlatform(nero, id, "Title") == platform);
        assert(index::NeroMagicPlatform("https://another-shop.example/", id, "Title").empty());
    }
    assert(index::NeroMagicPlatform(nero, "0100393010E58000", "ACA NEOGEO METAL SLUG") == "Neo Geo");
    assert(index::NeroMagicPlatform(nero, "0100C34019218000", "Arcade Archives BARADUKE") == "Arcade");
    assert(index::NeroMagicPlatform(nero, "0100C34019218000", "Johnny Turbo’s Arcade: Caveman Ninja") == "Arcade");
    assert(index::NeroMagicPlatform(nero, "0100C34019218000", "Arcade Paradise").empty());
    assert(index::NeroMagicPlatform(nero, "05598000ZZZZ0000", "Unknown").empty());
    assert(index::Platform("N2DS") == "Nintendo 3DS / 2DS");

    // Keep the full shelf across identity pages and maintain its classification
    // when a detailed card returns a conflicting name or platform.
    catalog.clear();
    incomplete_response = false;
    stale_card_platform = true;
    for (int i = 1; i <= 1003; ++i) {
        char id[17]{};
        std::snprintf(id, sizeof(id), "05158000%04X0000", static_cast<unsigned>(i));
        catalog.push_back({id, "Nintendo Switch", "Sample"});
    }
    catalog.push_back({"0559800000010000", "", "A Neo Geo title"});
    catalog.push_back({"0100000000001000", "Nintendo Switch", "Pok\u00e9mon Norte"});
    catalog.push_back({"0100000000002000", "Nintendo Switch", "Pokemon Sur"});
    catalog.push_back({"0100000000003000", "Nintendo Switch", "Pok\u00e9mon Este"});
    catalog.push_back({"0527500000010000", "", "Pok\u00e9mon Retro"});
    config.user = "nero";
    const auto fetch_nero = [&] {
        rows.clear();
        requests.clear();
        return FetchScopedNativePage(nero, config, {}, query, rows, total, error);
    };
    // Open search with a cold cache. All matches lie beyond the first identity page.
    query.category = Category::Search;
    query.search = "pokemon";
    query.page = 1;
    query.page_size = 2;
    query.platform = "Nintendo Switch";
    assert((fetch_nero() && requests == std::vector{Category::All, Category::All, Category::Favorites}));
    assert(total == 3 && rows.size() == 2 && rows.front().app_id == "0100000000001000");
    assert(rows.back().app_id == "0100000000002000" && rows.back().platform == "Nintendo Switch");

    query.search = "POK\u00c9MON";
    assert(fetch_nero() && total == 3 && rows.size() == 2 && rows.back().app_id == "0100000000002000");
    assert(requests == std::vector{Category::Favorites});
    query.page = 2;
    assert(fetch_nero() && total == 3 && rows.size() == 1 && rows.front().app_id == "0100000000003000");
    assert(requests == std::vector{Category::Favorites});
    query.platform.clear();
    assert(fetch_nero() && total == 4 && rows.size() == 2 && rows.back().platform == "Nintendo DS");

    query.page = 1;
    query.platform = "Nintendo DS";
    assert(fetch_nero() && total == 1 && rows.size() == 1 && rows.front().app_id == "0527500000010000");
    assert(rows.front().platform == "Nintendo DS" && requests == std::vector{Category::Favorites});
    query.search = "0100000000002000";
    query.platform.clear();
    assert(fetch_nero() && total == 1 && rows.size() == 1 && rows.front().app_id == "0100000000002000");

    query.category = Category::All;
    query.search.clear();
    query.page_size = 10;
    query.page = 101;
    query.platform = "Game Boy Advance";
    assert(fetch_nero() && total == 1003 && rows.size() == 3 && requests.size() == 1);
    assert(rows.front().app_id == "0515800003E90000" && rows.front().platform == "Game Boy Advance");
    query.page = 1;
    query.platform = "Neo Geo";
    assert(fetch_nero() && total == 1 && rows.size() == 1 && rows.front().platform == "Neo Geo" && requests.size() == 1);
    query.category = Category::Search;
    query.search = "Sample";
    query.platform = "Game Boy Advance";
    assert(fetch_nero() && total == 1003 && rows.size() == 10 && rows.front().platform == "Game Boy Advance");

    catalog = {{"0100C34019218000", "Nintendo Switch", "Arcade Archives BARADUKE", true},
        {"0585200000010000", "", "3x3 Eyes - Juuma Houkan"},
        {"0585200000030000", "", "7th Saga Redux, The"},
        {"0585200000050000", "", "A game in the same Super Nintendo collection"},
        {"0559800000010000", "", "2020 Super Baseball"},
        {"0559800000030000", "", "3 Count Bout"},
        {"0515800000010000", "", "007 - Everything or Nothing"},
        {"0515800000030000", "", "Unrelated game with a reused forwarder ID"}};
    config.user = "mirror";
    query.category = Category::All;
    query.search.clear();
    query.platform = "Nintendo Switch";
    assert(fetch() && rows.empty() && requests.size() == 1);
    query.platform = "Neo Geo";
    assert(fetch() && total == 2 && rows.size() == 2 && requests.size() == 1);
    query.platform = "Game Boy Advance";
    assert(fetch() && total == 0 && rows.empty() && requests.empty());
    query.platform = "__unknown__";
    assert(fetch() && total == 2 && rows.size() == 2 && requests.size() == 1);
    query.category = Category::Search;
    query.search = "game in the same";
    query.platform = "Super Nintendo";
    assert(fetch() && total == 1 && rows.size() == 1 && rows.front().app_id == catalog[3].app_id);
    assert(requests == std::vector{Category::Favorites});
    query.category = Category::All;
    query.search.clear();
    assert(fetch() && total == 3 && rows.size() == 3 && requests.size() == 1);
    query.platform = "Arcade";
    assert(fetch() && total == 1 && rows.size() == 1 && rows.front().platform == "Arcade");

    // A mirror may expose a title's real name in the client listing while
    // GraphQL returns a placeholder. Match the same owned game by exact ID.
    catalog = {{"0585200000010000", "", "Unrecognized"},
        {"0585200000030000", "", "Unrecognized"},
        {"0585200000050000", "", "Unrecognized"},
        {"0100000000001000", "Nintendo Switch", "A named Switch game"}};
    const auto metadata_file = [](std::string id, std::string name) {
        index::File file;
        file.id = std::move(id);
        file.name = std::move(name);
        file.kind = index::Kind::Base;
        return file;
    };
    directory_metadata = {metadata_file(catalog[0].app_id, "3x3 Eyes - Juuma Houkan"),
        metadata_file(catalog[1].app_id, "7th Saga Redux, The"),
        metadata_file(catalog[2].app_id, "Pok\u00e9mon Retro"),
        metadata_file(catalog[3].app_id, "Different name for the Switch game"),
        metadata_file("0585200000070000", "Pok\u00e9mon outside the authorized list")};
    config.user = "mirror-with-placeholder-names";
    directory_requests = 0;
    query.category = Category::Search;
    query.platform = "Super Nintendo";
    query.search = "pokemon";
    assert(fetch() && total == 1 && rows.size() == 1 && rows.front().app_id == catalog[2].app_id);
    assert(rows.front().platform == "Super Nintendo" && rows.front().name == "Pok\u00e9mon Retro" && directory_requests == 1);
    query.platform.clear();
    assert(fetch() && total == 1 && rows.size() == 1 && directory_requests == 1);
    query.search = "Unrecognized";
    assert(fetch() && total == 0 && rows.empty() && requests.empty());
    query.search = "different name";
    assert(fetch() && total == 0 && rows.empty() && requests.empty());
}
