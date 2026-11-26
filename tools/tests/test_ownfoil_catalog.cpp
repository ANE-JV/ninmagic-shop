#include "utils/ownfoil_catalog.hpp"
#include "utils/ownfoil_browse.hpp"
#include "ui/shop_navigation.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>

static void TestPlatformSearch() {
    using namespace sphaira::ownfoil;
    // All these entries use the same installable extension. The folder, the
    // server metadata or a previously identified game establishes its console.
    constexpr std::string_view listing = R"({"files":[
      {"url":"/Nintendo Switch/Mario Kart Test [01000000000A0000].nsp"},
      {"url":"/Pack/Mario Advance Test [01000000000B0000].nsp","console":"GBA"},
      {"url":"/Pack/Mario Classic [05000000000C0000].nsp"},
      {"url":"/Pack/Mario Mystery [01000000000D0000].nsp"},
      {"url":"/Loose Mario/","name":"Loose Mario","directory":true},
      {"url":"/Nintendo Switch/Another Game [01000000000E0000].nsp"}
    ]})";
    std::string error;
    std::vector<index::File> files;
    assert(index::Parse(listing.data(), listing.size(), files, error));
    constexpr std::string_view unknown_game = R"({"games":[{
      "app_id":"01000000000D0000","app_type":"BASE","id":"01000000000D0000 not found in titledb",
      "name":"Mario Mystery","iconUrl":"","bannerUrl":"//placehold.it/400x200","category":""}]})";
    std::vector<index::Artwork> records;
    assert(index::ParseArtwork(unknown_game.data(), unknown_game.size(), records));
    index::ApplyArtwork(files, records);
    const index::BrowseIndex catalog{files, {{"05000000000C0000","Game Boy Advance"}}};
    api::CatalogQuery query;
    query.consolidated = true;
    query.category = api::Category::Search;
    query.search = "mario";
    const auto results = [&](const std::string& platform) {
        query.platform = platform;
        std::unordered_set<std::string> names;
        for (auto row : catalog.Select(files, query)) names.insert(files[row].name);
        return names;
    };
    using Names = std::unordered_set<std::string>;
    assert(api::MatchesPlatform("", "") && api::MatchesPlatform("", "Nintendo Switch"));
    assert(api::MatchesPlatform("__unknown__", "") && !api::MatchesPlatform("__unknown__", "Nintendo Switch"));
    assert(!api::MatchesPlatform("Nintendo Switch", "") && !api::MatchesPlatform("Nintendo Switch", "Game Boy Advance"));
    assert(results("Nintendo Switch") == Names({"Mario Kart Test"}));
    assert(results("Game Boy Advance") == Names({"Mario Advance Test", "Mario Classic"}));
    assert(results("__unknown__") == Names({"Mario Mystery", "Loose Mario"}));
    assert(results("") == Names({"Mario Kart Test", "Mario Advance Test", "Mario Classic", "Mario Mystery", "Loose Mario"}));
}

