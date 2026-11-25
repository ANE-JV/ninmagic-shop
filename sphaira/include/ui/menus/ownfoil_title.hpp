#pragma once

#include "ui/menus/menu_base.hpp"
#include "utils/ownfoil.hpp"
#include "utils/ownfoil_api.hpp"
#include "utils/ownfoil_installed.hpp"
#include "utils/thread.hpp"
#include "fs.hpp"
#include "image.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <stop_token>

namespace sphaira::ui::menu::ownfoil {

// shared by the catalog and the pages it opens, so artwork the grid already
// fetched is read back rather than fetched again.
inline constexpr auto CACHE_PATH = "/switch/NinMagicShop/cache/ownfoil";

// keyed on the url, not the app id: two shops can answer with different art for
// one game, and a shop swaps a hotlink for its own copy as the download lands.
// both ends mint content-addressed names, so a changed url means changed bytes.
auto BuildImageCache(const std::string& app_id, const std::string& url, const char* kind) -> fs::FsPath;
auto LoadShopImage(const sphaira::ownfoil::Config& config, const std::string& artwork_id,
    const sphaira::ownfoil::api::ShopImage& image, const char* kind, std::stop_token token) -> ImageResult;

// Created, filled and released on the UI thread. A detail page may outlive
// the catalog page that originally uploaded this thumbnail.
struct ShopCardTexture {
    ShopCardTexture() = default;
    ShopCardTexture(const ShopCardTexture&) = delete;
    auto operator=(const ShopCardTexture&) -> ShopCardTexture& = delete;
    ~ShopCardTexture();
    int handle{};
};

// what a page is opened with, so it can draw the name and artwork its card
// already carried on its first frame rather than wait on the shop.
struct TitlePage {
    std::string id{}; // a game's title id, or a dlc's own app id.
    bool dlc{};
    // on a dlc's page, the only way to ask the console whether it is installed.
    std::string artwork_id{};
    std::string game_id{};
    std::string name{};
    std::string publisher{};
    std::string platform{};
    std::string genre{};
    std::string description{};
    std::string game_name{}; // a dlc's game.
    sphaira::ownfoil::api::ShopImage banner{};
    // drawn beside the game a dlc needs, and in place of a missing banner.
    sphaira::ownfoil::api::ShopImage icon{};
    std::shared_ptr<const ShopCardTexture> card_texture{};
    // a dlc's own file: the shop answers with no apps under a dlc's id, so this
    // only ever comes from whatever opened the page.
    sphaira::ownfoil::api::ShopDownload download{};
};

// the page's images edge to edge at the size the shop keeps them. pushed over the
// page, which outlives it, so its smaller copies stand in while a full one loads.
struct ScreenshotViewer final : Widget {
    ScreenshotViewer(const sphaira::ownfoil::Config& config, const std::string& id, std::vector<sphaira::ownfoil::api::ShopImage> images, std::vector<int> previews, s64 index);
    ~ScreenshotViewer();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    auto IsMenu() const -> bool override {
        return true;
    }

private:
    void Show(s64 index);

    const sphaira::ownfoil::Config m_config;
    const std::string m_id;
    const std::vector<sphaira::ownfoil::api::ShopImage> m_images;
    // the page's own textures, 0 where none landed: not this viewer's to delete.
    const std::vector<int> m_previews;
    s64 m_index{-1};
    int m_image{}; // the full-size texture for m_index, 0 until it lands.

    // stopped when the image changes, abandoning the load left behind.
    std::stop_source m_stop{};
    std::unique_ptr<utils::Async> m_loader{};
};

// one game or dlc, pushed over the catalog. the right column - the name, the
// facts, the versions - never moves; the left is a page that scrolls under it:
// the images, the tagline over the description, then a row per dlc.
struct TitleMenu final : MenuBase, std::enable_shared_from_this<TitleMenu> {
    enum class Mode { FullPage, Preview };
    TitleMenu(const sphaira::ownfoil::Config& config, const std::string& base_url, const TitlePage& page, Mode mode = Mode::FullPage);
    ~TitleMenu();

    auto GetShortTitle() const -> const char* override { return "Game"; }
    void OnFocusGained() override;
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

