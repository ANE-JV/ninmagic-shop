// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menus/main_menu.hpp"
#include "ui/menus/ownfoil.hpp"
#include "ui/menus/game_menu.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/install_queue.hpp"
#include "ui/scrollable_text.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "evman.hpp"
#include "i18n.hpp"
#include "shop_update.hpp"
#include <utility>

namespace sphaira::ui::menu::main {
namespace {
class UpdateInfoMenu final : public MenuBase {
public:
    UpdateInfoMenu(std::string version, std::string notes, bool available, std::function<void()> install)
    : MenuBase{"Actualizar NinMagic Shop", MenuFlag_None}, m_available{available} {
        SetTitleSubHeading(version.empty() ? "v" APP_VERSION : "v" + version);
        SetAction(Button::B, Action{"Volver", [this] { SetPop(); }});
        if (available) SetAction(Button::A, Action{"Actualizar", [install = std::move(install)] { if (install) install(); }});
        m_notes = std::make_unique<ScrollableText>(notes.empty() ? "Sin notas de esta versión." : notes, 0.f, 205.f, 385.f, 1000.f, 20.f);
    }
    const char* GetShortTitle() const override { return "Actualizar"; }
    void Update(Controller* controller, TouchInfo* touch) override { MenuBase::Update(controller, touch); m_notes->Update(controller, touch); }
    void Draw(NVGcontext* vg, Theme* theme) override {
        MenuBase::Draw(vg, theme);
        const auto text = m_available ? "Hay una nueva versión de NinMagic Shop" : "Ya tienes la versión publicada";
        gfx::drawText(vg, 110.f, 125.f, 25.f, theme->GetColour(ThemeEntryID_TEXT), text, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        gfx::drawText(vg, 110.f, 166.f, 20.f, theme->GetColour(ThemeEntryID_TEXT_INFO), "Novedades", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        gfx::drawRect(vg, 110.f, 187.f, 1040.f, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        m_notes->Draw(vg, theme);
    }
private:
    bool m_available{};
    std::unique_ptr<ScrollableText> m_notes;
};
template<typename Menu>
auto MakeMenu(u32 flags) { return std::make_unique<Menu>(flags); }
const MiscMenuEntry SHOP_MENUS[] = {
    {.name="Ownfoil", .title="Tienda", .func=MakeMenu<ownfoil::Menu>, .flag=MiscMenuFlag_Shortcut,
        .info="Catálogo, búsqueda y cuenta del servidor que elijas."},
    {.name="Games", .title="Juegos instalados", .func=MakeMenu<game::Menu>, .flag=MiscMenuFlag_Shortcut,
        .info="Consulta los juegos instalados en esta consola."},
};
}
auto GetMenuMenuEntries() -> std::span<const MiscMenuEntry> { return SHOP_MENUS; }

MainMenu::MainMenu() {
    // Shop is always the central page; old launcher tab preferences cannot
    // route a fresh installation into unrelated system/file tools.
    m_centre_menu = std::make_unique<ownfoil::Menu>(MenuFlag_Tab);
    m_right_menu = std::make_unique<game::Menu>(MenuFlag_Tab);
    m_current_menu = m_centre_menu.get();
    SetAction(Button::START, Action{"Menú", [this] { ShowShopOptions(); }});
    AddOnLRPress();
    for (const auto& [button, action] : m_actions) m_current_menu->SetAction(button, action);
    CheckForUpdates(false);
}
MainMenu::~MainMenu() {
    m_update_stop.request_stop();
    m_update_worker.reset();
}
void MainMenu::ShowCatalog() {
    if (m_current_menu != m_centre_menu.get()) OnLRPress(m_centre_menu.get(), Button::R);
}
void MainMenu::ShowShopOptions() {
    auto options = std::make_unique<Sidebar>("NinMagic Shop", "v" APP_VERSION, Sidebar::Side::LEFT);
    options->Add<SidebarEntryCallback>("Tienda", [this] { ShowCatalog(); }, "Vuelve al catálogo del servidor seleccionado.");
    options->Add<SidebarEntryCallback>("Cuenta y servidores", [this] {
        ShowCatalog();
        static_cast<ownfoil::Menu*>(m_centre_menu.get())->ShowServerProfiles();
    }, "Añade o edita el nombre, la dirección, el usuario y la contraseña de tus perfiles.");
    options->Add<SidebarEntryCallback>("Descargas e instalaciones", [] { InstallQueue::Get().ShowMenu(); }, "Consulta el progreso, la cola y el resultado de tus instalaciones.");
    options->Add<SidebarEntryCallback>("Juegos instalados", [this] {
        if (m_current_menu != m_right_menu.get()) OnLRPress(m_right_menu.get(), Button::R);
    }, "Abre los juegos que ya están en esta consola.");
    options->Add<SidebarEntryCallback>("Opciones de instalación", [] { App::DisplayInstallOptions(); }, "Destino y opciones del instalador.");
    SidebarEntryArray::Items languages{"Automático", "English", "日本語", "Français", "Deutsch", "Italiano", "Español", "简体中文", "한국어", "Nederlands", "Português", "Русский", "繁體中文", "Svenska", "Tiếng Việt", "Українська", "Türkçe", "Magyar"};
    options->Add<SidebarEntryArray>("Idioma", languages, [](s64& index) { App::SetLanguage(index); }, static_cast<s64>(App::GetLanguage()), "Selecciona el idioma de la aplicación.");
    if (m_update_state == UpdateState::Update) options->Add<SidebarEntryCallback>("Actualizar a " + m_release.version, [this] { ShowUpdateInfo(); }, true, "Lee las novedades antes de instalar el NRO verificado.");
    options->Add<SidebarEntryCallback>("Buscar actualización", [this] { CheckForUpdates(true); }, true, "Consulta el canal oficial de NinMagic Shop mediante HTTPS verificado.");
    options->Add<SidebarEntryCallback>("Acerca de NinMagic Shop", [] {
        App::Push<OptionBox>("NinMagic Shop v" APP_VERSION "\nCliente Ownfoil basado en Sphaira.\nSelecciona tu servidor y usa tu cuenta personal.\nSoftware libre, licencia GPLv3.", "Aceptar");
    });
    options->Add<SidebarEntryCallback>("Salir", [this] { RequestExit(); });
    App::Push(std::move(options));
}
void MainMenu::CheckForUpdates(bool show_result) {
    if (m_update_worker && m_update_state == UpdateState::Pending) { if (show_result) App::Notify("Buscando actualizaciones de NinMagic Shop..."); return; }
    m_update_worker.reset();
    const auto generation = ++m_update_generation;
    const auto token = m_update_stop.get_token();
    m_update_state = UpdateState::Pending;
    if (show_result) App::Notify("Buscando actualizaciones de NinMagic Shop...");
    m_update_worker = std::make_unique<utils::Async>([this, generation, token, show_result] {
        shop_update::Manifest release; std::string error;
        const bool recovered = shop_update::RecoverInstalled(error);
        const bool checked = recovered && shop_update::Check(release, error, {.cancelled=[token] { return token.stop_requested(); }, .progress={}});
        evman::push(evman::CallbackEventData{[this, generation, checked, show_result, release=std::move(release), error=std::move(error)]() mutable {
            if (generation != m_update_generation) return;
            m_release = std::move(release); m_update_error = std::move(error);
            m_update_state = !checked ? UpdateState::Error : shop_update::IsVersionNewer(APP_VERSION, m_release.version) ? UpdateState::Update : UpdateState::None;
            if (show_result) ShowUpdateInfo();
            else if (m_update_state == UpdateState::Update) App::Notify("NinMagic Shop " + m_release.version + " disponible. Abre + Shop para actualizar.");
        }, token}, false);
    });
    if (!m_update_worker->IsRunning()) {
        m_update_state = UpdateState::Error; m_update_error = "No se pudo iniciar la consulta de actualización.";
        if (show_result) ShowUpdateInfo();
    }
}
void MainMenu::ShowUpdateInfo() {
    if (m_update_state == UpdateState::Pending) { App::Notify("Buscando actualizaciones de NinMagic Shop..."); return; }
    if (m_update_state == UpdateState::Error) { App::Push<OptionBox>(m_update_error.empty() ? "No se pudo consultar la actualización." : m_update_error, "Aceptar"); return; }
    App::Push<UpdateInfoMenu>(m_release.version, m_release.notes, m_update_state == UpdateState::Update, [this] { StartUpdateInstall(); });
}
void MainMenu::StartUpdateInstall() {
    if (m_update_state != UpdateState::Update) return;
    if (InstallQueue::Get().IsBusy()) { App::Push<OptionBox>("Termina las instalaciones de la cola antes de actualizar NinMagic Shop.", "Aceptar"); return; }
    if (App::GetExePath() != shop_update::Target) {
        App::Push<OptionBox>("Instala y abre NinMagic Shop desde\n/switch/NinMagicShop/NinMagic-Shop.nro\npara usar la actualización integrada.", "Aceptar"); return;
    }
    const auto release = m_release;
    App::Push<OptionBox>("Actualizar NinMagic Shop a " + release.version + "\nSe verificará el SHA-256 y se conservará un respaldo.\nAl terminar tendrás que cerrar y volver a abrir la aplicación.", "Cancelar", "Actualizar", 0, [this, release](auto selected) {
        if (!selected || !*selected) return;
        auto detail = std::make_shared<std::string>();
        App::Push<ProgressBox>(0, "Actualizando NinMagic Shop", release.version, [release, detail](auto progress) -> Result {
            progress->NewTransfer("Descargando y verificando el NRO oficial");
            // App loads assets with short-lived RomFS mounts. Explicitly
            // unmount its default device before renaming the running NRO;
            // NOTFOUND is harmless, and no UI assets are read during Apply.
            romfsExit();
            const bool ok = shop_update::Install(release, *detail, {
                .cancelled=[progress] { return progress->ShouldExit(); },
                .progress=[progress](uint64_t current, uint64_t total) { progress->UpdateTransfer(current, total); }
            });
            return ok ? 0 : Result_MainFailedToInstallUpdate;
        }, [this, detail](Result result) {
            if (R_FAILED(result)) { App::Push<OptionBox>(detail->empty() ? "No se pudo completar la actualización. Se conservan los respaldos." : *detail, "Aceptar"); return; }
            m_update_state = UpdateState::None;
            App::Push<OptionBox>("NinMagic Shop se ha actualizado.\nCierra la aplicación y vuelve a abrirla desde hbmenu.", "Cerrar", [](auto) { App::Exit(); });
        });
    });
}
void MainMenu::RequestExit() {
    if (!InstallQueue::Get().IsBusy()) { App::Exit(); return; }
    App::Push<OptionBox>("Hay instalaciones en curso. ¿Quieres salir?", "Continuar", "Salir", 0,
        [](auto selected) { if (selected && *selected) App::Exit(); });
}
void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    m_current_menu->Update(controller, touch);
    if (m_current_menu->ShouldPop()) {
        m_current_menu->SetPop(false);
        if (m_current_menu != m_centre_menu.get()) ShowCatalog();
        else RequestExit();
    }
}
void MainMenu::Draw(NVGcontext* vg, Theme* theme) { m_current_menu->Draw(vg, theme); }
void MainMenu::OnFocusGained() { Widget::OnFocusGained(); m_current_menu->OnFocusGained(); }
void MainMenu::OnFocusLost() { Widget::OnFocusLost(); m_current_menu->OnFocusLost(); }
void MainMenu::OnLRPress(MenuBase* menu, Button) {
    m_current_menu->OnFocusLost();
    m_current_menu = menu;
    AddOnLRPress();
    m_current_menu->OnFocusGained();
    for (const auto& [button, action] : m_actions) m_current_menu->SetAction(button, action);
}
void MainMenu::AddOnLRPress() {
    const bool catalog = m_current_menu == m_centre_menu.get();
    if (catalog) {
        SetAction(Button::L, Action{[this] { static_cast<ownfoil::Menu*>(m_centre_menu.get())->CyclePlatform(-1); }});
        SetAction(Button::R, Action{"Consola", [this] { static_cast<ownfoil::Menu*>(m_centre_menu.get())->CyclePlatform(1); }});
    } else {
        SetAction(Button::L, Action{"Descargas", [] { InstallQueue::Get().ShowMenu(); }});
        SetAction(Button::R, Action{"Tienda", [this] { OnLRPress(m_centre_menu.get(), Button::R); }});
    }
}
}
