#include "ui/menus/ownfoil.hpp"
#include "ui/menus/ownfoil_title.hpp"
#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/install_queue.hpp"
#include "ui/shop_style.hpp"
#include "ui/shop_catalog_panel.hpp"
#include "ui/shop_layout.hpp"
#include "utils/utils.hpp"

#include "app.hpp"
#include "image.hpp"
#include "swkbd.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "defines.hpp"
#include "evman.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <utility>

namespace sphaira::ui::menu::ownfoil {
namespace {

// bumped by every install from a shop page. read by the fetch worker too.
std::atomic<u32> g_installs{};

// the list stops short of the menu body to leave room for the discover button,
// which is why it pages 4 rows rather than filling the area.
constexpr Vec4 LIST_POS{296.f, 182.f, 906.f, 344.f};
constexpr Vec4 LIST_ITEM{314.f, 192.f, 870.f, 73.f};
constexpr Vec2 LIST_PAD{0.f, 12.f};
constexpr Vec4 DISCOVER_BUTTON{314.f, 548.f, 870.f, 48.f};
constexpr auto ToVec4(shop::Rect rect) -> Vec4 { return {rect.x,rect.y,rect.w,rect.h}; }
constexpr auto GRID_POS = ToVec4(shop::layout::Grid);
constexpr auto GRID_ITEM = ToVec4(shop::layout::Card);
constexpr Vec2 GRID_PAD{shop::layout::ColumnGap,shop::layout::RowGap};
constexpr Vec4 SIDEBAR{30.f, 108.f, 240.f, 508.f};
constexpr auto SEARCH_BOX = ToVec4(shop::layout::Search);
constexpr auto FOLDERS_BUTTON = ToVec4(shop::layout::Sections);
constexpr auto DETAIL_PANEL = ToVec4(shop::layout::Detail);
constexpr s64 PAGE_SIZES[]{20,30,50,100};

// the title row draws the term back, so the cap keeps the header from scrolling.
constexpr s64 SEARCH_MAX = 64;

// what "Sort" offers. the shop's OrderField has more members, but VERSION is the
// app's own version (0 on every retail base game) and SIZE/DOWNLOAD_COUNT aren't
// app fields at all.
struct SortMode {
    const char* field; // the shop's own OrderField name.
    const char* name;  // shown in the sidebar, translated at use.
};

// a bullet keeps the title row's parts apart without reading as a range.
constexpr const char* SEPARATOR = "  \u2022  ";

constexpr SortMode SORT_MODES[] = {
    {"NAME", "Name"},
    {"ADDED_AT", "Date added"},
    {"RELEASE_DATE", "Release date"},
};

// the first three are the shop's catalog crossed with what this console holds,
// which is why they cost a scan of it; the last two only ask the shop.
struct CategoryMode {
    sphaira::ownfoil::api::Category category;
    const char* name; // shown in the panel, translated at use.
};

// the first is what every connect opens on: what the shop can add to this
// console is the reason to open it.
constexpr CategoryMode CATEGORIES[] = {
    {sphaira::ownfoil::api::Category::All, "All games"},
    {sphaira::ownfoil::api::Category::NewGames, "New games"},
    {sphaira::ownfoil::api::Category::Updates, "Updates"},
    {sphaira::ownfoil::api::Category::Dlc, "DLC"},
    {sphaira::ownfoil::api::Category::Favorites, "Favorites"},
    {sphaira::ownfoil::api::Category::Search, "Search"},
};

} // namespace

auto Menu::MakeTitlePage(const sphaira::ownfoil::api::ShopApp& app) -> TitlePage {
    TitlePage page;
    page.dlc = app.type == sphaira::ownfoil::api::AppType::Dlc;
    page.id = page.dlc ? app.app_id : app.title_id;
    page.artwork_id = app.artwork_id;
    page.game_id = app.title_id;
    page.name = app.name;
    page.publisher = app.publisher;
    page.platform = app.platform;
    page.genre = app.genre;
    page.description = app.description;
    page.game_name = app.game_name;
    page.banner = app.banner;
    page.icon = app.icon;
    page.download = app.download;
    const auto& entry = m_entries[m_entry_index];
    const auto& cover = app.icon.url.empty() && !app.banner.url.empty() ? entry.banner : entry.icon;
    if (cover.state == ImageState::Done || cover.state == ImageState::Progress) page.card_texture = cover.texture;
    return page;
}

Menu::Menu(u32 flags, std::function<void()> check_update)
: grid::Menu{"NinMagic Shop", flags}, m_check_update{std::move(check_update)} {
    SetShopLayout();
    SetShopAccountAction([this] { if (m_mode == Mode::Home) ShowServerProfiles(); else ShowOptions(); });
    // B leaves the menu from the catalog too: the server list is reached from
    // the catalog's options, not by backing out.
    SetAction(Button::B, Action{"Back"_i18n, [this]{
        // backing out of a connect leaves the list up, not the menu.
        if (m_connecting) {
            m_connect_stop.request_stop();
            m_connecting = false;
            SetShopConnection(false, false);
            return;
        }
        if (m_mode == Mode::Home && m_detail_focus) { m_detail_focus = false; return; }
        if (m_mode == Mode::Home && m_sidebar_focus) { m_sidebar_focus = false; return; }
        if (m_mode == Mode::Home && m_toolbar_index) { m_toolbar_index.reset(); return; }
        if (m_mode == Mode::Home && !m_directory_stack.empty()) {
            GoUp();
            return;
        }
        if (m_mode == Mode::Home) {
            ShowServerProfiles();
            return;
        }
        if (!m_base_url.empty() && !m_config.uid.empty()) {
            SetMode(Mode::Home);
            LoadPage(m_page);
            return;
        }
        SetPop();
    }});

    SetAction(Button::A, Action{"Select"_i18n, [this]{
        if (m_mode == Mode::Home) {
            if (m_sidebar_focus) { ActivateSidebar(); return; }
            if (m_toolbar_index) { ActivateToolbar(); return; }
            if (m_detail_focus && m_preview) { Controller press{}; press.Set(Button::A, true); m_detail_focus = m_preview->HandleDetailController(&press); return; }
            OpenSelected();
            return;
        }

        if (m_mode != Mode::ServerList || m_connecting) {
            return;
        }

        if (m_focus_discover) {
            StartDiscovery();
        } else {
            OnServerSelected(m_index);
        }
    }});

    SetAction(Button::X, Action{"Buscar", [this]{
        if (m_connecting) {
            return;
        }
        if (m_mode == Mode::Home) {
            SelectCategory(sphaira::ownfoil::api::Category::Search);
        } else {
            AddServer();
        }
    }});

    m_list = std::make_unique<List>(1, 4, LIST_POS, LIST_ITEM, LIST_PAD);

    // a hand-edited ini must not leave the sidebar pointing past the list, nor
    // BuildQuery indexing past SORT_MODES.
    if (m_sort.Get() < 0 || m_sort.Get() >= static_cast<s64>(std::size(SORT_MODES))) {
        m_sort.Set(0);
    }

    SetMode(Mode::ServerList);
    const auto saved_page_size = m_page_size.Get();
    const auto page_size = std::lower_bound(std::begin(PAGE_SIZES), std::end(PAGE_SIZES), saved_page_size);
    m_page_size.Set(saved_page_size <= 0 ? 30 : page_size == std::end(PAGE_SIZES) ? PAGE_SIZES[std::size(PAGE_SIZES) - 1] : *page_size);
    fs::FsNativeSd().GetFreeSpace("/", &m_sd_free);
}

Menu::~Menu() {
    // ~Async blocks on the worker and runs before the base Object dtor that would
    // cancel it, so the ~1.5s discovery window is cut short here rather than
    // waited out; m_page_stop drops the artwork still queued for the last page.
    m_stop_source.request_stop();
    m_page_stop.request_stop();
    m_connect_stop.request_stop();
    m_connect_async.reset();
}

void Menu::SetMode(Mode mode) {
    const auto was_home = m_mode == Mode::Home;
    if (mode != m_mode) m_toolbar_index.reset();
    m_mode = mode;
    SetShopConnection(mode == Mode::Home, m_connecting);
    SetAction(Button::X, Action{mode == Mode::Home ? "Buscar" : "Añadir tienda", [this] {
        if (m_mode == Mode::Home) SelectCategory(sphaira::ownfoil::api::Category::Search); else AddServer();
    }});
    auto primary = m_actions.at(Button::A);
    primary.m_hint = mode == Mode::Home ? "Ver juego" : "Conectar";
    SetAction(Button::A, primary);

    if (mode == Mode::Home) {
        if (!was_home) { m_sidebar_focus = false; m_detail_focus = false; m_sidebar_index = 0; m_sidebar_offset = 0; }
        // browsing a shop, the header names it rather than the menu.
        SetTitle(m_config.name);

        // '-' and Y open MainMenu's options and menu picker while this menu is a
        // tab, so remember whatever held them to hand back.
        if (!was_home) {
            m_prev_select = FindAction(Button::SELECT);
            m_prev_y = FindAction(Button::Y);
        }
        SetAction(Button::Y, Action{"Cola", [] { InstallQueue::Get().ShowMenu(); }});
        SetAction(Button::R3, Action{[this] { CycleView(); }});
        SetAction(Button::SELECT, Action{[this]{
            ShowOptions();
        }});
        // no hint on ZL: the bar draws it beside ZR, in front of the one hint.
        SetAction(Button::L2, Action{[this]{
            if (m_page > 0) {
                LoadPage(m_page - 1);
            }
        }});
        SetAction(Button::R2, Action{[this]{
            if (m_page + 1 < GetPageMax()) {
                LoadPage(m_page + 1);
            }
        }});
        OnViewChange();
    } else {
        if (was_home) {
            FreeEntries();
            m_pending_page.reset();
            RestoreAction(Button::SELECT, m_prev_select);
            RestoreAction(Button::Y, m_prev_y);
            RemoveAction(Button::L2);
            RemoveAction(Button::R2);
            RemoveAction(Button::R3);
            SetTitle("NinMagic Shop");
            SetTitleSubHeading("");
            SetSubHeading("");
            m_list = std::make_unique<List>(1, 4, LIST_POS, LIST_ITEM, LIST_PAD);
        }
        SetTitleSubHeading("Selecciona una tienda y pulsa Y para editarla");
        SetAction(Button::SELECT, Action{"Opciones", [this] { ShowOptions(); }});
        UpdateProfileActions();
    }
}

auto Menu::FindAction(Button button) const -> std::optional<Action> {
    if (const auto it = m_actions.find(button); it != m_actions.end()) {
        return it->second;
    }
    return {};
}

void Menu::RestoreAction(Button button, std::optional<Action>& prev) {
    if (prev) {
        SetAction(button, *prev);
    } else {
        RemoveAction(button);
    }
    prev.reset();
}

void Menu::FreeEntries() {
    // more than tidiness: a queued download returns at once on a stopped token,
    // which hands the four download threads to the page arriving rather than
    // making it wait out a screenful of artwork nobody will see.
    m_page_stop.request_stop();
    m_page_stop = std::stop_source{};
    m_loading = false;
    m_entries.clear();
    m_entry_index = 0;
    RetirePreview();
    m_preview_id.clear();
    m_preview_changed_tick = armGetSystemTick();
    m_detail_focus = false;
}

void Menu::CycleView() {
    m_list_view.Set(!m_list_view.Get());
    OnViewChange();
}

void Menu::UpdateCatalogSummary() {
    const auto available = sphaira::ownfoil::api::CachedPlatforms(m_base_url, m_config);
    // Keep discoveries for the connected shop. A filtered page or folder
    // may supply more evidence, but it cannot remove another console.
    const auto add_platform = [this](const std::string& label) {
        if (!label.empty() && std::find(m_platform_choices.begin(), m_platform_choices.end(), label) == m_platform_choices.end())
            m_platform_choices.push_back(label);
    };
    for (const auto& label : available) add_platform(label);
    for (const auto& entry : m_entries) {
        if (!entry.app.directory) add_platform(entry.app.platform);
    }
    m_catalog_status = sphaira::ownfoil::api::CachedCatalogStatus(m_base_url, m_config);
}
void Menu::SelectPlatform(const std::string& platform) {
    m_platform = platform;
    m_consolidated = true;
    const auto choices = PlatformChoices();
    const auto selected = std::find(choices.begin(), choices.end(), platform);
    if (selected != choices.end()) m_sidebar_index = static_cast<size_t>(selected - choices.begin());
    m_category = 0;
    m_search.clear();
    // Consoles refer to the shop library, even when a legacy folder was open.
    if (!m_directory_stack.empty()) m_base_url = m_directory_stack.front();
    m_directory_stack.clear(); m_folder_names.clear();
    m_sidebar_focus = false; m_detail_focus = false;
    m_toolbar_index.reset();
    FreeEntries(); m_pending_page.reset(); LoadPage(0);
}
void Menu::ShowServerFolders() {
    if (!m_config.json_index || !m_config.direct_catalog.empty() || m_base_url.empty() || m_connecting) return;
    if (!m_directory_stack.empty()) m_base_url = m_directory_stack.front();
    m_directory_stack.clear(); m_folder_names.clear();
    m_platform.clear(); m_search.clear(); m_category = 0;
    m_consolidated = false; m_sidebar_focus = false; m_detail_focus = false;
    m_toolbar_index.reset();
    FreeEntries(); m_pending_page.reset(); LoadPage(0);
}
void Menu::CyclePlatform(int direction) {
    if (m_mode != Mode::Home || m_connecting) return;
    const auto choices = PlatformChoices();
    if (choices.empty()) return;
    const auto current = std::find(choices.begin(), choices.end(), m_platform);
    const auto count = static_cast<s64>(choices.size());
    const auto index = current == choices.end() ? (direction > 0 ? -1 : 0) : static_cast<s64>(current - choices.begin());
    m_sidebar_index = static_cast<size_t>((index + (direction > 0 ? 1 : -1) + count) % count);
    SelectPlatform(choices[m_sidebar_index]);
}
void Menu::ActivateSidebar() {
    const auto& choices = PlatformChoices();
    if (m_sidebar_index < choices.size()) SelectPlatform(choices[m_sidebar_index]);
}

void Menu::FocusToolbar(size_t index) {
    m_sidebar_focus = false;
    m_detail_focus = false;
    m_toolbar_index = index;
}

void Menu::ActivateToolbar() {
    if (*m_toolbar_index == 0) InstallQueue::Get().ShowMenu();
    else if (*m_toolbar_index == 1) ShowOptions();
    else if (m_check_update) m_check_update();
}
void Menu::RetirePreview() {
    if (!m_preview) return;
    if (m_preview.use_count() == 1) m_preview->CancelPreview();
    m_retired_preview = std::move(m_preview);
}

void Menu::UpdatePreview(bool immediate) {
    if (m_retired_preview && m_retired_preview->PreviewWorkersFinished()) m_retired_preview.reset();
    if (m_mode != Mode::Home || m_entry_index < 0 || m_entry_index >= static_cast<s64>(m_entries.size())) return;
    const auto& selected = m_entries[m_entry_index].app;
    const auto identity = m_base_url + "|" + selected.app_id + "|" + selected.download.url + "|" + selected.name;
    if (identity != m_preview_id) {
        RetirePreview(); m_preview_id = identity; m_preview_changed_tick = armGetSystemTick();
    }
    if (selected.directory) return;
    if (!m_preview && (immediate || (!m_retired_preview && armTicksToNs(armGetSystemTick() - m_preview_changed_tick) >= 160000000ULL)))
        m_preview = std::make_shared<TitleMenu>(m_config, m_base_url, MakeTitlePage(selected), TitleMenu::Mode::Preview);
    if (!immediate && m_preview) m_preview->PollPreview();
}

void Menu::OnViewChange() {
    const auto first_visible = static_cast<s64>(m_list->GetYoff() / m_list->GetMaxY()) * m_list->GetRow();
    if (m_list_view.Get()) {
        m_list = std::make_unique<List>(1, shop::layout::VisibleListRows, GRID_POS,
            ToVec4(shop::layout::ListCard), Vec2{0.f, shop::layout::ListRowGap});
    } else {
        m_list = std::make_unique<List>(shop::layout::Columns, shop::layout::VisibleCards, GRID_POS, GRID_ITEM, GRID_PAD);
    }
    m_list->SetPageJump(false);
    m_list->SetScrollBarPos(shop::layout::GridScrollX, shop::layout::GridScrollY, shop::layout::GridScrollHeight);
    ScrollToSelection(first_visible);
}

void Menu::ScrollToSelection(s64 first_visible) {
    const auto columns = m_list->GetRow();
    const auto visible_rows = m_list->GetPage() / columns;
    const auto total_rows = (static_cast<s64>(m_entries.size()) + columns - 1) / columns;
    const auto selected_row = m_entry_index / columns;
    const auto first = std::max<s64>(0, selected_row - visible_rows + 1);
    const auto last = std::min(selected_row, std::max<s64>(0, total_rows - visible_rows));
    m_list->SetYoff(std::clamp(first_visible / columns, first, last) * m_list->GetMaxY());
}

bool Menu::MoveAcrossPage(Controller* controller, TouchInfo* touch) {
    const auto columns = m_list->GetRow();
    const auto count = static_cast<s64>(m_entries.size());
    const auto last_row = (count - 1) / columns;
    bool forward = controller->GotDown(Button::DOWN) && m_entry_index / columns == last_row;
    bool backward = controller->GotDown(Button::UP) && m_entry_index < columns;
    if (touch->is_end && touch->initial.x >= GRID_POS.x && touch->initial.x < GRID_POS.x + GRID_POS.w
        && touch->initial.y >= GRID_POS.y && touch->initial.y < GRID_POS.y + GRID_POS.h) {
        const auto distance = static_cast<float>(touch->initial.y) - static_cast<float>(touch->cur.y);
        const auto visible_rows = m_list->GetPage() / columns;
        const auto bottom = std::max<s64>(0, last_row + 1 - visible_rows) * m_list->GetMaxY();
        forward = distance >= 48.f && m_list->GetYoff() >= bottom - 1.f;
        backward = distance <= -48.f && m_list->GetYoff() < 1.f;
        if (forward || backward) {
            // Finish the gesture before resetting its viewport on a new page.
            m_list->OnUpdate(controller, touch, m_entry_index, count, [](bool, s64) {});
        }
    }
    if (!forward && !backward) return false;
    const auto target = shop::AdjacentPage(m_page, GetPageMax(), m_page_size.Get(), columns, m_entry_index, forward, backward);
    if (!target) {
        if (forward && !backward && controller->GotDown(Button::DOWN)) {
            FocusToolbar();
            return true;
        }
        return false;
    }
    if (!m_loading && !m_pending_page) LoadPage(target->page, target->focus);
    return true;
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_mode == Mode::Home && !m_loading && m_pending_page && (!m_loader || m_loader->HasFinished())) {
        const auto next = *m_pending_page;
        m_pending_page.reset();
        LoadPage(next.page, next.focus);
    }
    if (m_mode == Mode::ServerList && touch->is_clicked) {
        if (m_check_update && touch->in_range(ToVec4(shop::layout::ProfileUpdate))) { m_check_update(); return; }
        if (touch->in_range(Vec4{49.f, 176.f, 214.f, 42.f})) { AddServer(); return; }
        if (touch->in_range(Vec4{49.f, 228.f, 214.f, 42.f})) { EditSelected(); return; }
    }
    if (m_mode == Mode::Home && m_installs_shown != g_installs) {
        m_installs_shown = g_installs;
        const auto category = CATEGORIES[m_category].category;
        if (category != sphaira::ownfoil::api::Category::All && category != sphaira::ownfoil::api::Category::Search) LoadPage(m_page);
    }
    if (m_mode == Mode::Home) {
        const std::string primary_hint = m_sidebar_focus || m_detail_focus || m_toolbar_index ? "Seleccionar" : "Ver juego";
        if (m_actions.at(Button::A).m_hint != primary_hint) { auto action = m_actions.at(Button::A); action.m_hint = primary_hint; SetAction(Button::A, action); }
        // The shell owns + and the header. Search/queue stay directly available
        // while sidebar and detail navigation handle only their own controls.
        if (!HasAction(Button::Y) || m_actions.at(Button::Y).m_hint != "Cola")
            SetAction(Button::Y, Action{"Cola", [] { InstallQueue::Get().ShowMenu(); }});
        if (touch->is_clicked) {
            if (m_preview && m_preview->HandleDetailTouch(touch)) {
                m_detail_focus = true; m_sidebar_focus = false; m_toolbar_index.reset(); return;
            }
            if (touch->in_range(SEARCH_BOX)) { ShowSearch(std::size(CATEGORIES) - 1); return; }
            if (touch->in_range(FOLDERS_BUTTON)) { ShowCategories(); return; }
            if (touch->in_range(ToVec4(shop::layout::ViewMode))) { CycleView(); return; }
            const auto& choices = PlatformChoices();
            for (size_t row = 0; row < shop::layout::VisiblePlatforms && m_sidebar_offset + row < choices.size(); ++row) if (touch->in_range(ToVec4(shop::layout::PlatformRow(row)))) {
                m_sidebar_index = m_sidebar_offset + row; m_sidebar_focus = true; m_detail_focus = false; ActivateSidebar(); return;
            }
            const std::array toolbar{shop::layout::Downloads, shop::layout::Settings, shop::layout::UpdateApp};
            for (size_t index = 0; index < (m_check_update ? toolbar.size() : toolbar.size() - 1); ++index) {
                if (!touch->in_range(ToVec4(toolbar[index]))) continue;
                FocusToolbar(index);
                ActivateToolbar();
                return;
            }
        }
        if ((touch->is_clicked || touch->is_scroll) && touch->in_range(GRID_POS)) {
            m_sidebar_focus = false;
            m_detail_focus = false;
            m_toolbar_index.reset();
        }
        const auto platform_viewport = shop::layout::PlatformViewport;
        if (touch->is_end && touch->initial.x >= platform_viewport.x && touch->initial.x <= platform_viewport.x + platform_viewport.w
            && touch->initial.y >= platform_viewport.y && touch->initial.y <= platform_viewport.y + platform_viewport.h) {
            const auto delta = static_cast<float>(touch->initial.y) - static_cast<float>(touch->cur.y);
            if (std::abs(delta) >= 24.f) {
                const auto choices = PlatformChoices();
                const auto step = static_cast<s64>(delta / shop::layout::PlatformStep);
                const auto last = static_cast<s64>(choices.size() > shop::layout::VisiblePlatforms ? choices.size() - shop::layout::VisiblePlatforms : 0);
                m_sidebar_offset = static_cast<size_t>(std::clamp(static_cast<s64>(m_sidebar_offset) + (step ? step : delta > 0 ? 1 : -1), s64{0}, last));
                m_sidebar_index = m_sidebar_offset;
                m_sidebar_focus = true; m_detail_focus = false;
                m_toolbar_index.reset();
                return;
            }
        }
        if (!m_sidebar_focus && !m_detail_focus && !m_toolbar_index) {
            const auto columns = m_list->GetRow();
            if (controller->GotDown(Button::LEFT) && (m_entries.empty() || m_entry_index % columns == 0)) {
                m_sidebar_focus = true;
            } else if (controller->GotDown(Button::RIGHT) && !m_entries.empty() && m_preview
                    && (m_entry_index % columns == columns - 1
                        || m_entry_index + 1 == static_cast<s64>(m_entries.size()))) {
                m_detail_focus = true;
            }
        }
    } else UpdateProfileActions();
    MenuBase::Update(controller, touch);
    if (m_mode == Mode::Home) {
        if (m_sidebar_focus) {
            const auto count = PlatformChoices().size();
            if (controller->GotDown(Button::UP) && m_sidebar_index > 0) --m_sidebar_index;
            if (controller->GotDown(Button::DOWN)) {
                if (m_sidebar_index + 1 < count) ++m_sidebar_index;
                else FocusToolbar();
            }
            if (controller->GotDown(Button::RIGHT)) m_sidebar_focus = false;
        } else if (m_toolbar_index) {
            const size_t last = m_check_update ? 2 : 1;
            if (controller->GotDown(Button::LEFT) && *m_toolbar_index > 0) --*m_toolbar_index;
            if (controller->GotDown(Button::RIGHT) && *m_toolbar_index < last) ++*m_toolbar_index;
            if (controller->GotDown(Button::UP)) m_toolbar_index.reset();
        } else if (m_detail_focus && m_preview) {
            if (controller->GotDown(Button::DOWN) && m_preview->IsLastDetailAction()) {
                FocusToolbar();
            } else {
                Controller navigation = *controller;
                navigation.Set(Button::A, false); navigation.Set(Button::B, false);
                m_detail_focus = m_preview->HandleDetailController(&navigation);
            }
        } else if (m_entries.empty() && controller->GotDown(Button::DOWN)) {
            FocusToolbar();
        } else if (!m_entries.empty() && !MoveAcrossPage(controller, touch)) {
            m_list->OnUpdate(controller, touch, m_entry_index, m_entries.size(), [this](bool tapped, s64 index) {
                if (tapped && m_entry_index == index) OpenSelected();
                else { m_entry_index = index; m_sidebar_focus = false; m_detail_focus = false; }
            });
        }
        UpdatePreview();
        return;
    }
    if (m_mode != Mode::ServerList || m_connecting) {
        return;
    }

