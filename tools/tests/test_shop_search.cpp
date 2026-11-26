#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using s64 = std::int64_t;
constexpr int SEARCH_MAX = 128;
constexpr bool R_FAILED(int result) { return result != 0; }

namespace swkbd {
std::string answer, hint;
int result;
int ShowText(std::string& text, const std::string&, const std::string& subtitle, const char*, int, int) {
    hint = subtitle;
    text = answer;
    return result;
}
}

namespace sphaira::ownfoil::api {
std::vector<std::string> available;
auto CachedPlatforms(const std::string&, int) -> std::vector<std::string> { return available; }
int CachedCatalogStatus(const std::string&, int) { return 1; }
}

struct Menu {
    enum class Mode { Home };
    s64 m_category{};
    std::string m_search, m_platform, m_base_url{"https://shop.invalid/"};
    std::vector<std::string> m_directory_stack, m_folder_names;
    std::vector<std::string> m_platform_choices{"Nintendo Switch"};
    struct Entry {
        struct App { bool directory{}; std::string platform; } app;
    };
    std::vector<Entry> m_entries;
    int m_config{}, m_catalog_status{};
    bool m_consolidated{true}, m_connecting{}, m_sidebar_focus{}, m_detail_focus{};
    std::size_t m_sidebar_index{};
    std::optional<std::size_t> m_toolbar_index;
    std::optional<int> m_pending_page;
    int fetch_count{};
    s64 page{-1};
    void FreeEntries() {}
    void SetMode(Mode) {}
    void SetCatalogTitle() {}
    void LoadPage(s64 requested) { ++fetch_count; page = requested; }
    auto PlatformChoices() const -> const std::vector<std::string>& { return m_platform_choices; }
    auto SearchScope() const -> std::string;
    void ShowSearch(s64 index);
    void UpdateCatalogSummary();
    void SelectPlatform(const std::string& platform);
    void ActivateSidebar();
};

// Built from the product's ShowSearch definition by test-ownfoil.py. Only the
// keyboard and screen refresh are substitutes; the state transition is real.
#include "production_show_search.inc"

static void CheckScope(Menu screen, const std::string& expected_label) {
    screen.m_toolbar_index = 0;
    const Menu before = screen;
    swkbd::result = 0;
    swkbd::answer = "mario";
    screen.ShowSearch(4);
    assert(screen.m_platform == before.m_platform);
    assert(screen.m_base_url == before.m_base_url);
    assert(screen.m_directory_stack == before.m_directory_stack);
    assert(screen.m_folder_names == before.m_folder_names);
    assert(screen.m_consolidated == before.m_consolidated);
    assert(screen.m_category == 4 && screen.m_search == "mario");
    assert(screen.fetch_count == 1 && screen.page == 0 && !screen.m_toolbar_index);
    assert(swkbd::hint.find(expected_label) != std::string::npos);
    screen.ShowSearch(4);
    assert(screen.fetch_count == 1);
    swkbd::result = 1;
    swkbd::answer = "not accepted";
    screen.ShowSearch(4);
    assert(screen.m_search == "mario" && screen.fetch_count == 1);
}

static void CheckPlatformSelection() {
    Menu screen;
    screen.m_platform_choices = {"Nintendo Switch", "NES", "Super Nintendo"};
    screen.m_platform = "Nintendo Switch";
    screen.m_sidebar_focus = true;
    screen.m_sidebar_index = 1;
    screen.m_detail_focus = true;
    screen.m_toolbar_index = 0;
    screen.m_category = 4;
    screen.m_search = "mario";
    screen.m_pending_page = 3;
    screen.page = 2;
    screen.m_consolidated = false;
    screen.m_directory_stack = {screen.m_base_url};
    screen.m_base_url += "old-folder/";
    screen.m_folder_names = {"old-folder"};

    screen.ActivateSidebar();
    assert(screen.m_platform == "NES" && screen.m_sidebar_focus && screen.m_sidebar_index == 1);
    assert(!screen.m_detail_focus && !screen.m_toolbar_index);
    assert(screen.m_category == 0 && screen.m_search.empty() && !screen.m_pending_page);
    assert(screen.m_consolidated && screen.m_directory_stack.empty() && screen.m_folder_names.empty());
    assert(screen.m_base_url == "https://shop.invalid/" && screen.page == 0 && screen.fetch_count == 1);

    screen.ActivateSidebar();
    assert(screen.m_platform == "NES" && screen.m_sidebar_focus && screen.fetch_count == 2);
    screen.m_sidebar_index = 2;
    screen.ActivateSidebar();
    assert(screen.m_platform == "Super Nintendo" && screen.m_sidebar_focus && screen.m_sidebar_index == 2);
    assert(screen.fetch_count == 3);

    screen.m_sidebar_focus = false;
    screen.SelectPlatform("Nintendo Switch");
    assert(screen.m_platform == "Nintendo Switch" && !screen.m_sidebar_focus && screen.m_sidebar_index == 0);
    assert(screen.page == 0 && screen.fetch_count == 4);
}

int main() {
    sphaira::ownfoil::api::available = {"Arcade", "NES", "Nintendo Switch", "Super Nintendo"};
    Menu catalog;
    catalog.UpdateCatalogSummary();
    auto all = catalog.m_platform_choices;
    assert(all == (std::vector<std::string>{"Nintendo Switch", "Arcade", "NES", "Super Nintendo"}));
    sphaira::ownfoil::api::available.clear();
    catalog.m_entries = {{{false, "NES"}}, {{false, "Game Boy Advance"}}, {{true, "Ignored folder"}}};
    catalog.UpdateCatalogSummary();
    all.emplace_back("Game Boy Advance");
    assert(catalog.m_platform_choices == all);
    catalog.m_entries.clear();
    catalog.UpdateCatalogSummary();
    assert(catalog.m_platform_choices == all && catalog.m_catalog_status == 1);

    Menu screen;
    CheckScope(screen, "toda la biblioteca");
    for (const auto scope : {"Nintendo Switch", "Game Boy Advance", "__unknown__"}) {
        screen.m_platform = scope;
        CheckScope(screen, screen.m_platform == "__unknown__" ? "Sin clasificar" : screen.m_platform);
    }
    screen.m_consolidated = false;
    screen.m_platform.clear();
    screen.m_directory_stack = {"https://shop.invalid/"};
    screen.m_folder_names = {"GBA"};
    screen.m_base_url = "https://shop.invalid/GBA/";
    CheckScope(screen, "GBA");
    CheckPlatformSelection();
    std::cout << "Search scope and sidebar focus retained when changing console\n";
}
