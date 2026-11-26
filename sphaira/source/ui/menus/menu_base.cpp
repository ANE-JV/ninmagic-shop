#include "app.hpp"
#include "log.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"
#include "i18n.hpp"
#include "ui/shop_style.hpp"
#include <algorithm>

namespace sphaira::ui::menu {

auto MenuBase::GetPolledData(bool force_refresh) -> PolledData {
    static PolledData data{};
    static TimeStamp timestamp{};
    static bool has_init = false;

    if (!has_init) {
        has_init = true;
        force_refresh = true;
    }

    // update every second, do this in Draw because Update() isn't called if it
    // doesn't have focus.
    if (force_refresh || timestamp.GetSeconds() >= 1) {
        data.tm = {};
        data.type = {};
        data.status = {};
        data.strength = {};
        data.ip = {};
        data.battery = -1;
        // avoid divide by zero if getting the size fails, for whatever reason.
        data.sd_free = 1;
        data.sd_total = 1;
        data.emmc_free = 1;
        data.emmc_total = 1;

        const auto t = std::time(NULL);
        localtime_r(&t, &data.tm);
        nifmGetInternetConnectionStatus(&data.type, &data.strength, &data.status);
        nifmGetCurrentIpAddress(&data.ip);
        u32 battery{};
        if (serviceIsActive(psmGetServiceSession()) && R_SUCCEEDED(psmGetBatteryChargePercentage(&battery))) {
            data.battery = static_cast<int>(std::min<u32>(100, battery));
        }

        App::GetSdSize(&data.sd_free, &data.sd_total);
        App::GetEmmcSize(&data.emmc_free, &data.emmc_total);

        timestamp.Update();
    }

    return data;
}

MenuBase::MenuBase(const std::string& title, u32 flags) : m_title{title}, m_flags{flags} {
    // this->SetParent(this);
    this->SetPos(30, 87, 1220 - 30, 646 - 87);
    SetAction(Button::START, Action{App::Exit});
    SetNeonButtons();
}

MenuBase::~MenuBase() {
    if (m_shop_logo) {
        nvgDeleteImage(App::GetVg(), m_shop_logo);
    }
}

void MenuBase::Update(Controller* controller, TouchInfo* touch) {
    if (m_shop_account_action && touch->is_clicked && touch->in_range(744, 25, 160, 38)) {
        App::PlaySoundEffect(SoundEffect::Focus);
        m_shop_account_action();
        return;
    }
    Widget::Update(controller, touch);
}

void MenuBase::Draw(NVGcontext* vg, Theme* theme) {
    shop::Background(vg);
    if (!m_shop_logo_tried) {
        m_shop_logo_tried = true;
        if (R_SUCCEEDED(romfsInit())) {
            m_shop_logo = nvgCreateImage(vg, "romfs:/theme/ninmagic-shop-logo.png", 0);
            romfsExit();
        }
    }
    const auto pdata = GetPolledData();
    const auto status = m_shop_connected ? "Servidor conectado" : m_shop_connecting ? "Conectando…" :
        pdata.ip ? "Elige tu tienda" : "Sin conexión";
    shop::Header(vg, m_shop_logo, status, m_shop_connected, m_shop_connecting,
        pdata.battery, pdata.ip != 0, static_cast<bool>(m_shop_account_action), "v" APP_VERSION);
    if (!m_shop_layout) {
        nvgSave(vg);
        nvgIntersectScissor(vg, 137.f, 64.f, 580.f, 23.f);
        shop::Text(vg, 139.f, 76.f, 17.f, shop::Muted, m_title.c_str());
        nvgRestore(vg);
        if (!m_sub_heading.empty()) {
            nvgSave(vg); nvgIntersectScissor(vg, 54.f, 612.f, 950.f, 24.f);
            shop::Text(vg, 54.f, 624.f, 15.f, shop::Muted, m_sub_heading.c_str());
            nvgRestore(vg);
        }
    }
    if (!m_title_warning.empty()) {
        nvgSave(vg); nvgIntersectScissor(vg, 440.f, 65.f, 780.f, 20.f);
        shop::Text(vg, 1218.f, 76.f, 15.f, shop::Red, m_title_warning.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
    }
    if (!App::IsApplication()) {
        shop::Text(vg, 1220.f, 626.f, 13.f, shop::Amber,
            "Modo Álbum · abre con R para instalar", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    shop::Line(vg, 30.f, 638.f, 1250.f, 638.f, shop::Cyan, 1.f);
    shop::Line(vg, 30.f, 639.f, 1250.f, 639.f, shop::Alpha(shop::Cyan, .1f), 5.f);
    nvgBeginPath(vg); nvgMoveTo(vg, 1215, 709); nvgLineTo(vg, 1204, 709);
    nvgLineTo(vg, 1191, 719); nvgLineTo(vg, 1149, 719);
    nvgStrokeWidth(vg, 1.4f); nvgStrokeColor(vg, shop::Cyan); nvgStroke(vg);
    Widget::Draw(vg, theme);
}

} // namespace sphaira::ui::menu