    // One metadata/selection/planner owner for both the full page and the
    // catalog's right pane. Preview polling never consumes catalog controls.
    auto PollPreview() -> bool;
    void OpenFullPage();
    static void DrawPreviewSummary(NVGcontext* vg, const Vec4& bounds,
        const sphaira::ownfoil::api::ShopApp& app, int fallback_artwork);
    void CancelPreview() { m_stop.request_stop(); }
    auto PreviewWorkersFinished() const -> bool {
        return (!m_loader || m_loader->HasFinished()) && (!m_console || m_console->HasFinished()) && (!m_gallery || m_gallery->HasFinished());
    }
    void DrawDetailPanel(NVGcontext* vg, Theme* theme, const Vec4& bounds, int fallback_artwork = 0);
    auto HandleDetailTouch(const TouchInfo* touch) -> bool;
    auto HandleDetailController(Controller* controller) -> bool;
    auto IsLastDetailAction() const -> bool { return m_detail_action == 4; }
    void SetDetailFocused(bool focused) { m_detail_focused = focused; }
    void OpenInstallOptions();
    void QueueSelectedPackage();
    auto IsReady() const -> bool { return m_loaded && m_console_loaded && !m_failed; }
    auto HasFailed() const -> bool { return m_failed; }

private:
    void EnsureSelection();
    void ChoosePanelVersion();
    void StartGallery();
    auto HasPackage() const -> bool;
    auto IsFullPage() const -> bool { return m_mode == Mode::FullPage || m_promoted; }
    void ActivateDetailAction(s64 action);
    auto HasBasePackage() const -> bool;
    auto HasUpdatePackage() const -> bool;
    auto DetailActionBounds(s64 action) const -> Vec4;
    auto DetailActionAvailable(s64 action) const -> bool;


    // one row of the fact list, its label already translated.
    struct Fact {
        std::string label{};
        std::string value{};
        bool group{}; // starts a group of its own, set a little apart.
    };

    // which of A's two jobs is bound right now: a highlighted row opens its own
    // page, anything else the install menu - which waits for the shop, being a
    // choice of what the shop holds.
    enum class MainAction { None, Install, View };

    // one of the game's dlc, as its row draws it.
    struct Row {
        std::string name{};    // without the game's name in front.
        std::string tagline{}; // flattened onto one line.
        bool installed{};
        int image{};           // its banner, 0 until the decode lands.
    };

    // the shop's once it has answered, the card's until then - too small to draw,
    // but enough to say whether there is a banner to make room for.
    auto GetBanner() const -> const sphaira::ownfoil::api::ShopImage&;
    auto DetailArtwork(int fallback = 0) const -> int;
    void LoadDetailBanner(const sphaira::ownfoil::api::ShopImage& image, int TitleMenu::*destination);
    auto GetName() const -> const std::string&;
    auto GetPublisher() const -> const std::string&;
    auto GetDescription() const -> const std::string&;
    void RefreshDetailText();
    // the banner, when there is one, then the screenshots; 0 until a decode lands.
    auto GetImageCount() const -> s64;
    auto GetImage(s64 index) const -> int;
    void SetImageIndex(s64 index);
    // built when the shop answers and again when the console does.
    void BuildFacts();
    // either answer can be the last to land, so both run it.
    void MarkInstalled();
    // run from a worker, when the page opens and after an install.
    void LoadInstalled(std::stop_token token);

    // measures where each part of the page starts, at the width it is drawn at.
    void Layout();
    auto GetImagesBottom() const -> float;
    auto GetMaxScroll() const -> float;
    void ScrollTo(float y);
    // whether the page is still up at its images, which left, right and Y act on.
    auto ImagesInView() const -> bool;
    auto GetRowTop(s64 index) const -> float;
    auto RowInView(s64 index) const -> bool;
    auto FirstRowInView() const -> s64; // -1 when none is wholly on screen.
    auto GetRowRect(s64 index) const -> Vec4;
    // down and up: the page a part at a time, then the rows one at a time.
    void StepDown();
    void StepUp();
    void SetFocus(s64 index); // -1 is no row; a highlighted row is scrolled to.
    // set every frame, outside any action - replacing an Action from inside its
    // own callback would destroy the running std::function.
    void UpdateActions();
    // A off the rows. what the menu is set to lives on the page rather than in the
    // sidebar, so reopening it comes back to the same choices.
    void ShowInstallOptions();
    void Install();
    // one on/off entry per dlc, over the install menu. the row it was opened from
    // is handed in, since it counts what is ticked.
    void ShowAddons(const std::function<void()>& changed);
    auto GetAddonCount() const -> std::string;
    // what Install greys out on, re-asked every frame the menu is drawn.
    auto HasSomethingToInstall() const -> bool;
    // Update or Downgrade when the only thing chosen is a version newer or older
    // than the console's, Install for anything else.
    auto GetInstallLabel() const -> std::string;
    // what the console holds and newer, plus an older update while downgrades are
    // allowed; anything else is greyed out in the picker.
    auto IsVersionOffered(s64 version) const -> bool;
    void OpenDlc(s64 index);
    void OpenViewer();

