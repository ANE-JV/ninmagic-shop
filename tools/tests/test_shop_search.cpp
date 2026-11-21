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

struct Menu {
    enum class Mode { Home };
    s64 m_category{};
    std::string m_search, m_platform, m_base_url{"https://shop.invalid/"};
    std::vector<std::string> m_directory_stack, m_folder_names;
    bool m_consolidated{true}, m_connecting{}, m_sidebar_focus{}, m_detail_focus{};
    std::optional<int> m_pending_page;
    int fetch_count{};
    s64 page{-1};
    void FreeEntries() {}
    void SetMode(Mode) {}
    void SetCatalogTitle() {}
    void LoadPage(s64 requested) { ++fetch_count; page = requested; }
    auto SearchScope() const -> std::string;
    void ShowSearch(s64 index);
};

// Built from the product's ShowSearch definition by test-ownfoil.py. Only the
// keyboard and screen refresh are substitutes; the state transition is real.
#include "production_show_search.inc"

static void CheckScope(Menu screen, const std::string& expected_label) {
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
    assert(screen.fetch_count == 1 && screen.page == 0);
    assert(swkbd::hint.find(expected_label) != std::string::npos);
    screen.ShowSearch(4);
    assert(screen.fetch_count == 1);
    swkbd::result = 1;
    swkbd::answer = "not accepted";
    screen.ShowSearch(4);
    assert(screen.m_search == "mario" && screen.fetch_count == 1);
}

int main() {
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
    std::cout << "Search action retained its console, directory or whole-library scope\n";
}