    // taken before the list, whose touch bounds are the whole menu body.
    if (touch->is_clicked && touch->in_range(DISCOVER_BUTTON)) {
        m_focus_discover = true;
        FireAction(Button::A);
        return;
    }

    // with nothing in the list, the button is the only thing there is to focus.
    if (m_candidates.empty()) {
        return;
    }

    if (m_focus_discover) {
        if (controller->GotDown(Button::UP)) {
            App::PlaySoundEffect(SoundEffect::Focus);
            m_focus_discover = false;
            return;
        }

        // the list is kept out of the d-pad's way while the button has focus but
        // still sees touch, so a tap can pull focus back onto a row.
        if (!touch->is_clicked && !touch->is_scroll && !touch->is_end) {
            return;
        }
    } else if (controller->GotDown(Button::DOWN) && m_index == static_cast<s64>(m_candidates.size()) - 1) {
        App::PlaySoundEffect(SoundEffect::Focus);
        m_focus_discover = true;
        return;
    }

    m_list->OnUpdate(controller, touch, m_index, m_candidates.size(), [this](bool is_touch, s64 i) {
        // a tap on the already-selected row selects it, unless that row didn't
        // have focus to begin with - then the tap only takes it back.
        if (is_touch && !m_focus_discover && m_index == i) {
            FireAction(Button::A);
        } else {
            m_focus_discover = false;
            m_index = i;
        }
    });
    UpdateProfileActions();
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    if (m_mode == Mode::Home) { DrawHome(vg, theme); return; }
    shop::Panel(vg, SIDEBAR, shop::Cyan);
    shop::DrawIcon(vg, shop::Icon::User, 49.f, 121.f, 25.f, shop::Cyan);
    shop::Text(vg, 85.f, 134.f, 23.f, shop::White, "Mi cuenta", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    shop::Line(vg, 49.f, 154.f, 253.f, 154.f, shop::Alpha(shop::Cyan, .55f));
    shop::Button(vg, {49.f, 176.f, 214.f, 42.f}, "Añadir tienda", false, true, shop::Icon::Plus, true, 18.f);
    const bool editable = !m_focus_discover && m_index >= 0 && m_index < static_cast<s64>(m_candidates.size()) && m_candidates[m_index].saved;
    shop::Button(vg, {49.f, 228.f, 214.f, 42.f}, "Y  Editar tienda", editable, false, shop::Icon::Settings, true, 17.f);
    shop::Text(vg, 49.f, 301.f, 15.f, shop::Muted, "URL, puerto y ruta");
    shop::Text(vg, 49.f, 328.f, 15.f, shop::Muted, "Usuario y contraseña");
    shop::Text(vg, 49.f, 371.f, 14.f, shop::Muted, "Cada cuenta conserva");
    shop::Text(vg, 49.f, 394.f, 14.f, shop::Muted, "su propio perfil.");
    shop::Line(vg, 49.f, 438.f, 253.f, 438.f, shop::Border);
    shop::Text(vg, 49.f, 472.f, 15.f, shop::White, "A  Conectar tienda");
    shop::Text(vg, 49.f, 503.f, 15.f, shop::White, "X  Añadir tienda");
    shop::Text(vg, 49.f, 534.f, 15.f, shop::White, "Y  Editar selección");
    if (m_check_update) shop::Button(vg,shop::layout::ProfileUpdate,"Actualizar app",false,false,shop::Icon::Download,true,17.f);
    shop::Text(vg, 298.f, 129.f, 31.f, shop::White, "Tus tiendas", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    shop::Text(vg, 299.f, 157.f, 17.f, shop::Muted, "Selecciona un perfil para conectar o editar sus datos");
    shop::Panel(vg, {296.f, 178.f, 906.f, 432.f}, shop::Border);
    if (m_connecting) {
        shop::Text(vg, 748.f, 349.f, 24.f, shop::White, ("Conectando con " + m_connecting_name + "...").c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        shop::Text(vg, 748.f, 388.f, 17.f, shop::Muted, "B Cancelar para volver a tus tiendas", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }
    if (m_candidates.empty()) {
        shop::Text(vg, 748.f, 327.f, 25.f, shop::White, "Conecta tu tienda Ownfoil", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
        shop::Text(vg, 748.f, 371.f, 18.f, shop::Muted, "Pulsa X para añadir la URL, el usuario y la contraseña de tu tienda.", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    } else m_list->Draw(vg, theme, m_candidates.size(), [this](NVGcontext* vg, Theme*, const Vec4& row, s64 index) {
        const auto& candidate = m_candidates[index];
        const auto& config = candidate.config;
        const bool selected = !m_focus_discover && index == m_index;
        shop::Panel(vg, row, selected ? shop::Cyan : shop::Border, selected);
        shop::DrawIcon(vg, shop::Icon::User, row.x + 14.f, row.y + 22.f, 27.f, selected ? shop::Cyan : shop::Muted);
        shop::Text(vg, row.x + 53.f, row.y + 23.f, 21.f, shop::White, config.name.c_str(), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
        const auto& address = config.local_address.empty() ? config.remote_address : config.local_address;
        nvgSave(vg); nvgIntersectScissor(vg, row.x + 53.f, row.y + 39.f, row.w - 78.f, 25.f);
        shop::Text(vg, row.x + 53.f, row.y + 51.f, 16.f, shop::Muted, address.c_str());
        nvgRestore(vg);
        if (!candidate.saved) shop::Text(vg, row.x + row.w - 18.f, row.y + 22.f, 15.f, shop::Amber, "Nueva", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        else if (selected) shop::Text(vg, row.x + row.w - 18.f, row.y + 22.f, 15.f, shop::Cyan, "Y Editar", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, true);
    });
    DrawDiscoverButton(vg, theme);
}

void Menu::DrawHome(NVGcontext* vg, Theme* theme) {
    shop::CatalogPanelState state;
    state.list_view = m_list_view.Get();
    state.can_update = static_cast<bool>(m_check_update);
    state.platforms = PlatformChoices();
    if (m_sidebar_index < state.platforms.size()) {
        if (m_sidebar_index < m_sidebar_offset) m_sidebar_offset = m_sidebar_index;
        if (m_sidebar_index >= m_sidebar_offset + shop::layout::VisiblePlatforms) m_sidebar_offset = m_sidebar_index - shop::layout::VisiblePlatforms + 1;
    }
    m_sidebar_offset = std::min(m_sidebar_offset, state.platforms.size() > shop::layout::VisiblePlatforms ? state.platforms.size() - shop::layout::VisiblePlatforms : 0);
    state.sidebar_offset = m_sidebar_offset; state.sidebar_index = m_sidebar_index;
    state.sidebar_focused = m_sidebar_focus; state.detail_focused = m_detail_focus;
    state.toolbar_focused = m_toolbar_index.has_value();
    state.toolbar_index = m_toolbar_index.value_or(0);
    state.platform = m_platform;
    state.heading = !m_consolidated ? "Carpetas del servidor" : m_platform.empty() ? "Biblioteca" : m_platform == "__unknown__" ? "Sin clasificar" : m_platform;
    state.location = "Biblioteca";
    if (!m_folder_names.empty()) state.location += "  /  " + m_folder_names.back();
    else if (!m_config.name.empty()) state.location += "  /  " + m_config.name;
    state.search = m_search;
    state.search_hint = "Buscar en " + SearchScope() + "…";
    state.storage = m_sd_free >= 0 ? "microSD · " + utils::formatSizeStorage(m_sd_free) + " libres" : "microSD";
    state.page = m_fetch_error.empty() ? "Página " + std::to_string(m_page + 1) + " / " + std::to_string(GetPageMax()) + " · ZL / ZR" : "No se pudo cargar · − Opciones para reintentar";
    const auto& catalog_status = m_catalog_status;
    if (catalog_status.partial && m_fetch_error.empty()) state.page += " · Parcial: " + std::to_string(catalog_status.unresolved_folders) + " carpetas sin identificar";
    state.loading = m_loading;
    state.empty_text = !m_fetch_error.empty() ? "No se pudo cargar esta selección" : m_search.empty() ? "No hay juegos en esta selección" : "No hay resultados para esta búsqueda";
    int budget = 2;
    m_list->Draw(vg, theme, m_entries.size(), [this, &state, &budget](NVGcontext*, Theme*, const Vec4& bounds, s64 index) {
        auto& entry = m_entries[index];
        const auto favorite_id = entry.app.type == sphaira::ownfoil::api::AppType::Base ? entry.app.app_id : entry.app.title_id;
        const auto image = entry.app.directory ? 0 : GetImage(entry, entry.app.icon.url.empty() && !entry.app.banner.url.empty(), budget);
        state.cards.push_back({entry.app.name, image == App::GetDefaultImage() ? 0 : image, entry.app.directory,
            std::find(m_config.favorites.begin(), m_config.favorites.end(), favorite_id) != m_config.favorites.end(),
            entry.icon.state == ImageState::Progress || entry.banner.state == ImageState::Progress, index == m_entry_index, shop::Rect{bounds}});
    });
    shop::DrawCatalogPanel(vg, state);
    int selected_image{};
    for (const auto& card : state.cards) if (card.selected) { selected_image = card.image; break; }
    if (m_preview) {
        m_preview->SetDetailFocused(m_detail_focus);
        m_preview->DrawDetailPanel(vg, theme, DETAIL_PANEL, selected_image);
    } else if (!m_entries.empty() && !m_entries[m_entry_index].app.directory) {
        TitleMenu::DrawPreviewSummary(vg, DETAIL_PANEL, m_entries[m_entry_index].app, selected_image);
    } else {
        shop::Panel(vg, DETAIL_PANEL, shop::Violet);
        const auto center = DETAIL_PANEL.x + DETAIL_PANEL.w * .5f;
        shop::DrawIcon(vg, shop::Icon::Queue, center - 25.f, 274.f, 50.f, shop::Violet);
        const auto message = !m_entries.empty() && m_entries[m_entry_index].app.directory ? "Abre una carpeta para ver sus juegos" : "Selecciona un juego para ver su contenido";
        nvgSave(vg); nvgIntersectScissor(vg, DETAIL_PANEL.x + 12.f, DETAIL_PANEL.y + 225.f, DETAIL_PANEL.w - 24.f, 54.f);
        shop::Text(vg, center, 354.f, 16.f, shop::Muted, message, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); nvgRestore(vg);
    }
}

void Menu::QueuePublicArtwork(Entry& entry) {
    if (entry.artwork_tried || entry.app.directory || m_config.artwork_search_url.empty()
        || entry.app.artwork_id.empty() || (!entry.app.platform.empty() && !entry.app.icon.url.empty())) return;
    size_t slot{};
    while (slot < m_artwork_workers.size() && m_artwork_workers[slot] && !m_artwork_workers[slot]->HasFinished()) ++slot;
    if (slot == m_artwork_workers.size()) return;
    entry.artwork_tried = true;
    const auto index = &entry - m_entries.data();
    const auto id = entry.app.artwork_id;
    const auto provider = m_config.artwork_search_url;
    const auto token = m_page_stop.get_token();
    m_artwork_workers[slot] = std::make_unique<utils::Async>([this, index, id, provider, token] {
        sphaira::ownfoil::api::ShopApp artwork;
        const auto found = sphaira::ownfoil::api::LookupPublicArtwork(provider, id, token, artwork);
        evman::push(evman::CallbackEventData{[this, index, found, artwork = std::move(artwork)]() mutable {
            if (!found || index < 0 || index >= static_cast<s64>(m_entries.size())) return;
            auto& entry = m_entries[index];
            if (entry.app.genre.empty()) entry.app.genre = std::move(artwork.genre);
            if (entry.app.description.empty()) entry.app.description = std::move(artwork.description);
            if (entry.app.icon.url.empty() && !artwork.icon.url.empty()) { entry.app.icon = std::move(artwork.icon); entry.icon.state = ImageState::None; }
            if (entry.app.banner.url.empty() && !artwork.banner.url.empty()) { entry.app.banner = std::move(artwork.banner); entry.banner.state = ImageState::None; }
            if ((entry.app.platform.empty() || entry.app.platform_learned) && !artwork.platform.empty()) {
                entry.app.platform = std::move(artwork.platform);
                entry.app.platform_learned = true;
                if (std::find(m_platform_choices.begin(), m_platform_choices.end(), entry.app.platform) == m_platform_choices.end())
                    m_platform_choices.push_back(entry.app.platform);
            }
        }, token}, false);
    });
    if (!m_artwork_workers[slot]->IsRunning()) m_artwork_workers[slot].reset();
}

auto Menu::GetImage(Entry& entry, bool banner, int& budget) -> int {
    QueuePublicArtwork(entry);
    auto& image = banner ? entry.banner : entry.icon;
    const auto handle = image.texture ? image.texture->handle : 0;
    if (image.state != ImageState::None || budget <= 0) return handle;

    const auto slot = std::find_if(m_cover_workers.begin(), m_cover_workers.end(),
        [](const auto& worker) { return !worker || worker->HasFinished(); });
    if (slot == m_cover_workers.end()) return handle;

    const auto token = m_page_stop.get_token();
    const auto index = static_cast<size_t>(&entry - m_entries.data());
    const auto& src = banner ? entry.app.banner : entry.app.icon;
    const auto& id = entry.app.artwork_id.empty() ? entry.app.app_id : entry.app.artwork_id;
    if (!image.texture) image.texture = std::make_shared<ShopCardTexture>();
    image.state = ImageState::Progress;
    --budget;
    *slot = std::make_unique<utils::Async>([this, token, index, banner, src, id, config = m_config] {
        auto pixels = LoadShopImage(config, id, src, "card", token);
        if (token.stop_requested()) return;
        evman::push(evman::CallbackEventData{[this, index, banner, url = src.url, pixels = std::move(pixels)] {
            if (index >= m_entries.size()) return;
            auto& target = m_entries[index];
            // The public provider can supply a better image while the first
            // request is running. Its newer URL owns the card from that point.
            if ((banner ? target.app.banner : target.app.icon).url != url) return;
            auto& thumbnail = banner ? target.banner : target.icon;
            if (!pixels.data.empty()) {
                const auto replacement = nvgCreateImageRGBA(App::GetVg(), pixels.w, pixels.h, 0, pixels.data.data());
                if (replacement) {
                    // A gallery viewer may still display the old handle. Give a
                    // replacement its own lifetime, while filling a pending slot in place.
                    if (thumbnail.texture->handle) thumbnail.texture = std::make_shared<ShopCardTexture>();
                    thumbnail.texture->handle = replacement;
                }
            }
            thumbnail.state = thumbnail.texture->handle ? ImageState::Done : ImageState::Failed;
        }, token}, false);
    });
    if (!(*slot)->IsRunning()) image.state = ImageState::Failed;
    return handle;
}

void Menu::DrawDiscoverButton(NVGcontext* vg, Theme*) const {
    shop::Button(vg, DISCOVER_BUTTON, m_discovering ? "Buscando tiendas en la red..." : "Buscar tiendas en la red local", m_focus_discover,
        false, shop::Icon::Search, true, 20.f);
}

void Menu::ShowServerProfiles() {
    m_did_initial_setup = true;
    m_connect_stop.request_stop();
    m_connecting = false;
    m_connect_async.reset();
    ReloadSaved();
    SetMode(Mode::ServerList);
}

void Menu::AddServer() {
    ShowServerProfiles();
    App::Push<sphaira::ownfoil::OwnfoilForm>(sphaira::ownfoil::Config{}, [this](const auto& saved) {
        ProfileSaved({}, saved);
    });
}

void Menu::SelectCategory(sphaira::ownfoil::api::Category category, bool prompt_search) {
    const auto found = std::find_if(std::begin(CATEGORIES), std::end(CATEGORIES), [category](const auto& item) { return item.category == category; });
    if (found == std::end(CATEGORIES)) return;
    const auto index = static_cast<s64>(found - std::begin(CATEGORIES));
    if (category == sphaira::ownfoil::api::Category::Search && prompt_search) { ShowSearch(index); return; }
    m_category = index;
    if (m_connecting || m_base_url.empty()) return;
    m_toolbar_index.reset();
    FreeEntries();
    m_pending_page.reset();
    SetMode(Mode::Home);
    SetCatalogTitle();
    LoadPage(0);
}

void Menu::EnterDirectory(const sphaira::ownfoil::api::ShopApp& app) {
    if (!sphaira::ownfoil::IsSameOrigin(m_base_url, app.download.url)) {
        App::Notify("La carpeta debe pertenecer al mismo servidor de la tienda.");
        return;
    }
    m_directory_stack.push_back(m_base_url);
    m_consolidated = false;
    m_category = 0;
    m_search.clear();
    m_platform.clear();
    m_folder_names.push_back(app.name);
    m_base_url = app.download.url;
    m_toolbar_index.reset();
    FreeEntries();
    m_pending_page.reset();
    LoadPage(0);
}

void Menu::RefreshCatalog() {
    if (m_connecting || m_base_url.empty()) return;
    m_refresh_requested = true;
    m_config.revalidate_artwork = true;
    // Keep the current cards visible if the refresh fails. A refresh requested
    // during another fetch is invalidated when its pending request starts.
    LoadPage(0);
}

void Menu::GoUp() {
    if (m_directory_stack.empty()) return;
    m_base_url = m_directory_stack.back();
    m_directory_stack.pop_back();
    if (!m_folder_names.empty()) m_folder_names.pop_back();
    m_toolbar_index.reset();
    FreeEntries();
    m_pending_page.reset();
    LoadPage(0);
}

void Menu::ToggleFavorite() {
    if (m_entry_index < 0 || m_entry_index >= static_cast<s64>(m_entries.size())) return;
    const auto& app = m_entries[m_entry_index].app;
    if (app.directory) return;
    const auto id = app.type == sphaira::ownfoil::api::AppType::Base ? app.app_id : app.title_id;
    if (id.empty()) return;
    const auto found = std::find(m_config.favorites.begin(), m_config.favorites.end(), id);
    const auto enabled = found == m_config.favorites.end();
    sphaira::ownfoil::SetFavorite(m_config, id, enabled);
    App::Notify(enabled ? "Añadido a favoritos" : "Quitado de favoritos");
    if (CATEGORIES[m_category].category == sphaira::ownfoil::api::Category::Favorites) LoadPage(m_page);
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();

    // this runs back from every popup the menu pushes, so the list is not
    // reloaded here: every path that writes an entry calls ReloadSaved itself.
    if (m_did_initial_setup) {
        return;
    }
    m_did_initial_setup = true;
    ReloadSaved();

    if (m_saved.empty()) {
        AddServer();
        return;
    }
    if (!GetPolledData().ip) return;

    // a single saved server needs no picking; the list stays one X press away.
    if (m_saved.size() == 1) {
        ConnectTo(m_saved[0]);
    }
}

void Menu::ReloadSaved() {
    sphaira::ownfoil::LoadConfigs(m_saved);
    RebuildCandidates();
}

void Menu::StartDiscovery() {
    if (m_discovering) {
        return;
    }

    if (!GetPolledData().ip) {
        return;
    }

    m_discovering = true;

    // owned by the menu rather than fired and forgotten: ~Async joins, and a
    // thread can't join itself.
    m_discovery = std::make_unique<utils::Async>([this](){
        // qualified from the root: this file's own namespace otherwise wins.
        auto found = sphaira::ownfoil::discovery::Discover(this->GetToken());

        evman::push(evman::CallbackEventData{
            [this, found = std::move(found)]() mutable {
                m_discovered = std::move(found);
                m_discovering = false;
                RebuildCandidates();
            },
            this->GetToken()
        }, false);
    });
}

void Menu::RebuildCandidates() {
    m_candidates.clear();

    for (const auto& config : m_saved) {
        Candidate candidate{};
        candidate.config = config;
        candidate.saved = true;

        // the case the handshake can't cover: a local-only shop whose ip moved is
        // unreachable, so no handshake ever happens to correct the entry.
        // discovery's fresher address gets the connect through, which saves it.
        if (const auto d = FindDiscovered(config.server_uid.empty() ? config.uid : config.server_uid)) {
            candidate.version = d->version;
            candidate.is_public = d->is_public;
            if (!d->local.empty()) {
                candidate.config.local_address = d->local;
            }
            if (!d->remote.empty()) {
                candidate.config.remote_address = d->remote;
            }
        }

        m_candidates.emplace_back(std::move(candidate));
    }

    for (const auto& d : m_discovered) {
        // matched on the server-issued uid alone, so a hand-added entry still
        // holding a placeholder is listed a second time until its first connect.
        if (sphaira::ownfoil::FindByUid(m_saved, d.uid)) {
            continue;
        }

        Candidate candidate{};
        candidate.config.server_uid = d.uid;
        candidate.config.name = d.name;
        candidate.config.local_address = d.local;
        candidate.config.remote_address = d.remote;
        candidate.version = d.version;
        candidate.is_public = d.is_public;
        m_candidates.emplace_back(std::move(candidate));
    }

    if (m_index >= static_cast<s64>(m_candidates.size())) {
        m_index = 0;
    }

    // an empty list has nothing to focus, so the button is all there is.
    if (m_candidates.empty()) {
        m_focus_discover = true;
    }
}

auto Menu::FindDiscovered(const std::string& uid) const -> const sphaira::ownfoil::discovery::DiscoveredServer* {
    if (uid.empty()) {
        return nullptr;
    }

    const auto it = std::find_if(m_discovered.begin(), m_discovered.end(), [&](const auto& d) {
        return d.uid == uid;
    });
    return it == m_discovered.end() ? nullptr : &*it;
}

void Menu::OnServerSelected(s64 index) {
    if (index < 0 || index >= static_cast<s64>(m_candidates.size())) {
        return;
    }

    const auto candidate = m_candidates[index];
    const auto config = candidate.config;

    // already a saved location: connect straight away.
    if (candidate.saved) {
        ConnectTo(config);
        return;
    }

    // a discovered server not saved yet: say whether it needs a login before
    // asking whether to set one up.
    const auto message = candidate.is_public
        ? "This shop is public. Set up a login anyway?"_i18n
        : "This shop is private. Set up a login now?"_i18n;

    App::Push<OptionBox>(message, "No"_i18n, "Yes"_i18n, 1, [this, config](std::optional<s64> op_index) {
        if (op_index && *op_index) {
            App::Push<sphaira::ownfoil::OwnfoilForm>(config, [this](const sphaira::ownfoil::Config& saved) {
                ReloadSaved();
                ConnectTo(saved);
            });
        } else {
            // SaveConfig assigns the uid, so it needs a mutable copy.
            auto to_save = config;
            sphaira::ownfoil::SaveConfig(to_save);
            ReloadSaved();
            ConnectTo(to_save);
        }
    });
}

void Menu::ConnectTo(const sphaira::ownfoil::Config& config) {
    if (m_connecting) {
        return;
    }

    // opening a shop asks what it can add to this console, so that is what it
    // opens on rather than wherever the last one was left.
    m_category = 0; // All games: a former empty Search must not hide a new shop.
    m_search.clear();
    m_platform = "Nintendo Switch";
    m_consolidated = true;
    m_installs_shown = g_installs;

    // read here rather than on the worker: option::Get() fills its cache on the
    // first call, which has no business happening off the main thread.
    const auto query = BuildQuery(0);

    // B cancels this connect alone, and its result is dropped with it. a
    // cancelled worker may still be unwinding when the next connect replaces it,
    // which joins it, but curl notices a stop within a progress tick.
    m_connect_stop = {};
    const auto token = m_connect_stop.get_token();

    m_connecting = true;
    SetShopConnection(false, true);
    m_connecting_name = config.name;

    // owned by the menu rather than fired and forgotten: ~Async joins, and a
    // thread can't join itself.
    m_connect_async = std::make_unique<utils::Async>([this, config, query, token](){
        auto connect_result = sphaira::ownfoil::api::Connect(config, token);
        auto connected_config = config;
        connected_config.json_index = connect_result.json_index;
        connected_config.legacy_html = connect_result.legacy_html;
        connected_config.legacy_headers = connect_result.legacy_headers;
        bool saved_changed{};
        std::vector<sphaira::ownfoil::api::ShopApp> fetched;
        std::string fetch_error;
        s64 total{};
        std::string base_url;

        if (connect_result.success) {
            // the server owns its identity and its name; empty values are
            // ignored, so a server reporting none can't erase what discovery
            // gave.
            const auto& info = connect_result.info;
            if (!info.uid.empty()) {
                connected_config.server_uid = info.uid;
            }
            if (connected_config.name.empty() && !info.name.empty()) {
                connected_config.name = info.name;
            }
            // no local address: that is discovery's alone (RebuildCandidates).
            if (!info.remote_address.empty()) {
                connected_config.remote_address = info.remote_address;
            }
            // so the next launch goes straight to the address that just worked.
            connected_config.resolved_url = connect_result.base_url;

            // written here, on the worker: minIni rewrites the whole file once
            // per key, so saving an entry is half a dozen SD writes and they
            // have no business landing on the main thread as the grid appears.
            if (connected_config.server_uid != config.server_uid || connected_config.name != config.name
                || connected_config.remote_address != config.remote_address
                || connected_config.resolved_url != config.resolved_url) {
                // the old uid re-keys the entry: a placeholder promoted on first
                // contact, or an entry re-pointed at a different server.
                sphaira::ownfoil::SaveConfig(connected_config, config.uid);
                saved_changed = true;
            }

            // the first page rides the same worker, so the grid appears full.
            if (info.features.shop) {
                base_url = connect_result.base_url;
                // made here rather than in the constructor, which runs at startup
                // for every user whether or not they ever open this tab.
                fs::FsNativeSd().CreateDirectoryRecursively(CACHE_PATH);
                auto page_query = query;
                if (PrepareQuery(page_query, base_url, connected_config, token, fetch_error)) {
                    sphaira::ownfoil::api::FetchApps(base_url, connected_config, token, page_query, fetched, total, fetch_error);
                }
            }
        }

        evman::push(evman::CallbackEventData{
            [this, config, connect_result = std::move(connect_result), connected_config = std::move(connected_config),
                saved_changed, fetched = std::move(fetched), fetch_error = std::move(fetch_error), total, base_url = std::move(base_url)]() mutable {
                m_connecting = false;
                SetShopConnection(false, false);

                if (!connect_result.success) {
                    App::Notify(config.name + ": " + connect_result.error);
                    App::Push<OptionBox>(connect_result.error + "\nPuedes corregir la URL o el acceso de esta tienda.", "Volver", "Editar tienda", 1, [this, config](auto selected) {
                        if (selected && *selected) EditProfile(config);
                    });
                    return;
                }

                const auto& info = connect_result.info;
                if (!info.motd.empty()) {
                    App::Notify(info.name + ": " + info.motd);
                }
                if (!connected_config.direct_catalog.empty()) {
                    App::Notify(info.name + ": catálogo de la microSD");
                } else {
                    App::Notify(info.name + (connect_result.used_remote ? ": connected via remote address"_i18n : ": connected via local network"_i18n));
                }

                // the entry on disk moved under the list, so pick the change up.
                if (saved_changed) {
                    ReloadSaved();
                }

                // the handshake says what this caller may actually do; without
                // shop access there is no catalog to show.
                if (!info.features.shop) {
                    App::Notify(info.name + ": " + "This account has no shop access"_i18n);
                    return;
                }

                if (!fetch_error.empty()) {
                    App::Notify(info.name + ": " + fetch_error);
                    return;
                }

                // Keep the previous working URL/account paired on a failed
                // connection or catalog read, so B can return to that shop.
                m_config = connected_config;
                m_base_url = base_url;
                m_directory_stack.clear();
                m_folder_names.clear();
                m_platform_choices = {"Nintendo Switch"};
                m_sidebar_index = 0;
                m_sidebar_offset = 0;
                SetMode(Mode::Home);
                ApplyPage(0, fetched, total);
            },
            token
        }, false);
    });
}

auto Menu::GetPageMax() -> s64 {
    const auto size = m_page_size.Get();
    if (m_total <= 0 || size <= 0) {
        return 1;
    }
    return (m_total + size - 1) / size;
}

auto Menu::BuildQuery(s64 page) -> sphaira::ownfoil::api::CatalogQuery {
    auto sort = m_sort.Get();
    if (sort < 0 || sort >= static_cast<s64>(std::size(SORT_MODES))) {
        sort = 0;
    }

    sphaira::ownfoil::api::CatalogQuery query{};
    // the shop pages from 1; m_page counts from 0 like every index here.
    query.page = page + 1;
    query.page_size = m_page_size.Get();
    query.order_field = SORT_MODES[sort].field;
    query.descending = m_sort_desc.Get();
    // the id lists this leaves empty are the worker's to fill, since the console
    // has to be scanned for them.
    query.category = CATEGORIES[m_category].category;
    query.search = m_search;
    query.platform = m_platform;
    query.consolidated = m_consolidated;
    return query;
}

auto Menu::PrepareQuery(sphaira::ownfoil::api::CatalogQuery& query, const std::string& base_url, const sphaira::ownfoil::Config& config, std::stop_token token, std::string& error) -> bool {
    using Category = sphaira::ownfoil::api::Category;

    // neither asks anything about this console, so the shop answers both alone.
    if (query.category == Category::Favorites) {
        query.app_ids = config.favorites;
        return true;
    }
    if (query.category == Category::All || query.category == Category::Search) {
        return true;
    }

    std::scoped_lock lock{m_installed_mutex};

    // the scan describes the console, so it outlives any one page: done once, and
    // again only when a category needs more of it, or an install changed it.
    // walking each title's content costs an ns query per title.
    const auto content = query.category != Category::NewGames;
    const auto installs = g_installs.load();
    const auto rescan = installs != m_installs_scanned;
    if (!m_installed_scanned || rescan || (content && !m_installed_content)) {
        m_installed = sphaira::ownfoil::installed::Scan(content);
        m_installed_scanned = true;
        m_installed_content = content;
        m_installs_scanned = installs;
    }

    switch (query.category) {
        case Category::All:
        case Category::Favorites:
        case Category::Search:
            break;

        case Category::NewGames:
            query.app_ids = sphaira::ownfoil::installed::TitleIds(m_installed);
            break;

        case Category::Dlc:
            query.title_ids = sphaira::ownfoil::installed::TitleIds(m_installed);
            query.app_ids = m_installed.dlc_ids;
            break;

        case Category::Updates: {
            // a round trip whose answer is the same for every page, so it is
            // asked on page 1 - where the category is entered and a changed sort
            // lands - and after an install.
            if (query.page <= 1 || rescan) {
                m_outdated.clear();

                std::vector<sphaira::ownfoil::api::ShopUpdate> updates;
                if (!sphaira::ownfoil::api::FetchUpdates(base_url, config, token, sphaira::ownfoil::installed::TitleIds(m_installed), updates, error)) {
                    return false;
                }

                for (const auto& update : updates) {
                    if (update.version > sphaira::ownfoil::installed::UpdateVersionOf(m_installed, update.title_id)) {
                        m_outdated.emplace_back(update.app_id);
                    }
                }
            }

            query.app_ids = m_outdated;
        }   break;
    }

    return true;
}

void Menu::LoadPage(s64 page, s64 focus) {
    // the page on screen stays up until the next lands, with no modal: a fetch is
    // a few tens of milliseconds, and a dialog that appears and vanishes inside
    // that reads as slower than none. a request arriving meanwhile is held rather
    // than dropped, since `Order` can be flipped faster than the shop answers.
    if (m_loading || (m_loader && !m_loader->HasFinished())) {
        m_pending_page = shop::PageTarget{page, focus};
        return;
    }

    if (m_refresh_requested) {
        sphaira::ownfoil::api::InvalidateCatalog(m_base_url, m_config);
        m_refresh_requested = false;
    }
    // option::Get() fills its cache on first call, so the query is built here
    // rather than on the worker.
    const auto query = BuildQuery(page);
    const auto config = m_config;
    const auto base_url = m_base_url;
    // the page on screen owns the fetch that would replace it, so leaving it
    // abandons the request.
    const auto token = m_page_stop.get_token();

    m_loading = true;
    m_fetch_error.clear();

    // owned by the menu rather than fired and forgotten: ~Async joins, and a
    // thread can't join itself.
    m_loader = std::make_unique<utils::Async>([this, config, base_url, query, page, focus, token](){
        // the result travels with the event rather than through a member: no
        // dialog is holding the main thread off one this time.
        std::vector<sphaira::ownfoil::api::ShopApp> apps;
        s64 total{};
        std::string error;
        auto page_query = query;
        if (PrepareQuery(page_query, base_url, config, token, error)) {
            sphaira::ownfoil::api::FetchApps(base_url, config, token, page_query, apps, total, error);
        }

        evman::push(evman::CallbackEventData{
            [this, page, focus, total, apps = std::move(apps), error = std::move(error)]() mutable {
                if (!error.empty()) {
                    m_loading = false;
                    m_fetch_error = error;
                    App::Notify(m_config.name + ": " + error);
                } else {
                    // ApplyPage clears the flag by way of FreeEntries.
                    ApplyPage(page, apps, total, focus);
                }

                // whatever was asked for mid-flight, issued now against the
                // options as they stand.
                if (m_pending_page) {
                    const auto next = *m_pending_page;
                    m_pending_page.reset();
                    LoadPage(next.page, next.focus);
                }
            },
            token
        }, false);
    });
    if (!m_loader->IsRunning()) {
        m_loading = false;
        m_fetch_error = "No se pudo iniciar la lectura del catálogo.";
    }
}

void Menu::ApplyPage(s64 page, std::vector<sphaira::ownfoil::api::ShopApp>& apps, s64 total, s64 focus) {
    m_page = page;
    m_total = total;
    m_fetch_error.clear();

    FreeEntries();
    m_entries.reserve(apps.size());
    for (auto& app : apps) {
        Entry entry{};
        entry.app = std::move(app);
        m_entries.emplace_back(std::move(entry));
    }
    apps.clear();

    UpdateCatalogSummary();
    m_entry_index = std::clamp<s64>(focus, 0, std::max<s64>(0, static_cast<s64>(m_entries.size()) - 1));
    ScrollToSelection();
    m_banner_name.Reset();
    m_banner_publisher.Reset();

    char subheading[64];
    std::snprintf(subheading, sizeof(subheading), "Page %ld / %ld"_i18n.c_str(),
        m_page + 1, GetPageMax());
    SetSubHeading(subheading);

    // the count belongs beside what it counts, in the title row.
    SetCatalogTitle();
}

void Menu::SetCatalogTitle() {
    // what is shown and how much of it, beside the shop's name. what orders it is
    // left to the sidebar: the row is bounded by the status text to its right,
    // and a third part is what tips it into scrolling.
    auto title = i18n::get(CATEGORIES[m_category].name);

    // "Search" alone says nothing about what is on screen, so the term shares the
    // same slot rather than taking another.
    if (CATEGORIES[m_category].category == sphaira::ownfoil::api::Category::Search && !m_search.empty()) {
        title += ": " + m_search;
    }

    // until a page answers, "0 titles" would read as a claim about the shop
    // rather than about the wait.
    if (m_total > 0) {
        char total[64];
        std::snprintf(total, sizeof(total), "%ld titles"_i18n.c_str(), m_total);
        title += SEPARATOR;
        title += total;
    }

    if (CATEGORIES[m_category].category == sphaira::ownfoil::api::Category::Search)
        title += SEPARATOR + SearchScope();
    else if (!m_folder_names.empty()) title += SEPARATOR + m_folder_names.back();
    else if (!m_platform.empty()) title += SEPARATOR + (m_platform == "__unknown__" ? std::string{"Sin clasificar"} : m_platform);
    SetTitleSubHeading(title);
}

void Menu::ShowCategories() {
    // a panel down the left rather than an options entry: this is the one choice
    // that says what the whole screen is about, and is made often enough for a
    // button of its own.
    auto panel = std::make_unique<Sidebar>("Consolas y carpetas", i18n::get(CATEGORIES[m_category].name), Sidebar::Side::LEFT);
    ON_SCOPE_EXIT(App::Push(std::move(panel)));

    panel->Add<SidebarEntryCallback>("Actualizar catálogo", [this] { RefreshCatalog(); }, true);
    if (m_config.json_index && m_config.direct_catalog.empty()) panel->Add<SidebarEntryCallback>("Ver carpetas del servidor", [this] { ShowServerFolders(); }, true,
        "Explora también las carpetas que aún no tienen un juego identificado.");
    if (!m_directory_stack.empty()) panel->Add<SidebarEntryCallback>("Volver a la carpeta anterior", [this] { GoUp(); }, true);
    for (const auto& entry : m_entries) {
        if (entry.app.directory) panel->Add<SidebarEntryCallback>(entry.app.name, [this, app = entry.app] { EnterDirectory(app); }, true);
    }
    const auto& platforms = PlatformChoices();
    if (!platforms.empty()) {
        panel->Add<SidebarEntryTextBase>("Consolas detectadas", "Carpeta y metadatos consultados", SidebarEntryTextBase::Callback{});
        panel->Add<SidebarEntryCallback>("Todas las consolas", [this] {
            SelectPlatform("");
        }, true);
        for (const auto& platform : platforms) {
            panel->Add<SidebarEntryCallback>(platform == "__unknown__" ? "Sin clasificar" : platform, [this, platform] { SelectPlatform(platform); }, true);
        }
    }
    for (s64 i = 0; i < static_cast<s64>(std::size(CATEGORIES)); i++) {
        const auto& mode = CATEGORIES[i];
        panel->Add<SidebarEntryCallback>(i18n::get(mode.name), [this, i](){
            // Search says nothing on its own, so picking it always asks for a
            // term - re-picking it is how a different search is started.
            if (CATEGORIES[i].category == sphaira::ownfoil::api::Category::Search) {
                ShowSearch(i);
                return;
            }

            if (i == m_category) {
                return;
            }

            SelectCategory(CATEGORIES[i].category, false);
        }, true);
    }
}

auto Menu::SearchScope() const -> std::string {
    if (!m_consolidated) return m_folder_names.empty() ? "esta carpeta" : m_folder_names.back();
    if (m_platform.empty()) return "toda la biblioteca";
    return m_platform == "__unknown__" ? "Sin clasificar" : m_platform;
}

void Menu::ShowSearch(s64 index) {
    std::string term;

    // the term on screen is the initial text: narrowing a search that came back
    // too wide is far more common than starting an unrelated one.
    const auto scope_hint = "Nombre o ID · " + SearchScope();
    if (R_FAILED(swkbd::ShowText(term, "Buscar juego", scope_hint.c_str(), m_search.c_str(), 1, SEARCH_MAX)) || term.empty()) {
        // backing out of the keyboard leaves the catalog that was up, up.
        return;
    }

    // the same term against the same catalog would come back the same.
    if (index == m_category && term == m_search) {
        return;
    }

    m_search = term;
    m_category = index;
    m_sidebar_focus = m_detail_focus = false;
    m_toolbar_index.reset();
    if (!m_base_url.empty() && !m_connecting) {
        FreeEntries();
        m_pending_page.reset();
        SetMode(Mode::Home);
        SetCatalogTitle();
        LoadPage(0);
    }
}

void Menu::ShowOptions() {
    auto options = std::make_unique<Sidebar>("Options"_i18n, Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));
    if (m_check_update) options->Add<SidebarEntryCallback>("Buscar actualización de la app", m_check_update, true);
    if (m_mode == Mode::Home && !m_config.uid.empty()) {
        options->Add<SidebarEntryCallback>("Editar tienda actual", [this] { EditCurrent(); }, true,
            "Cambia la URL, el puerto, la ruta o el acceso de esta tienda y vuelve a conectar.");
    }

    // the queue is reachable from either list, it outlives both.
    {
        const auto& queue = InstallQueue::Get();
        auto title = "Install queue"_i18n;
        if (const auto pending = queue.GetPendingCount(); queue.IsBusy()) {
            title += " (" + std::to_string(pending + 1) + ")";
        }
        options->Add<SidebarEntryCallback>(title, [](){
            InstallQueue::Get().ShowMenu();
        }, false, "Installs running in the background and waiting their turn."_i18n);
    }

    if (m_mode == Mode::ServerList) {
        options->Add<SidebarEntryCallback>("Añadir tienda", [this](){
            AddServer();
        }, true, "Add a server."_i18n);

        if (!m_focus_discover && m_index >= 0 && m_index < static_cast<s64>(m_candidates.size()) && m_candidates[m_index].saved) {
            options->Add<SidebarEntryCallback>("Editar tienda", [this](){
                EditSelected();
            }, true, "Change the selected server's name, addresses or login."_i18n);
        }

        if (!m_focus_discover && m_index < static_cast<s64>(m_candidates.size()) && m_candidates[m_index].saved) {
            options->Add<SidebarEntryCallback>("Delete server"_i18n, [this](){
                DeleteSelected();
            }, true, "Forget the selected server."_i18n);
        }
    } else {
        options->Add<SidebarEntryCallback>("Actualizar catálogo", [this] { RefreshCatalog(); }, true);
        options->Add<SidebarEntryCallback>("Buscar juego", [this] { SelectCategory(sphaira::ownfoil::api::Category::Search); }, true);
        if (!m_search.empty()) options->Add<SidebarEntryCallback>("Quitar búsqueda", [this] {
            m_search.clear(); SelectCategory(sphaira::ownfoil::api::Category::All, false);
        }, true);
        if (m_entry_index >= 0 && m_entry_index < static_cast<s64>(m_entries.size()) && !m_entries[m_entry_index].app.directory) {
            const auto& selected = m_entries[m_entry_index].app;
            const auto& id = selected.type == sphaira::ownfoil::api::AppType::Base ? selected.app_id : selected.title_id;
            const auto favorite = std::find(m_config.favorites.begin(), m_config.favorites.end(), id) != m_config.favorites.end();
            options->Add<SidebarEntryCallback>(favorite ? "Quitar de favoritos" : "Añadir a favoritos", [this] { ToggleFavorite(); }, true);
        }
        options->Add<SidebarEntryCallback>("Consolas y carpetas", [this] { ShowCategories(); }, true);
        SidebarEntryArray::Items page_items;
        s64 page_index{};
        for (size_t index = 0; index < std::size(PAGE_SIZES); ++index) {
            page_items.emplace_back(std::to_string(PAGE_SIZES[index]));
            if (PAGE_SIZES[index] == m_page_size.Get()) page_index = index;
        }
        options->Add<SidebarEntryArray>("Juegos por página", page_items, [this](s64& index) {
            if (PAGE_SIZES[index] != m_page_size.Get()) { m_page_size.Set(PAGE_SIZES[index]); LoadPage(0); }
        }, page_index, "Desplázate para ver más juegos o cambia de página con ZL / ZR.");
        SidebarEntryArray::Items sort_items;
        for (const auto& mode : SORT_MODES) {
            sort_items.emplace_back(i18n::get(mode.name));
        }

        // the shop does the sorting, and a different ordering is a different
        // catalog, so either of these refetches from page 1.
        options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this](s64& index_out){
            if (index_out == m_sort.Get()) {
                return;
            }
            m_sort.Set(index_out);
            LoadPage(0);
        }, m_sort.Get());

        // two states, so A flips it in place rather than opening a picker. stored
        // here, not through the OptionBool overload, which runs its callback
        // before the store and would refetch the old order.
        options->Add<SidebarEntryBool>("Order"_i18n, m_sort_desc.Get(), [this](bool& descending){
            m_sort_desc.Set(descending);
            LoadPage(0);
        }, "", "Descending"_i18n, "Ascending"_i18n);

        // last: it leaves the catalog, where everything above adjusts it.
        options->Add<SidebarEntryCallback>("Servers list"_i18n, [this](){
            ShowServerProfiles();
        }, true, "Return to the server list."_i18n);
    }

    // on both lists: every shop's artwork shares the one cache.
    options->Add<SidebarEntryCallback>("Clear cache"_i18n, [](){
        App::Push<OptionBox>(
            "Remove all cached artwork from the SD card?"_i18n,
            "No"_i18n, "Yes"_i18n, 0, [](std::optional<s64> op_index) {
                if (!op_index || !*op_index) {
                    return;
                }

                // one delete however much is cached, but still an sd write not to
                // wait on from the main thread.
                App::Push<ProgressBox>(0, "Clear cache"_i18n, "Ownfoil"_i18n, [](ProgressBox*) -> Result {
                    const auto rc = fs::FsNativeSd().DeleteDirectoryRecursively(CACHE_PATH);
                    // nothing cached yet is already clear.
                    R_UNLESS(R_SUCCEEDED(rc) || rc == FsError_PathNotFound, rc);
                    R_SUCCEED();
                }, [](Result rc) {
                    if (R_SUCCEEDED(App::PushErrorBox(rc, "Failed to clear the cache."_i18n))) {
                        App::Notify("Cache cleared."_i18n);
                    }
                });
            }
        );
    }, true, "Remove all cached shop artwork from the SD card."_i18n);
}

void Menu::OpenSelected() {
    if (m_entry_index < 0 || m_entry_index >= static_cast<s64>(m_entries.size())) {
        return;
    }

    const auto& app = m_entries[m_entry_index].app;
    if (app.directory) { EnterDirectory(app); return; }

    UpdatePreview(true);
    if (m_preview) m_preview->OpenFullPage();
}

void Menu::EditSelected() {
    // the discover button has focus, or there is nothing saved to edit yet.
    if (m_focus_discover || m_index < 0 || m_index >= static_cast<s64>(m_candidates.size()) || !m_candidates[m_index].saved) {
        return;
    }

    // a discovered row that was never saved is edited as what it will become: the
    // form writes the entry, addresses and all, on its first save.
    EditProfile(m_candidates[m_index].config);
}

void Menu::EditCurrent() { if (!m_config.uid.empty()) EditProfile(m_config); }
void Menu::EditProfile(const sphaira::ownfoil::Config& config) {
    const auto previous = config;
    ShowServerProfiles();
    App::Push<sphaira::ownfoil::OwnfoilForm>(previous, [this, previous](const auto& saved) { ProfileSaved(previous, saved); });
}
void Menu::ProfileSaved(const sphaira::ownfoil::Config& previous, const sphaira::ownfoil::Config& saved) {
    if (!m_base_url.empty()) sphaira::ownfoil::api::InvalidateCatalog(m_base_url, previous);
    if (!previous.resolved_url.empty()) sphaira::ownfoil::api::InvalidateCatalog(previous.resolved_url, previous);
    m_config = saved;
    m_base_url.clear(); m_directory_stack.clear(); m_folder_names.clear();
    m_search.clear(); m_platform.clear(); m_pending_page.reset();
    ReloadSaved();
    for (size_t index = 0; index < m_candidates.size(); ++index) if (m_candidates[index].config.uid == saved.uid) {
        m_index = index; m_focus_discover = false; break;
    }
    SetMode(Mode::ServerList);
    ConnectTo(saved);
}
void Menu::UpdateProfileActions() {
    if (m_mode != Mode::ServerList) return;
    if (!m_focus_discover && m_index >= 0 && m_index < static_cast<s64>(m_candidates.size()) && m_candidates[m_index].saved)
        SetAction(Button::Y, Action{"Editar tienda", [this] { EditSelected(); }});
    else RemoveAction(Button::Y);
}

void Menu::DeleteSelected() {
    if (m_focus_discover || m_index >= static_cast<s64>(m_candidates.size())) {
        return;
    }

    const auto& candidate = m_candidates[m_index];
    if (!candidate.saved) {
        return;
    }

    const auto uid = candidate.config.uid;
    App::Push<OptionBox>(
        i18n::Reorder("Delete ", candidate.config.name) + '?',
        "No"_i18n, "Yes"_i18n, 0, [this, uid](std::optional<s64> op_index) {
            if (op_index && *op_index) {
                sphaira::ownfoil::DeleteConfig(uid);
                ReloadSaved();
            }
        }
    );
}

void SignalInstalled() {
    g_installs++;
}

} // namespace sphaira::ui::menu::ownfoil
