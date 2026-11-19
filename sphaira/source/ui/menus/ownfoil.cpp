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

Menu::LazyImage::LazyImage(LazyImage&& rhs) noexcept {
    *this = std::move(rhs);
}

auto Menu::LazyImage::operator=(LazyImage&& rhs) noexcept -> LazyImage& {
    std::swap(image, rhs.image);
    std::swap(tried_cache, rhs.tried_cache);
    std::swap(cached, rhs.cached);
    std::swap(state, rhs.state);
    return *this;
}

Menu::LazyImage::~LazyImage() {
    if (image) {
        nvgDeleteImage(App::GetVg(), image);
    }
}

Menu::Menu(u32 flags) : grid::Menu{"NinMagic Shop", flags} {
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

    if (m_view.Get() < 0 || m_view.Get() >= ViewType_Count) {
        m_view.Set(ViewType_Icon);
    }

    // a hand-edited ini must not leave the sidebar pointing past the list, nor
    // BuildQuery indexing past SORT_MODES.
    if (m_sort.Get() < 0 || m_sort.Get() >= static_cast<s64>(std::size(SORT_MODES))) {
        m_sort.Set(0);
    }

    SetMode(Mode::ServerList);
    const auto saved_page_size = m_page_size.Get();
    const auto page_size = std::lower_bound(std::begin(PAGE_SIZES), std::end(PAGE_SIZES), saved_page_size);
    m_page_size.Set(saved_page_size <= 0 ? 30 : page_size == std::end(PAGE_SIZES) ? PAGE_SIZES[std::size(PAGE_SIZES) - 1] : *page_size);
    m_view.Set(ViewType_Icon);
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
    for (auto& worker : m_artwork_workers) worker.reset();
    m_page_stop = std::stop_source{};
    m_loading = false;
    m_entries.clear();
    m_entry_index = 0;
    m_preview.reset();
    m_preview_id.clear();
    m_preview_changed_tick = armGetSystemTick();
    m_detail_focus = false;
}

void Menu::CycleView() {
    m_view.Set((m_view.Get() + 1) % ViewType_Count);
    OnViewChange();
}

auto Menu::PlatformChoices() const -> std::vector<std::string> {
    const auto available = sphaira::ownfoil::api::CachedPlatforms(m_base_url, m_config);
    std::vector<std::string> choices{"Nintendo Switch"};
    for (const auto& label : available) if (!label.empty() && std::find(choices.begin(), choices.end(), label) == choices.end()) choices.push_back(label);
    for (const auto& entry : m_entries) {
        const auto& label = entry.app.platform;
        if (!entry.app.directory && !label.empty() && std::find(choices.begin(), choices.end(), label) == choices.end()) choices.push_back(label);
    }
    return choices;
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
    FreeEntries(); m_pending_page.reset(); LoadPage(0);
}
void Menu::ShowServerFolders() {
    if (!m_config.json_index || m_base_url.empty() || m_connecting) return;
    if (!m_directory_stack.empty()) m_base_url = m_directory_stack.front();
    m_directory_stack.clear(); m_folder_names.clear();
    m_platform.clear(); m_search.clear(); m_category = 0;
    m_consolidated = false; m_sidebar_focus = false; m_detail_focus = false;
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
    const auto choices = PlatformChoices();
    if (m_sidebar_index < choices.size()) {
        SelectPlatform(choices[m_sidebar_index]);
    } else if (m_sidebar_index == choices.size()) InstallQueue::Get().ShowMenu();
    else ShowOptions();
}
void Menu::UpdatePreview() {
    if (m_mode != Mode::Home || m_entry_index < 0 || m_entry_index >= static_cast<s64>(m_entries.size())) return;
    const auto& selected = m_entries[m_entry_index].app;
    const auto identity = m_base_url + "|" + selected.app_id + "|" + selected.download.url + "|" + selected.name;
    if (identity != m_preview_id) {
        m_preview.reset(); m_preview_id = identity; m_preview_changed_tick = armGetSystemTick();
    }
    if (selected.directory) return;
    if (!m_preview && armTicksToNs(armGetSystemTick() - m_preview_changed_tick) >= 200000000ULL)
        m_preview = std::make_shared<TitleMenu>(m_config, m_base_url, MakeTitlePage(selected), TitleMenu::Mode::Preview);
    if (m_preview) m_preview->PollPreview();
}