static void TestDirectoryNames() {
    using namespace sphaira::ownfoil::index;
    struct Row {
        std::string id, name, platform;
    };
    std::vector<Row> rows{
        {"05158000000a0000", "Unrecognized", "Game Boy Advance"},
        {"05158000000B0000", "", ""},
        {"05158000000C0000", "Maintained title", ""},
        {"05158000000D0000", "05158000000d0000", ""},
        {"05158000000E0000", "  ", ""},
        {"05158000000F0000", " 05158000000F0000 not found in TitleDB ", ""},
        {"0515800000100000", "Unknown", ""},
        {"pack 05158000000A0000", "", ""},
    };
    std::vector<File> files;
    const auto add = [&](std::string id, std::string name, Kind kind = Kind::Unknown) -> File& {
        auto& file = files.emplace_back();
        file.id = std::move(id);
        file.name = std::move(name);
        file.kind = kind;
        return file;
    };
    add("05158000000A0000", "Pokémon Retro");
    add("05158000000A0000", "Pokémon Retro", Kind::Base);
    add("05158000000A0000", "Update title", Kind::Update);
    add("05158000000A0000", "Folder title").directory = true;
    add("05158000000B0000", "One title");
    add("05158000000B0000", "Different title");
    add("05158000000B0000", "One title");
    add("05158000000C0000", "File title");
    add("05158000000D0000", "DLC title", Kind::Dlc);
    add("05158000000E0000", "Folder title").directory = true;
    add("05158000000F0000", "Another game");
    add("0515800000100000", "Unrecognized");
    add("0000000000000000", "No identity");

    ApplyDirectoryNames(rows, files);
    assert(rows[0].name == "Pokémon Retro" && rows[0].platform == "Game Boy Advance");
    assert(rows[1].name.empty() && rows[2].name == "Maintained title");
    assert(rows[3].name == "05158000000d0000" && rows[4].name == "  ");
    assert(rows[5].name == "Another game" && rows[6].name == "Unknown");
    assert(rows[7].name.empty());
}

static void TestNesLibrary() {
    using namespace sphaira::ownfoil;
    std::vector<index::File> files;
    auto add = [&](std::string id, std::string name, std::string platform = {}) -> index::File& {
        auto& file = files.emplace_back();
        file.name = std::move(name);
        file.id = std::move(id);
        file.url = "/games/" + file.id + ".nsp";
        file.platform = std::move(platform);
        file.extension = "nsp";
        return file;
    };
    add("0557200000010000", "1942");
    add("0557200003870000", "Linus Spacehead's Cosmic Crusade");
    for (unsigned i = 0; i < 300; ++i) {
        char id[17]{};
        std::snprintf(id, sizeof(id), "05572000%04X0000", 3 + i * 2);
        add(id, (i % 9 ? "Classic " : "Mario ") + std::to_string(i));
    }
    for (unsigned i = 0; i < 5; ++i) add("known-" + std::to_string(i), "Known " + std::to_string(i), "NES");
    add("0557200007210000", "Mario with an explicit console", "Game Boy Advance");
    add("0557200007220000", "Mario with an even number");
    add("0557100005710000", "Mario outside the collection");
    add("0557200007250000", "Mario DLC").kind = index::Kind::Dlc;
    add("0557200007270000", "Mario patch").kind = index::Kind::Update;
    add("0557200007290000", "Mario folder").directory = true;
    const auto original_size = files.size();
    const auto count_nes = [](const auto& list) {
        return std::count_if(list.begin(), list.end(), [](const auto& file) { return file.platform == "NES"; });
    };
    assert(count_nes(files) == 5);
    auto different_provider = files;
    index::ApplyNinMagicPlatforms(different_provider, "https://example.invalid/art");
    assert(count_nes(different_provider) == 5);
    auto unrelated_shop = files;
    unrelated_shop.erase(unrelated_shop.begin(), unrelated_shop.begin() + 2);
    index::ApplyNinMagicPlatforms(unrelated_shop, "https://ninmagic.com/juegos");
    assert(count_nes(unrelated_shop) == 5);

    index::ApplyNinMagicPlatforms(files, "https://ninmagic.com/juegos");
    assert(files.size() == original_size && count_nes(files) == 307);
    assert(files[307].platform == "Game Boy Advance");
    for (size_t i = 308; i < files.size(); ++i) assert(files[i].platform.empty());
    const index::BrowseIndex catalog{files};
    api::CatalogQuery query;
    query.consolidated = true;
    query.platform = "NES";
    query.page_size = 30;
    const auto games = catalog.Select(files, query);
    assert(games.size() == 307);
    const auto next = sphaira::ui::shop::AdjacentPage(0, 11, 30, 5, 28, true, false);
    assert(next && next->page == 1 && next->focus == 3);
    const auto next_row = games.at(static_cast<size_t>(next->page * query.page_size + next->focus));
    assert(files[next_row].platform == "NES" && next_row != games[28]);
    assert(!sphaira::ui::shop::AdjacentPage(10, 11, 30, 5, 6, true, false));
    query.category = api::Category::Search;
    query.search = "mario";
    const auto mario = catalog.Select(files, query);
    assert(mario.size() == 34);
    for (auto row : mario) assert(catalog.PlatformOf(files[row]) == "NES" && files[row].name.starts_with("Mario "));
}

