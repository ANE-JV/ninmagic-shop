#pragma once

#include "ui/widget.hpp"
#include "ui/menus/menu_base.hpp"
#include "fs.hpp"
#include "shop_update.hpp"
#include "utils/thread.hpp"
#include <span>
#include <stop_token>

namespace sphaira::ui::menu::main {

enum class UpdateState {
    // still downloading json from github
    Pending,
    // no update available.
    None,
    // update available!
    Update,
    // there was an error whilst checking for updates.
    Error,
};

using MiscMenuFunction = std::function<std::unique_ptr<MenuBase>(u32 flags)>;

enum MiscMenuFlag : u8 {
    // can be set as the rightside menu.
    MiscMenuFlag_Shortcut = 1 << 0,
    // needs install option to be enabled.
    MiscMenuFlag_Install = 1 << 1,
};

struct MiscMenuEntry {
    const char* name;
    const char* title;
    MiscMenuFunction func;
    u8 flag;
    const char* info;

    auto IsShortcut() const -> bool {
        return flag & MiscMenuFlag_Shortcut;
    }

    auto IsInstall() const -> bool {
        return flag & MiscMenuFlag_Install;
    }
};

auto GetMenuMenuEntries() -> std::span<const MiscMenuEntry>;

// this holds 2 menus and allows for switching between them
struct MainMenu final : Widget {
    MainMenu();
    ~MainMenu();

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    auto IsMenu() const -> bool override {
        return true;
    }

private:
    void OnLRPress(MenuBase* menu, Button b);
    void AddOnLRPress();
    void CheckForUpdates(bool show_result);
    void ShowUpdateInfo();
    void StartUpdateInstall();
    void ShowShopOptions();
    void ShowCatalog();
    void RequestExit();

private:
    std::unique_ptr<MenuBase> m_centre_menu{};
    std::unique_ptr<MenuBase> m_right_menu{};
    MenuBase* m_current_menu{};

    shop_update::Manifest m_release{};
    std::string m_update_error{};
    UpdateState m_update_state{UpdateState::Pending};
    bool m_show_update_result{};
    u64 m_update_generation{};
    std::stop_source m_update_stop{};
    std::unique_ptr<utils::Async> m_update_worker{};
};

} // namespace sphaira::ui::menu::main