void Menu::OnViewChange() {
    m_entry_index = 0;
    m_banner_name.Reset(); m_banner_publisher.Reset();
    m_list = std::make_unique<List>(shop::layout::Columns, shop::layout::VisibleCards, GRID_POS, GRID_ITEM, GRID_PAD);
    m_list->SetPageJump(false);
    m_list->SetScrollBarPos(shop::layout::GridScrollX, shop::layout::GridScrollY, shop::layout::GridScrollHeight);
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    if (m_mode == Mode::ServerList && touch->is_clicked) {
        if (touch->in_range(Vec4{49.f, 176.f, 214.f, 42.f})) { AddServer(); return; }
        if (touch->in_range(Vec4{49.f, 228.f, 214.f, 42.f})) { EditSelected(); return; }
    }
    if (m_mode == Mode::Home && m_installs_shown != g_installs) {
        m_installs_shown = g_installs;
        const auto category = CATEGORIES[m_category].category;
        if (category != sphaira::ownfoil::api::Category::All && category != sphaira::ownfoil::api::Category::Search) LoadPage(m_page);
    }
    if (m_mode == Mode::Home) {
        const std::string primary_hint = m_sidebar_focus || m_detail_focus ? "Seleccionar" : "Ver juego";
        if (m_actions.at(Button::A).m_hint != primary_hint) { auto action = m_actions.at(Button::A); action.m_hint = primary_hint; SetAction(Button::A, action); }
        // The shell owns + and the header. Search/queue stay directly available
        // while sidebar and detail navigation handle only their own controls.
        if (!HasAction(Button::Y) || m_actions.at(Button::Y).m_hint != "Cola")
            SetAction(Button::Y, Action{"Cola", [] { InstallQueue::Get().ShowMenu(); }});
        if (touch->is_clicked) {
            if (m_preview && m_preview->HandleDetailTouch(touch)) { m_detail_focus = true; m_sidebar_focus = false; return; }
            if (touch->in_range(SEARCH_BOX)) { ShowSearch(std::size(CATEGORIES) - 1); return; }
            if (touch->in_range(FOLDERS_BUTTON)) { ShowCategories(); return; }
            const auto choices = PlatformChoices();
            for (size_t row = 0; row < shop::layout::VisiblePlatforms && m_sidebar_offset + row < choices.size(); ++row) if (touch->in_range(ToVec4(shop::layout::PlatformRow(row)))) {
                m_sidebar_index = m_sidebar_offset + row; m_sidebar_focus = true; m_detail_focus = false; ActivateSidebar(); return;
            }
            if (touch->in_range(ToVec4(shop::layout::Downloads))) { InstallQueue::Get().ShowMenu(); return; }
            if (touch->in_range(ToVec4(shop::layout::Settings))) { ShowOptions(); return; }
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
                return;
            }
        }
        if (!m_sidebar_focus && !m_detail_focus && !m_entries.empty()) {
            if (controller->GotDown(Button::LEFT) && m_entry_index % shop::layout::Columns == 0) { m_sidebar_focus = true; }
            else if (controller->GotDown(Button::RIGHT) && (m_entry_index % shop::layout::Columns == shop::layout::Columns - 1 || m_entry_index + 1 == static_cast<s64>(m_entries.size())) && m_preview) m_detail_focus = true;
        }
    } else UpdateProfileActions();
    MenuBase::Update(controller, touch);
    if (m_mode == Mode::Home) {
        if (m_sidebar_focus) {
            const auto count = PlatformChoices().size() + 2;
            if (controller->GotDown(Button::UP)) m_sidebar_index = (m_sidebar_index + count - 1) % count;
            if (controller->GotDown(Button::DOWN)) m_sidebar_index = (m_sidebar_index + 1) % count;
            if (controller->GotDown(Button::RIGHT)) m_sidebar_focus = false;
        } else if (m_detail_focus && m_preview) {
            Controller navigation = *controller;
            navigation.Set(Button::A, false); navigation.Set(Button::B, false);
            m_detail_focus = m_preview->HandleDetailController(&navigation);
        } else if (!m_entries.empty()) {
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
    state.platforms = PlatformChoices();
    if (m_sidebar_index < state.platforms.size()) {
        if (m_sidebar_index < m_sidebar_offset) m_sidebar_offset = m_sidebar_index;
        if (m_sidebar_index >= m_sidebar_offset + shop::layout::VisiblePlatforms) m_sidebar_offset = m_sidebar_index - shop::layout::VisiblePlatforms + 1;
    }
    m_sidebar_offset = std::min(m_sidebar_offset, state.platforms.size() > shop::layout::VisiblePlatforms ? state.platforms.size() - shop::layout::VisiblePlatforms : 0);
    state.sidebar_offset = m_sidebar_offset; state.sidebar_index = m_sidebar_index;
    state.sidebar_focused = m_sidebar_focus; state.detail_focused = m_detail_focus;
    state.platform = m_platform;
    state.heading = !m_consolidated ? "Carpetas del servidor" : m_platform.empty() ? "Biblioteca" : m_platform == "__unknown__" ? "Sin clasificar" : m_platform;
    state.location = "Biblioteca";
    if (!m_folder_names.empty()) state.location += "  /  " + m_folder_names.back();
    else if (!m_config.name.empty()) state.location += "  /  " + m_config.name;
    state.search = m_search;
    state.storage = m_sd_free >= 0 ? "microSD · " + utils::formatSizeStorage(m_sd_free) + " libres" : "microSD";
    state.page = m_fetch_error.empty() ? "ZL / ZR · Página " + std::to_string(m_page + 1) + " / " + std::to_string(GetPageMax()) : "No se pudo cargar · − Opciones para reintentar";
    const auto catalog_status = sphaira::ownfoil::api::CachedCatalogStatus(m_base_url, m_config);
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
    if (m_preview) {
        int selected_image{};
        for (const auto& card : state.cards) if (card.selected) { selected_image = card.image; break; }
        m_preview->SetDetailFocused(m_detail_focus);
        m_preview->DrawDetailPanel(vg, theme, DETAIL_PANEL, selected_image);
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
    while (slot < m_artwork_workers.size() && m_artwork_workers[slot]) ++slot;
    if (slot == m_artwork_workers.size()) return;
    entry.artwork_tried = true;
    const auto index = &entry - m_entries.data();
    const auto id = entry.app.artwork_id;
    const auto provider = m_config.artwork_search_url;
    const auto token = m_page_stop.get_token();
    m_artwork_workers[slot] = std::make_unique<utils::Async>([this, index, id, provider, token, slot] {
        sphaira::ownfoil::api::ShopApp artwork;
        const auto found = sphaira::ownfoil::api::LookupPublicArtwork(provider, id, token, artwork);
        evman::push(evman::CallbackEventData{[this, index, slot, found, artwork = std::move(artwork)]() mutable {
            m_artwork_workers[slot].reset();
            if (!found || index < 0 || index >= static_cast<s64>(m_entries.size())) return;
            auto& entry = m_entries[index];
            if (entry.app.icon.url.empty() && !artwork.icon.url.empty()) { entry.app.icon = std::move(artwork.icon); entry.icon.state = ImageState::None; }
            if (entry.app.banner.url.empty() && !artwork.banner.url.empty()) { entry.app.banner = std::move(artwork.banner); entry.banner.state = ImageState::None; }
            if ((entry.app.platform.empty() || entry.app.platform_learned) && !artwork.platform.empty()) {
                entry.app.platform = std::move(artwork.platform);
                entry.app.platform_learned = true;
            }
        }, token}, false);
    });
    if (!m_artwork_workers[slot]->IsRunning()) m_artwork_workers[slot].reset();
}

auto Menu::GetImage(Entry& e, bool banner, int& budget) -> int {
    QueuePublicArtwork(e);
    auto& image = banner ? e.banner : e.icon;
    const auto& src = banner ? e.app.banner : e.app.icon;
    const auto& url = src.url;

    if (image.image) {
        return image.image;
    }

    // a title the catalog has no artwork for is never going to gain any.
    if (url.empty()) {
        image.state = ImageState::Failed;
        return App::GetDefaultImage();
    }

    if (image.state == ImageState::Failed) {
        return App::GetDefaultImage();
    }

    // this runs per visible entry per frame, so bail before hashing a cache path
    // that won't be used.
    if (image.state == ImageState::Progress) {
        return 0;
    }

    if (budget <= 0) {
        return 0;
    }

    const auto path = BuildImageCache(e.app.app_id, url, banner ? "banner" : "icon");

    const auto load = [&]() -> bool {
        const auto data = ImageLoadFromFile(path, ImageFlag_None);
        if (data.data.empty()) {
            return false;
        }
        image.image = nvgCreateImageRGBA(App::GetVg(), data.w, data.h, 0, data.data.data());
        return image.image != 0;
    };

    // whatever is already on the sd card is worth a look before the network.
    if (!image.tried_cache) {
        image.tried_cache = true;
        image.cached = load();
        if (image.cached) {
            // only a real decode is worth charging against the frame's budget.
            budget--;
            return image.image;
        }
    }

    switch (image.state) {
        case ImageState::None: {
            image.state = ImageState::Progress;
            const auto index = &e - m_entries.data();

            // the shop gates its own artwork like its catalogue, so its copies
            // need credentials; a hotlink points at a cdn that must not get them.
            const auto user = src.local ? m_config.user : "";
            const auto pass = src.local ? m_config.pass : "";

            const auto queued = curl::Api().ToFileAsync(
                curl::Url{url},
                curl::Path{path},
                curl::UserPass{user, pass},
                curl::PreemptiveAuth{src.local},
                curl::Flags{curl::Flag_Cache},
                curl::StopToken{m_page_stop.get_token()},
                curl::OnProgress{[](s64 total, s64 now, s64, s64) { return total <= 16 * 1024 * 1024 && now <= 16 * 1024 * 1024; }},
                curl::OnComplete{[this, index, banner](auto& result) {
                    // entries never move while a page is up, and leaving one stops
                    // the token this carries, so the index names the same title.
                    auto& entry = m_entries[index];
                    auto& done = banner ? entry.banner : entry.icon;
                    done.state = result.success ? ImageState::Done : ImageState::Failed;
                }}
            );

            if (!queued) {
                image.state = ImageState::Failed;
            }
        }   break;

        case ImageState::Done: {
            if (load()) {
                budget--;
            } else {
                image.state = ImageState::Failed;
            }
        }   break;

        case ImageState::Progress:
        case ImageState::Failed:
            break;
    }

    return image.image;
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
    m_folder_names.push_back(app.name);
    m_base_url = app.download.url;
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
                App::Notify(info.name + (connect_result.used_remote ? ": connected via remote address"_i18n : ": connected via local network"_i18n));

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

void Menu::LoadPage(s64 page) {
    // the page on screen stays up until the next lands, with no modal: a fetch is
    // a few tens of milliseconds, and a dialog that appears and vanishes inside
    // that reads as slower than none. a request arriving meanwhile is held rather
    // than dropped, since `Order` can be flipped faster than the shop answers.
    if (m_loading) {
        m_pending_page = page;
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
    m_loader = std::make_unique<utils::Async>([this, config, base_url, query, page, token](){
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
            [this, page, total, apps = std::move(apps), error = std::move(error)]() mutable {
                if (!error.empty()) {
                    m_loading = false;
                    m_fetch_error = error;
                    App::Notify(m_config.name + ": " + error);
                } else {
                    // ApplyPage clears the flag by way of FreeEntries.
                    ApplyPage(page, apps, total);
                }

                // whatever was asked for mid-flight, issued now against the
                // options as they stand.
                if (m_pending_page) {
                    const auto next = *m_pending_page;
                    m_pending_page.reset();
                    LoadPage(next);
                }
            },
            token
        }, false);
    });
}

void Menu::ApplyPage(s64 page, std::vector<sphaira::ownfoil::api::ShopApp>& apps, s64 total) {
    m_page = page;
    m_total = total;
    m_fetch_error.clear();

    FreeEntries();
    m_entries.reserve(apps.size());
    for (auto& app : apps) {
        Entry entry{};
        entry.app = std::move(app);
        entry.icon.tried_cache = entry.banner.tried_cache = m_config.revalidate_artwork;
        m_entries.emplace_back(std::move(entry));
    }
    apps.clear();

    // a fresh page starts at the top.
    m_entry_index = 0;
    m_list->SetYoff(0);
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

    if (!m_platform.empty()) title += SEPARATOR + (m_platform == "__unknown__" ? std::string{"Sin clasificar"} : m_platform);
    if (!m_folder_names.empty()) title += SEPARATOR + m_folder_names.back();
    if (CATEGORIES[m_category].category == sphaira::ownfoil::api::Category::Search) {
        title += SEPARATOR;
        title += m_consolidated ? (m_platform.empty() ? "Toda la biblioteca" : "Consola actual") : m_config.legacy_html ? "Carpeta actual" : m_config.json_index ? "Índice actual" : "Todo el catálogo";
    }
    SetTitleSubHeading(title);
}

void Menu::ShowCategories() {
    // a panel down the left rather than an options entry: this is the one choice
    // that says what the whole screen is about, and is made often enough for a
    // button of its own.
    auto panel = std::make_unique<Sidebar>("Consolas y carpetas", i18n::get(CATEGORIES[m_category].name), Sidebar::Side::LEFT);
    ON_SCOPE_EXIT(App::Push(std::move(panel)));

    panel->Add<SidebarEntryCallback>("Actualizar catálogo", [this] { RefreshCatalog(); }, true);
    if (m_config.json_index) panel->Add<SidebarEntryCallback>("Ver carpetas del servidor", [this] { ShowServerFolders(); }, true,
        "Explora también las carpetas que aún no tienen un juego identificado.");
    if (!m_directory_stack.empty()) panel->Add<SidebarEntryCallback>("Volver a la carpeta anterior", [this] { GoUp(); }, true);
    for (const auto& entry : m_entries) {
        if (entry.app.directory) panel->Add<SidebarEntryCallback>(entry.app.name, [this, app = entry.app] { EnterDirectory(app); }, true);
    }
    const auto platforms = sphaira::ownfoil::api::CachedPlatforms(m_base_url, m_config);
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

void Menu::ShowSearch(s64 index) {
    std::string term;

    // the term on screen is the initial text: narrowing a search that came back
    // too wide is far more common than starting an unrelated one.
    const auto search_hint = m_consolidated ? (m_platform.empty() ? "Nombre o ID · toda la biblioteca" : "Nombre o ID · consola actual") : m_config.legacy_html ? "Nombre o parte del nombre · carpeta actual" : m_config.json_index ? "Nombre o ID · índice actual" : "Nombre o ID · todo el catálogo";
    if (R_FAILED(swkbd::ShowText(term, "Buscar juego", search_hint, m_search.c_str(), 1, SEARCH_MAX)) || term.empty()) {
        // backing out of the keyboard leaves the catalog that was up, up.
        return;
    }

    // the same term against the same catalog would come back the same.
    if (index == m_category && term == m_search) {
        return;
    }

    m_search = term;
    m_category = index;
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
        }, page_index, "Diez carátulas a la vez. Desplázate para ver el resto o cambia de página con ZL / ZR.");
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

    App::Push<TitleMenu>(m_config, m_base_url, MakeTitlePage(app));
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