    void DrawPage(NVGcontext* vg, Theme* theme);
    void DrawColumn(NVGcontext* vg, Theme* theme);
    void ShowRatingPicker(int current);

private:
    const sphaira::ownfoil::Config m_config;
    const std::string m_base_url;
    const TitlePage m_page;
    const Mode m_mode;
    bool m_promoted{};
    std::string m_load_error{};
    bool m_selection_ready{};
    bool m_base_selected{true};
    bool m_update_selected{true};
    bool m_dlc_selected{true};
    bool m_detail_focused{};
    s64 m_detail_action{3};
    Vec4 m_detail_bounds{};
    enum class PendingAction { None, Options, Queue };
    PendingAction m_pending_action{PendingAction::None};

    // Baseline for the package and console timing log.
    const TimeStamp m_opened{};

    sphaira::ownfoil::api::ShopTitle m_title{};
    sphaira::ownfoil::api::ShopApp m_presentation{};
    bool m_presentation_done{};
    std::string m_detail_genre{};
    std::string m_detail_metadata{};
    std::string m_detail_description{};
    bool m_loaded{}; // the shop has answered for m_title.
    bool m_failed{}; // the shop won't answer: its error has been shown instead.
    // what this console holds, answered on a thread of its own, before the shop
    // or after it.
    bool m_console_loaded{};
    sphaira::ownfoil::installed::InstalledVersion m_installed{};
    std::vector<std::string> m_installed_dlc{};
    bool m_dlc_installed{};
    std::vector<Fact> m_facts{};
    std::string m_intro{}; // the tagline, flattened: it heads the description.
    std::vector<Row> m_rows{};

    int m_banner{};
    int m_card_banner{};
    int m_icon{};
    int m_presentation_artwork{};
    std::mutex m_banner_mutex{};
    std::vector<std::string> m_requested_banners{};
    std::vector<int> m_screenshots{};
    s64 m_image_index{};

    // the page's parts, where 0 is the top of the big image.
    float m_text_y{};
    float m_intro_h{};
    float m_desc_y{};
    float m_desc_h{};
    float m_facts_y{};
    float m_facts_h{};
    float m_rows_y{};
    float m_content_h{};

    float m_scroll{};        // how far the page is scrolled, as drawn.
    float m_scroll_target{}; // where it is gliding to.
    bool m_stick_scroll{};   // the stick sent it there, which glides at its own pace.
    bool m_dragging{};       // a drag that began on the page is moving it.
    float m_drag_from{};     // where the page was when that drag began.
    s64 m_focus{-1};
    std::string m_counter{}; // the subheading, set only when it changes.
    MainAction m_action{MainAction::None};

    // an index into the title's own version list, the newest until changed.
    s64 m_install_version{};
    // one per add-on row, sized when the menu is first opened - which is also what
    // says the defaults have been shown.
    std::vector<u8> m_install_dlc{};

    // stopped on the way out, ahead of the joins, so nothing still in flight is
    // waited on or run against a page that has gone.
    std::stop_source m_stop{};
    std::unique_ptr<utils::Async> m_loader{};
    // the console's answer, then the card's icon off the sd card: neither is a
    // reason to hold the shop's request up.
    std::unique_ptr<utils::Async> m_console{};
    std::unique_ptr<utils::Async> m_gallery{};
    // the install queue's finished count when this page last looked.
    u64 m_queue_finished{};
};

} // namespace sphaira::ui::menu::ownfoil
