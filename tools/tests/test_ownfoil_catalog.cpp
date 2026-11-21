#include "utils/ownfoil_catalog.hpp"
#include "utils/ownfoil_browse.hpp"
#include "ui/shop_navigation.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
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
            std::size_t checked{};
            std::size_t i, count; yyjson_val* row;
            yyjson_arr_foreach(yyjson_doc_get_root(document), i, count, row) {
                const auto capture = read((std::string(argv[3]) + '/' + String(row, "fixture")).c_str());
                assert(ParseHtml(capture.data(), capture.size(), packages, error));
                const auto expected = Id(String(row, "id"));
                assert(!expected.empty());
                assert(std::any_of(packages.begin(), packages.end(), [&expected](const auto& file) { return BelongsToGame(file, expected); }));
                assert(std::all_of(packages.begin(), packages.end(), [](const auto& file) { return file.directory || file.source_folder.empty(); }));
                ++checked;
            }
            yyjson_doc_free(document);
            std::cout << "Private selected-folder identity fixtures passed: folders=" << checked << '\n';
        }
    }
}
