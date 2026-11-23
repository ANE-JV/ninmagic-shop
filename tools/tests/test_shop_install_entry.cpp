#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

using s64 = std::int64_t;
using u64 = std::uint64_t;
std::string operator""_i18n(const char* value, size_t bytes) { return {value, bytes}; }
enum class Button : unsigned { A = 1, Y = 2, B = 4, DPAD_LEFT = 8, LS_LEFT = 16,
    DPAD_UP = 32, LS_UP = 64, DPAD_DOWN = 128, LS_DOWN = 256 };
constexpr Button operator|(Button a, Button b) { return Button(unsigned(a) | unsigned(b)); }
struct Controller {
    Button pressed;
    bool GotDown(Button buttons) const { return (unsigned(pressed) & unsigned(buttons)) != 0; }
};
enum class SoundEffect { Scroll };
struct Rect { float x{}, y{}, w{}, h{}; };
struct TouchInfo {
    bool is_clicked{};
    float x{}, y{};
    bool in_range(const Rect& box) const {
        return x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
    }
};
struct Action { std::string text; std::function<void()> press; };
struct TitleMenu;
struct DetailPageView {};
struct OptionBox {};
struct App {
    static inline std::shared_ptr<TitleMenu> screen;
    static inline std::string notice;
    static void PlaySoundEffect(SoundEffect) {}
    static inline std::function<void()> close;
    template <class T> static void Push(std::shared_ptr<TitleMenu> title, std::function<void()> on_close) {
        screen = std::move(title);
        close = std::move(on_close);
    }
    template <class T> static void Push(const std::string& message, const char*) { notice = message; }
};
struct InstallQueue {
    static InstallQueue& Get() { static InstallQueue queue; return queue; }
    int GetFinishedCounter() const { return 0; }
};
namespace utils {
struct Async {
    template<class Function> explicit Async(Function) {}
    bool HasFinished() const { return false; }
    bool IsRunning() const { return true; }
};
}
namespace api {
struct ShopDownload { std::string url, extension; };
struct ShopVersion { bool base{}; ShopDownload download; s64 version{}; bool version_known{}; std::string display{}; };
}
using Download = api::ShopDownload;
struct Page { bool dlc{}, directory{}; std::string app_id, name; Download download; };
Page MakeTitlePage(const Page& p) { return p; }
// The clock does not advance. A fresh selection therefore exercises the actual
// zero-wait path without waiting out a debounce timer in the test.
u64 armGetSystemTick() { return 10; }
u64 armTicksToNs(u64 ticks) { return ticks; }

struct TitleMenu : std::enable_shared_from_this<TitleMenu> {
    enum class Mode { Preview, FullPage };
    enum class MainAction { None, Install, View };
    enum class PendingAction { None, Options, Queue };
    static inline int loads{};
    TitleMenu(int, const std::string&, const Page& page, Mode mode = Mode::FullPage) : m_page(page), m_mode(mode) { ++loads; }
    Page m_page;
    Mode m_mode;
    MainAction m_action{MainAction::None};
    PendingAction m_pending_action{PendingAction::None};
    std::string m_load_error, m_counter;
    s64 m_focus{-1}, m_image_index{};
    int m_queue_finished{}, reviews{}, m_detail_action{};
    bool m_promoted{}, m_detail_focused{}, popped{};
    void SetPop(bool on = true) { popped = on; }
    bool IsFullPage() const { return m_promoted || m_mode == Mode::FullPage; }
    Rect m_detail_bounds{0,0,100,100};
    Rect DetailActionBounds(s64 index) const { return {0, float(index * 15), 100, 12}; }
    bool HandleDetailController(Controller*);
    bool HandleDetailTouch(const TouchInfo*);
    void Layout() {}
    void BuildFacts() {}
    void StartGallery() {}
    bool m_loaded{}, m_console_loaded{}, m_failed{}, m_update_selected{}, m_base_selected{}, m_selection_ready{};
    bool m_dlc_selected{}, m_dlc_installed{}, base_available{true};
    struct Installed { bool installed{}; };
    struct Content { Download download; };
    struct Package { std::vector<api::ShopVersion> versions; std::vector<Content> dlc; } m_title;
    s64 m_install_version{-1};
    Installed m_installed;
    std::vector<Installed> m_rows;
    std::vector<int> m_install_dlc;
    std::stop_source m_stop;
    std::unique_ptr<utils::Async> m_console;
    std::function<void()> primary, images;
    void SetAction(Button key, Action action) { (key == Button::A ? primary : images) = std::move(action.press); }
    void RemoveAction(Button key) { (key == Button::A ? primary : images) = {}; }
    bool HasAction(Button key) const { return bool(key == Button::A ? primary : images); }
    bool IsReady() const { return m_loaded && m_console_loaded && !m_failed; }
    bool HasSomethingToInstall() const { return IsReady() && base_available; }
    bool HasBasePackage() const;
    bool HasUpdatePackage() const;
    bool HasPackage() const;
    s64 GetImageCount() const { return 0; }
    bool ImagesInView() const { return false; }
    bool PreviewWorkersFinished() const { return false; }
    void SetSubHeading(const std::string&) {}
    void OpenDlc(s64) { assert(false); }
    void OpenViewer() { assert(false); }
    void QueueSelectedPackage() { assert(false); }
    void Install() { assert(false); }
    void LoadInstalled(std::stop_token) {}
    void EnsureSelection();
    void ChoosePanelVersion();
    void ShowInstallOptions() { ++reviews; }
    void OpenFullPage();
    bool PollPreview();
    void OpenInstallOptions();
    void ActivateDetailAction(s64);
    auto DetailActionAvailable(s64) const -> bool;
    void UpdateActions();
};
struct Menu {
    enum class Mode { Home };
    struct Entry { Page app; };
    Mode m_mode{Mode::Home};
    int m_config{};
    s64 m_entry_index{};
    std::string m_base_url{"https://shop.invalid/"}, m_preview_id;
    u64 m_preview_changed_tick{};
    std::vector<Entry> m_entries;
    std::shared_ptr<TitleMenu> m_preview, m_retired_preview;
    void EnterDirectory(const Page&) { assert(false); }
    void RetirePreview() { m_retired_preview = std::move(m_preview); }
    void UpdatePreview(bool immediate = false);
    void OpenSelected();
};