int main(int argc, char** argv) {
    TestPlatformSearch();
    TestDirectoryNames();
    TestNesLibrary();
    using namespace sphaira::ownfoil::index;
    const auto html = "<table><a href='Exact/'> </a><a href='Duplicate/'> </a><a href='Almost/'> </a><a href='Custom/'> </a><a href='Loose%20[0500123456780800].nsp'> </a></table>";
    std::vector<File> files;
    std::vector<Artwork> records;
    std::string error;
    assert(ParseHtml(html, std::strlen(html), files, error));
    const auto metadata = R"({"games":[
        {"name":"Exact","app_type":"BASE","app_id":"0100123456780000","iconUrl":"https://art.invalid/one.png","category":["Acción","Aventura"],"description":"Atraviesa una isla."},
        {"name":"Duplicate","app_type":"BASE","app_id":"0100123456790000"},
        {"name":"Duplicate","app_type":"BASE","app_id":"0100123456800000"},
        {"name":"Almost!","app_type":"BASE","app_id":"0100123456810000"},
        {"name":"Custom","app_type":"BASE","app_id":"0500123456780800","platform":"GBA","iconUrl":"https://art.invalid/custom.png"}
    ]})";
    assert(ParseArtwork(metadata, std::strlen(metadata), records));
    ApplyArtwork(files, records);
    const auto status = AddGameFolderCards(files, records);
    assert(status.folders == 4 && status.mapped_folders == 2 && status.ambiguous_folders == 1 && status.unresolved_folders == 2);
    const auto& retail = files[files.size()-2];
    const auto& custom = files.back();
    assert(!retail.directory && retail.source_folder == "Exact/" && retail.id == "0100123456780000");
    assert(retail.extension.empty() && retail.platform == "Nintendo Switch" && retail.icon == "https://art.invalid/one.png");
    assert(retail.genre == "Acción, Aventura" && retail.description == "Atraviesa una isla.");
    assert(custom.id == "0500123456780800" && custom.title_id == custom.id && custom.platform == "Game Boy Advance");
    assert(!BelongsToGame(custom, custom.id)); // a folder URL is never installed
    assert(files.front().kind == Kind::Unknown && files.front().title_id.empty());
    using namespace sphaira::ownfoil;
    api::CatalogQuery browse{};
    browse.consolidated = true;
    const BrowseIndex catalog{files};
    assert(catalog.folder_count == 4 && catalog.mapped_folders == 2);
    auto results = catalog.Select(files, browse);
    assert(results.size() == 3);
    auto second_page = browse;
    second_page.page = 2;
    second_page.page_size = 10;
    assert(browse.SameSelection(second_page));
    using sphaira::ui::shop::AdjacentPage;
    assert(!AdjacentPage(0, 3, 30, 5, 2, false, true));
    const auto down = AdjacentPage(0, 3, 30, 5, 28, true, false);
    assert(down && down->page == 1 && down->focus == 3);
    const auto up = AdjacentPage(1, 3, 30, 5, 3, false, true);
    assert(up && up->page == 0 && up->focus == 28);
    assert(!AdjacentPage(2, 3, 30, 5, 1, true, false));
    browse.category = api::Category::Search;
    assert(!browse.SameSelection(second_page));
    browse.search = "eXAct";
    results = catalog.Select(files, browse);
    assert(results.size() == 1 && !files[results.front()].directory);
    browse.search = "almost";
    results = catalog.Select(files, browse);
    assert(results.size() == 1 && files[results.front()].directory);
    assert(SearchText("Pokémon Edición") == "pokemon edicion");
    assert(SearchText("PINGÜINO AÑO Ç À") == "pinguino ano c a");
    assert(SearchText("逆転") == "逆転");
    std::stop_source cancelled;
    cancelled.request_stop();
    assert(catalog.Select(files, browse, cancelled.get_token()).empty());
    assert(BelongsToGame(files.front(), "0500123456780800"));
    assert(!BelongsToGame(files.front(), "0500123456780000"));
    const auto special = "<table><a href='#Game?/'> </a><a href='File#Part?.nsp'> </a></table>";
    std::vector<File> special_files;
    assert(ParseHtml(special, std::strlen(special), special_files, error));
    assert(special_files.size() == 2 && special_files[0].url == "File%23Part%3F.nsp");
    assert(special_files[0].name == "File#Part?" && special_files[1].url == "%23Game%3F/" && special_files[1].name == "#Game?");
    const auto contents = R"({"files":[
      "/Game [0100123456780000][v0].nsp",
      "/Update [0100123456780800][v65536].nsp",
      "/DLC [0100123456781001][v0].nsp",
      "/Different [0100123456790000][v0].nsp",
      "/Custom [0500123456780800][v65536].nsp",
      "/Other [0500123456780000][v0].nsp"
    ]})";
    std::vector<File> packages;
    assert(Parse(contents, std::strlen(contents), packages, error));
    assert(std::count_if(packages.begin(), packages.end(), [](const auto& file) { return BelongsToGame(file, "0100123456780000"); }) == 3);
    assert(std::count_if(packages.begin(), packages.end(), [](const auto& file) { return BelongsToGame(file, "0500123456780800"); }) == 1);
    assert(packages[4].kind == Kind::Unknown && packages[4].title_id.empty());
    GameFolderQueue many{"https://shop.invalid/Game/"};
    for (unsigned i{}; i < 17; ++i) many.Add("https://shop.invalid/Game/child" + std::to_string(i) + '/', 1);
    GameFolderQueue::Entry next;
    std::size_t visited{};
    while (many.Pop(next)) ++visited;
    assert(visited == GameFolderQueue::MaxDirectories && many.Incomplete());
    GameFolderQueue deep{"https://shop.invalid/Game/"};
    while (deep.Pop(next)) deep.Add(next.url + "nested/", next.depth + 1);
    assert(next.depth == GameFolderQueue::MaxDepth && deep.Incomplete());
    GameFolderQueue repeated{"https://shop.invalid/Game/"};
    for (unsigned i{}; i < 30; ++i) repeated.Add("https://shop.invalid/Game/", 1);
    assert(repeated.Pop(next) && !repeated.Pop(next) && !repeated.Incomplete());
    std::cout << "Ownfoil consolidated catalog identity fixtures passed\n";
    if (argc >= 3) {
        const auto read = [](const char* path) { std::ifstream in(path, std::ios::binary); assert(in.good()); return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}; };
        const auto listing = read(argv[1]), art = read(argv[2]);
        assert(ParseHtml(listing.data(), listing.size(), files, error));
        assert(ParseArtwork(art.data(), art.size(), records));
        ApplyArtwork(files, records);
        const auto real = AddGameFolderCards(files, records);
        ApplyNinMagicPlatforms(files, "https://ninmagic.com/juegos");
        const auto start = std::chrono::steady_clock::now();
        const BrowseIndex live_index{files};
        api::CatalogQuery live_query{};
        live_query.consolidated = true;
        const auto ordered = live_index.Select(files, live_query);
        const auto indexed = std::chrono::steady_clock::now();
        live_query.category = api::Category::Search;
        live_query.search = "mario";
        const auto hits = live_index.Select(files, live_query);
        const auto searched = std::chrono::steady_clock::now();
        for (auto row : hits) assert(SearchText(files[row].name).find(live_query.search) != std::string::npos);
        std::size_t mario_switch{}, mario_retro{}, mario_nes{}, mario_unknown{};
        for (const auto& scope : live_index.platforms) {
            live_query.platform = scope;
            const auto scoped = live_index.Select(files, live_query);
            for (auto row : scoped) {
                const auto actual = live_index.PlatformOf(files[row]);
                assert(scope == "__unknown__" ? actual.empty() : actual == scope);
                assert(std::find(hits.begin(), hits.end(), row) != hits.end());
            }
            if (scope == "Nintendo Switch") mario_switch += scoped.size();
            else if (scope == "__unknown__") mario_unknown += scoped.size();
            else mario_retro += scoped.size();
            if (scope == "NES") mario_nes = scoped.size();
        }
        std::cout << "Private search scope (mario): global=" << hits.size() << " Switch=" << mario_switch
            << " other_classified=" << mario_retro << " NES=" << mario_nes << " unclassified=" << mario_unknown << '\n';
        live_query.category = api::Category::All;
        live_query.platform = "NES";
        const auto nes_games = live_index.Select(files, live_query);
        std::cout << "Private NES shelf: games=" << nes_games.size() << " pages_of_30=" << (nes_games.size() + 29) / 30 << '\n';
        if (nes_games.size() > 30) {
            const auto pages = static_cast<std::int64_t>(nes_games.size() + 29) / 30;
            const auto target = sphaira::ui::shop::AdjacentPage(0, pages, 30, 5, 25, true, false);
            assert(target && live_index.PlatformOf(files[nes_games.at(target->page * 30 + target->focus)]) == "NES");
        }
        std::cout << "Host catalog: ordered=" << ordered.size() << " index/sort_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(indexed-start).count()
            << " search_matches=" << hits.size() << " search_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(searched-indexed).count() << '\n';
        std::size_t games{}, switch_games{}, covered{};
        std::unordered_set<std::string> unique;
        for (const auto& file : files) if (!file.directory && unique.insert(Key(file)).second) {
            ++games;
            switch_games += file.platform == "Nintendo Switch";
            covered += !file.icon.empty() || !file.banner.empty();
        }
        std::cout << "Private consolidated fixture: games=" << games << " switch_games=" << switch_games << " real_art=" << covered
            << " mapped_folders=" << real.mapped_folders << " unresolved_folders=" << real.unresolved_folders << " ambiguous_folders=" << real.ambiguous_folders << '\n';
        if (argc >= 4) {
            const auto manifest = read((std::string(argv[3]) + "/expected.json").c_str());
            auto document = yyjson_read(manifest.data(), manifest.size(), YYJSON_READ_NOFLAG);
            assert(document && yyjson_is_arr(yyjson_doc_get_root(document)));
            std::size_t checked{}, with_base{}, with_update{};
            std::size_t i, count; yyjson_val* row;
            yyjson_arr_foreach(yyjson_doc_get_root(document), i, count, row) {
                const auto capture = read((std::string(argv[3]) + '/' + String(row, "fixture")).c_str());
                assert(ParseHtml(capture.data(), capture.size(), packages, error));
                const auto expected = Id(String(row, "id"));
                assert(!expected.empty());
                assert(std::any_of(packages.begin(), packages.end(), [&expected](const auto& file) { return BelongsToGame(file, expected); }));
                assert(std::all_of(packages.begin(), packages.end(), [](const auto& file) { return file.directory || file.source_folder.empty(); }));
                const auto has_content = [&](Kind kind) {
                    return std::any_of(packages.begin(), packages.end(), [&](const auto& file) {
                        return file.kind == kind && BelongsToGame(file, expected) && !file.url.empty() && !file.extension.empty();
                    });
                };
                with_base += has_content(Kind::Base);
                with_update += has_content(Kind::Update);
                ++checked;
            }
            yyjson_doc_free(document);
            std::cout << "Private selected-folder identity fixtures passed: folders=" << checked
                << " with_base_package=" << with_base << " with_update=" << with_update << '\n';
        }
    }
}
