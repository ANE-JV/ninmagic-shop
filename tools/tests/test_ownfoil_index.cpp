#include "utils/ownfoil_index.hpp"
#include "utils/ownfoil_direct_index.hpp"
#include "utils/ownfoil_install_plan.hpp"
#include <cassert>
#include <iostream>
#include <cstring>
#include <fstream>
#include <iterator>

static void CheckDirectIndex() {
    namespace direct = sphaira::ownfoil::direct_index;
    namespace index = sphaira::ownfoil::index;
    const std::string server = "https://shop.invalid/tenant";
    const std::string original = R"({"schema":1,"server":"https://SHOP.invalid:443/tenant/","files":[
      {"url":"/api/get_game/1#Game%23one%3F%25%20copy.NSP","app_id":"0100123456780000","title_id":"0100123456780000","kind":"base","version":0,"size":15032385536},
      {"url":"/api/get_game/2#GameUpdate.nsz","app_id":"0500123456780807","title_id":"0500123456780000","kind":"update","version":65536,"size":11},
      {"url":"/api/get_game/3#extra.xci","app_id":"0100123456781001","title_id":"0100123456780000","kind":"dlc","version":1,"size":12}]})";
    std::vector<index::File> files;
    std::string error;
    assert(direct::Parse(original, server, files, error) && files.size() == 3);
    assert(files[0].url == "https://shop.invalid/api/get_game/1" && files[0].name == "Game#one?% copy");
    assert(files[0].size == 15032385536LL && files[0].extension == "nsp" && files[0].kind == index::Kind::Base);
    assert(files[1].kind == index::Kind::Update && files[1].title_id == "0500123456780000" && files[1].version_known);
    assert(files[2].kind == index::Kind::Dlc && files[2].extension == "xci");
    const auto rejects = [&](std::string_view old, std::string_view replacement) {
        auto value = original;
        value.replace(value.find(old), old.size(), replacement);
        assert(!direct::Parse(value, server, files, error) && files.empty() && !error.empty());
    };
    for (auto bad : {"https://other.invalid/api/get_game/1", "//other.invalid/api/get_game/1",
            "/api/get_game/0", "/api/get_game/-1", "/api/get_game/18446744073709551616",
            "/api/get_game/1?q=3", "/api/get_game/1/../3", "/api/get_game/%31", "/api/files/1",
            "/api/get_game/1\\u0000admin"}) rejects("/api/get_game/1", bad);
    for (auto bad : {"missing.txt", "folder%2Fgame.nsp", "folder%5cgame.nsp", "x%00.nsp",
                     "x%ZZ.nsp", "foo.nsp?query", "foo#game.nsp"}) rejects("Game%23one%3F%25%20copy.NSP", bad);
    rejects(":443/tenant", ":444/tenant");
    rejects("/tenant/", "/Tenant/");
    rejects("https://SHOP", "http://SHOP");
    rejects("https://SHOP", "https://user@SHOP");
    rejects("\"schema\":1", "\"schema\":true");
    rejects("0500123456780807", "05001234567808078");
    rejects("0500123456780807", "0000000000000000");
    rejects("\"kind\":\"update\"", "\"kind\":\"base\"");
    rejects("/api/get_game/2", "/api/get_game/1");
    rejects("\"size\":11", "\"size\":-1");
    rejects("\"version\":65536", "\"version\":4294967296");
    assert(direct::Parse(R"({"schema":1,"server":"https://shop.invalid/tenant","files":[]})", server, files, error));
    assert(files.empty() && error.empty());
}

int main(int argc, char** argv) {
    CheckDirectIndex();
    using namespace sphaira::ownfoil::index;
    std::vector<File> files;
    std::string error;
    const char* json = R"({"files":[
      {"url":"/files/base.nsp#Example%20Game%20[0100123456780000][v0].nsp","size":15032385536},
      {"url":"/files/update.nsz#Example Game [0100123456780800][v262144].nsz","size":"21474836480"},
      {"url":"/files/dlc.nsp#Example DLC [0100123456781001][v65536].nsp","size":1024},
      {"url":"https://cdn.invalid/file.nsp#A file without ID.nsp","size":-1},
      {"url":"/ignored.txt","size":10},
      null,42,{"name":"missing URL"}
    ],"titledb":{"0100123456780000":{"name":"Catalogue display name","publisher":"Example publisher","iconUrl":"/art/icon.png"}}})";
    assert(Parse(json, std::strlen(json), files, error));
    assert(files.size() == 4);
    assert(files[0].name == "Catalogue display name");
    assert(files[0].publisher == "Example publisher");
    assert(files[0].url == "/files/base.nsp");
    assert(files[0].size == 15032385536LL && files[1].size == 21474836480LL);
    assert(files[0].kind == Kind::Base && files[0].title_id == "0100123456780000");
    assert(files[1].kind == Kind::Update && files[1].title_id == files[0].id);
    assert(files[1].version == 262144 && files[1].extension == "nsz");
    assert(files[2].kind == Kind::Dlc && files[2].title_id == files[0].id && files[2].version == 65536);
    assert(files[3].kind == Kind::Unknown && files[3].name == "A file without ID" && files[3].size == 0);
    assert(Id("[0100abcdef120000]") == "0100ABCDEF120000");
    assert(Id("A00123456789000000").empty());
    assert(Id("abcdefgh12345678").empty());
    assert(Id("abcdef1234567890") == "ABCDEF1234567890");
    assert(CleanName("  A [DLC] [0100123456781001][v2].nsp ") == "A [DLC] .nsp");
    assert(Decode("one%20two%00three%0D%0A") == "one twothree");
    assert(Extension("GAME.NSZ?token=example") == "nsz");
    for (const auto invalid : {"{", "[]", "{\"files\":{}}", "encrypted data"}) assert(!Parse(invalid, std::strlen(invalid), files, error));
    assert(Parse("{\"files\":[]}", 12, files, error) && files.empty());
    const auto raw = R"({"files":["/Example%20[0100123456780000][v0].xci"]})";
    assert(Parse(raw, std::strlen(raw), files, error));
    assert(files.size() == 1 && files[0].name == "Example" && files[0].extension == "xci");
    const auto html = R"(<html><table><a href="Example%20[0100123456780000][v0].nsp"> </a><a href="Folder &amp; Extra/"> </a><a href="../"> </a></table></html>)";
    assert(IsOwnfoilDirectory("<!DOCTYPE html>\n<html>\n<body>\n<table>\n<a href='A/'> </a>\n</table></body></html>"));
    assert(IsOwnfoilDirectory("  <table></table>"));
    assert(!IsOwnfoilDirectory("<html><head><title>Ownfoil</title></head><body><a href='/library/'>Library</a></body></html>"));
    assert(!IsOwnfoilDirectory("<html><body><form><input type='password'><a href='/'>Home</a></form></body></html>"));
    assert(ParseHtml(html, std::strlen(html), files, error));
    assert(files.size() == 2 && files[0].name == "Example" && files[1].directory && files[1].name == "Folder & Extra");
    assert(!ParseHtml("<a href='00 - ERROR'> </a>", 25, files, error));
    assert(!ParseHtml("<html>Login</html>", 18, files, error));
    assert(ParseHtml("<table></table>", 15, files, error) && files.empty());
    // A container extension or a forwarder's Switch ID does not classify its game.
    assert(PlatformFromPath("https://shop.invalid/catalog/NES/Mario.nsp") == "NES");
    assert(PlatformFromPath("https://shop.invalid/catalog/PS1/Final%20Game.nsz") == "PlayStation");
    assert(PlatformFromPath("https://shop.invalid/Super%20Nintendo/Game.nsp") == "Super Nintendo");
    assert(PlatformFromPath("https://shop.invalid/Game%20NES%20Collection.nsp").empty());
    assert(PlatformFromPath("https://switch.invalid/catalog/Game.nsp").empty());
    assert(PlatformFromPath("https://shop.invalid/nes-classics/Game.nsp").empty());
    assert(Platform("01 - Nintendo Switch") == "Nintendo Switch");
    assert(PlatformFromTags("Example [GBA][0500123456780800].nsp") == "Game Boy Advance");
    assert(PlatformFromTags("Example [SNES].nsp") == "Super Nintendo");
    assert(PlatformFromTags("Example SNES Adventure.nsp").empty());
    const auto manifest = R"({"files":[
      {"url":"/unknown.nsp","name":"Generic package"},
      {"url":"/files/snes.nsp","platform":"SNES","cover":"/art/snes.png"},
      {"url":"/files/forwarder.nsp#Game [0100123456780000].nsp","console":"ps1"},
      {"url":"/retro/","directory":true,"name":"Retro collection"},
      {"url":"/nes/gba.nsz","system":"GBA"}
    ]})";
    assert(Parse(manifest, std::strlen(manifest), files, error));
    assert(files.size() == 5);
    assert(files[0].platform.empty());
    assert(files[1].platform == "Super Nintendo" && files[1].icon == "/art/snes.png");
    assert(files[2].platform == "PlayStation" && files[2].kind == Kind::Base);
    assert(files[3].directory && files[3].extension.empty());
    assert(files[4].platform == "Game Boy Advance"); // explicit manifest beats path
    const auto inherited = R"({"platform":"PSP","files":[{"url":"/Game.nsp"}]})";
    assert(Parse(inherited, std::strlen(inherited), files, error));
    assert(files.size() == 1 && files[0].platform == "PSP");
    assert(InstallTarget(0x0500123456780000ULL, "0500123456780000", true) == 0x0500123456780000ULL);
    assert(InstallTarget(0x0500123456780000ULL, "0500123456780807", false) == 0x0500123456780807ULL);
    assert(InstallTarget(0x0500123456780000ULL, "", false) == 0); // no guessed custom XOR
    assert(InstallTarget(0, "", true) == 0); // generic standalone: whole selected file
    assert(InstallTarget(0x0100123456780000ULL, "", false) == 0x0100123456780800ULL);
    namespace plan = sphaira::ownfoil::plan;
    const plan::Version custom_base{65536, "0500123456780000", true, true};
    const plan::Version custom_update{131072, "0500123456780807", false, true};
    const plan::Version unknown_update{0, "0500123456780807", false, false};
    auto selection = plan::Build(0x0500123456780000ULL, custom_base, custom_base, {}, false);
    assert(selection.size() == 1 && selection[0].file == plan::File::Chosen && selection[0].app_id == 0x0500123456780000ULL);
    selection = plan::Build(0x0500123456780000ULL, custom_update, custom_base, {}, false);
    assert(selection.size() == 2 && selection[0].app_id == 0x0500123456780000ULL && selection[1].app_id == 0x0500123456780807ULL);
    assert(plan::Offered(unknown_update, {true, 65536}, false));
    assert(plan::Needed(unknown_update, {true, 65536}, false));
    selection = plan::Build(0x0500123456780000ULL, unknown_update, custom_base, {true, 65536}, false);
    assert(selection.size() == 1 && selection[0].file == plan::File::Chosen && selection[0].app_id == 0x0500123456780807ULL);
    const plan::Version explicit_zero_update{0, "0500123456780807", false, true};
    assert(plan::Needed(explicit_zero_update, {true, 0}, false));
    selection = plan::Build(0, {0, "", true, false}, {}, {}, false);
    assert(selection.size() == 1 && selection[0].app_id == 0);
    assert(!plan::Needed(custom_update, {true, 131072}, false));
    assert(!plan::Offered(custom_update, {true, 262144}, false));
    assert(plan::Offered(custom_update, {true, 262144}, true));
    const auto forwarders = R"({"files":[
      {"url":"/One [0500123456780800][v65536].nsp"},
      {"url":"/Two [0500123456781001][v0].nsp"},
      {"url":"/Three [0500123456780800][v0].nsp"},
      {"url":"/Explicit.nsp","app_id":"0500123456780800","app_type":"UPDATE","parentTitleId":"0500123456780000"}
    ]})";
    assert(Parse(forwarders, std::strlen(forwarders), files, error));
    assert(files.size() == 4 && files[0].id == "0500123456780800");
    assert(files[0].kind == Kind::Unknown && files[1].kind == Kind::Unknown);
    assert(files[0].title_id.empty() && files[1].title_id.empty());
    assert(Key(files[0]) != Key(files[1]) && Key(files[0]) != Key(files[2]));
    assert(files[3].kind == Kind::Update && files[3].title_id == "0500123456780000");
    const auto generic = R"({"files":[{"url":"/retro/First.nsp","coverUrl":"/covers/first.png"},{"url":"/retro/Second.nsp","thumbnail":{"url":"/covers/second.png"}}]})";
    assert(Parse(generic, std::strlen(generic), files, error));
    assert(files.size() == 2 && files[0].id.empty() && files[1].id.empty());
    assert(files[0].title_id.empty() && files[1].title_id.empty());
    assert(Key(files[0]) != Key(files[1]));
    assert(files[0].icon == "/covers/first.png" && files[1].icon == "/covers/second.png");
    // Replacing server metadata updates both catalog membership and artwork URL.
    CatalogCache cache{"https://shop.invalid/", "fixture-user", "fixture-pass", std::make_shared<const std::vector<File>>(files), false};
    assert(cache.Matches("https://shop.invalid/", "fixture-user", "fixture-pass"));
    cache.Invalidate("https://shop.invalid/", "another-user", "fixture-pass");
    assert(cache.files && cache.files->size() == 2);
    cache.Invalidate("https://shop.invalid/", "fixture-user", "fixture-pass");
    assert(!cache.files);
    const auto refreshed = R"({"files":[{"url":"/retro/First.nsp","coverUrl":"/covers/first-v2.png"},{"url":"/retro/Second.nsp"},{"url":"/retro/New.nsp","coverUrl":"/covers/new.png"}]})";
    assert(Parse(refreshed, std::strlen(refreshed), files, error));
    cache = {"https://shop.invalid/", "fixture-user", "fixture-pass", std::make_shared<const std::vector<File>>(files), false};
    assert(cache.files->size() == 3 && cache.files->front().icon == "/covers/first-v2.png");
    const auto art_json = R"({"games":[
      {"app_id":"0100123456780000","iconUrl":"https://art.invalid/base.png"},
      {"filename":"First.nsp","coverUrl":"https://art.invalid/first.png","platform":"PS1"},
      {"filename":"Ambiguous.nsp","coverUrl":"https://art.invalid/a.png"},
      {"filename":"Ambiguous.nsp","coverUrl":"https://art.invalid/b.png"},
      {"app_id":"0100123456790000","bannerUrl":"//placehold.it/400x200"}
    ]})";
    std::vector<Artwork> artwork;
    assert(ParseArtwork(art_json, std::strlen(art_json), artwork));
    assert(artwork.size() == 5 && artwork.back().banner.empty());
    const auto with_art = R"({"files":[{"url":"/Game [0100123456780000].nsp"},{"url":"/First.nsp"},{"url":"/Second.nsp"},{"url":"/Ambiguous.nsp"}]})";
    assert(Parse(with_art, std::strlen(with_art), files, error));
    const auto generic_key = Key(files[1]);
    ApplyArtwork(files, artwork);
    assert(files[0].icon == "https://art.invalid/base.png");
    assert(files[1].icon == "https://art.invalid/first.png" && files[1].platform == "PlayStation");
    assert(Key(files[1]) == generic_key && files[1].id.empty());
    assert(files[2].icon.empty() && files[3].icon.empty());
    const auto web_html = R"(<html><script type="application/json" id="catalogGamesData">{"7":{"title_id":"0500123456780800","filename":"One.nsp","icon_url":"https://cdn.invalid/grid/one.png","banner_url":"https://cdn.invalid/hero/one.png","platform":"PS1"}}</script></html>)";
    assert(ParseArtwork(web_html, std::strlen(web_html), artwork));
    assert(artwork.size() == 1 && artwork[0].id == "0500123456780800");
    assert(artwork[0].icon == "https://cdn.invalid/grid/one.png" && artwork[0].platform == "PlayStation");
    const auto public_info = R"({"id":"0100123456780000","category":["Puzle",null,"Aventura"],"description":null,"intro":"Recorre el castillo.","icon":"https://example.invalid/icon"})";
    Artwork title;
    assert(!ParsePublicTitle(public_info, std::strlen(public_info), "0100123456770000", title));
    assert(!ParsePublicTitle("<html>", 6, "0100123456780000", title));
    assert(ParsePublicTitle(public_info, std::strlen(public_info), "0100123456780000", title));
    assert(title.genre == "Puzle, Aventura" && title.description == "Recorre el castillo." && !title.icon.empty());
    const auto described = R"({"files":[{"url":"/index [0100123456780000].nsp","genre":"Puzzle","description":"Own synopsis"}]})";
    assert(Parse(described, std::strlen(described), files, error));
    const std::vector<Artwork> supplemental{title};
    ApplyArtwork(files, IndexArtwork(supplemental));
    assert(files.front().genre == "Puzzle" && files.front().description == "Own synopsis");
    assert(!ParseArtwork("<html>Login required</html>", 27, artwork));
    std::cout << "Ownfoil JSON/HTML/platform/refresh/artwork parser fixtures passed\n";
    if (argc >= 2) {
        std::ifstream stream(argv[1], std::ios::binary);
        assert(stream.good());
        const std::string data{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        assert(ParseHtml(data.data(), data.size(), files, error));
        const auto folders = std::count_if(files.begin(), files.end(), [](const auto& f) { return f.directory; });
        const auto ids = std::count_if(files.begin(), files.end(), [](const auto& f) { return !f.id.empty(); });
        std::cout << "Private fixture: entries=" << files.size() << " directories=" << folders << " recognized IDs=" << ids << '\n';
        if (argc >= 3) {
            std::ifstream metadata_stream(argv[2], std::ios::binary);
            assert(metadata_stream.good());
            const std::string metadata_data{std::istreambuf_iterator<char>(metadata_stream), std::istreambuf_iterator<char>()};
            assert(ParseArtwork(metadata_data.data(), metadata_data.size(), artwork));
            ApplyArtwork(files, artwork);
            const auto covered = std::count_if(files.begin(), files.end(), [](const auto& f) { return !f.icon.empty() || !f.banner.empty(); });
            std::cout << "Private artwork fixture: records=" << artwork.size() << " matched entries with real artwork=" << covered << '\n';
        }
    }
}