#include "production_install_entry.inc"

static void CheckSlowServer(bool touch) {
    App::screen.reset();
    App::notice.clear();
    TitleMenu::loads = 0;
    Menu menu;
    menu.m_entries.push_back({{false, false, "0100123456780000", "Demo game", {}}});
    // Start with an unfinished preview once, and a freshly picked card once.
    if (touch) menu.UpdatePreview(true);
    const auto existing = menu.m_preview;
    menu.OpenSelected();
    assert(App::screen && TitleMenu::loads == 1);
    auto& title = *App::screen;
    if (existing) assert(existing == App::screen);
    title.UpdateActions();
    assert(!title.IsReady() && title.primary);
    for (int repeat = 0; repeat < 2; ++repeat) {
        if (touch) {
            const TouchInfo outside{true, 250, 250};
            assert(!title.HandleDetailTouch(&outside));
            const auto button = title.DetailActionBounds(3);
            const TouchInfo inside{true, button.x + 2, button.y + 2};
            assert(title.HandleDetailTouch(&inside));
        } else {
            Controller pad{Button::A};
            assert(title.HandleDetailController(&pad));
        }
    }
    assert(title.reviews == 0 && title.m_pending_action == TitleMenu::PendingAction::Options);
    title.m_console_loaded = true;
    title.PollPreview();
    assert(title.reviews == 0);
    // The provider releases its response now; artwork has never completed.
    title.m_title.versions.push_back({true, {"https://shop.invalid/verified", "nsp"}});
    title.m_loaded = true;
    title.PollPreview();
    title.PollPreview();
    assert(title.reviews == 1 && title.m_pending_action == TitleMenu::PendingAction::None);
    Controller back{Button::B};
    assert(title.HandleDetailController(&back) && title.popped);
    App::close();
    App::screen.reset();
    menu.OpenSelected();
    assert(App::screen == menu.m_preview && TitleMenu::loads == 1);
    auto& incomplete = *App::screen;
    incomplete.m_title.versions = {{true, {"https://shop.invalid/folder/", ""}}};
    incomplete.ActivateDetailAction(3);
    assert(incomplete.reviews == 1 && App::notice.find("Sin archivos") != std::string::npos);
    incomplete.m_failed = true;
    incomplete.m_load_error = "HTTP 403";
    incomplete.ActivateDetailAction(3);
    assert(incomplete.reviews == 1 && App::notice.find("403") != std::string::npos);
}
static void CheckUpdateChoice(bool already_installed) {
    TitleMenu title(0, "", {}, TitleMenu::Mode::FullPage);
    Controller up{Button::DPAD_UP}, activate{Button::A};
    title.m_detail_action = 3;
    assert(!title.DetailActionAvailable(0) && !title.DetailActionAvailable(1));
    title.m_title.versions = {{true, {"base", "nsp"}}, {false, {"patch", "nsz"}}};
    title.m_loaded = true;
    title.PollPreview();
    assert(title.DetailActionAvailable(0) && title.DetailActionAvailable(1));
    assert(!title.DetailActionAvailable(4) && title.m_base_selected && title.m_update_selected);
    title.HandleDetailController(&up);
    assert(title.m_detail_action == 1);
    title.HandleDetailController(&activate);
    assert(title.m_base_selected && !title.m_update_selected && title.m_install_version == 0);
    title.HandleDetailController(&up);
    title.HandleDetailController(&activate);
    assert(!title.m_base_selected);
    const auto update = title.DetailActionBounds(1);
    const TouchInfo tap{true, update.x + 1, update.y + 1};
    assert(title.HandleDetailTouch(&tap) && title.m_update_selected && !title.m_base_selected);
    assert(title.m_install_version == 1);
    title.OpenInstallOptions();
    assert(title.reviews == 0);
    title.m_installed.installed = already_installed;
    title.m_console_loaded = true;
    title.PollPreview();
    assert(title.reviews == 1 && title.m_update_selected && title.m_install_version == 1);
    assert(title.m_base_selected == !already_installed);
}

int main() {
    CheckUpdateChoice(true);
    CheckUpdateChoice(false);
    assert(FormatInstallVersion({false, {"patch", "nsp"}}) == "Actualización");
    assert(FormatInstallVersion({true, {"base", "nsp"}}) == "Base");
    CheckSlowServer(false);
    CheckSlowServer(true);
    std::cout << "Slow provider: selection opens immediately, A/touch opens review once packages are ready\n";
}
