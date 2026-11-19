#include "utils/ownfoil.hpp"

#include "ui/sidebar.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <minIni.h>
#include <curl/curl.h>
#include <cctype>
#include <switch.h>

namespace sphaira::ownfoil {
namespace {

using namespace sphaira::ui;

#define OWNFOIL_INI_PATH "/config/NinMagicShop/ownfoil.ini"

constexpr const char* PLACEHOLDER_UID_PREFIX = "pending-";

// The local profile key remains stable even if the server identity changes.
auto GeneratePlaceholderUid() -> std::string {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%s%016lx", PLACEHOLDER_UID_PREFIX, randomGet64());
    return buf;
}

} // namespace

OwnfoilForm::OwnfoilForm(const Config& config, const OnSaved& on_saved)
: FormSidebar{"Conectar una tienda"}
, m_config{config}
, m_on_saved{on_saved} {
    SetupButtons();
}

void OwnfoilForm::SetupButtons() {
    m_name = this->Add<SidebarEntryTextInput>(
        "Nombre del perfil", m_config.name, "", "", -1, 32,
        "Nombre que aparecerá en tu lista de tiendas."
    );

    m_local_address = this->Add<SidebarEntryTextInput>(
        "URL del servidor", m_config.local_address, "", "", -1, PATH_MAX,
        "Dirección de tu Ownfoil. Incluye https:// y la ruta si usa una subcarpeta. El puerto y la ruta también se pueden editar debajo."
    );

    std::string port, path;
    if (!m_config.local_address.empty()) {
        const auto url = m_config.local_address.find("://") == std::string::npos ? "https://" + m_config.local_address : m_config.local_address;
        if (auto parsed = curl_url()) {
            if (curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK) {
                char* value{};
                if (curl_url_get(parsed, CURLUPART_PORT, &value, 0) == CURLUE_OK) { port = value; curl_free(value); }
                if (curl_url_get(parsed, CURLUPART_PATH, &value, 0) == CURLUE_OK) { path = value; curl_free(value); }
            }
            curl_url_cleanup(parsed);
        }
    }
    m_port = this->Add<SidebarEntryTextInput>("Puerto (opcional)", port, "", "", -1, 5,
        "Deja vacío para usar el puerto de la URL o el predeterminado (HTTPS 443 / HTTP 80).");
    m_path = this->Add<SidebarEntryTextInput>("Ruta del catálogo", path, "", "", -1, PATH_MAX,
        "Ejemplo: / o /mi-tienda/. Se conserva la ruta escrita en la URL si dejas este campo vacío.");

    m_remote_address = this->Add<SidebarEntryTextInput>(
        "URL alternativa (opcional)", m_config.remote_address, "", "", -1, PATH_MAX,
        "Optional second URL for the same server, for example its remote address."_i18n
    );

    m_artwork_search_url = this->Add<SidebarEntryTextInput>("Web de carátulas (búsqueda pública)", m_config.artwork_search_url, "", "", -1, PATH_MAX,
        "Busca por ID exacto en las tarjetas visibles. NinMagic publica sus carátulas aquí; puedes cambiar la URL o vaciarla. No recibe la contraseña de tu Ownfoil.");
    m_metadata_url = this->Add<SidebarEntryTextInput>("URL de carátulas web (opcional)", m_config.metadata_url, "", "", -1, PATH_MAX,
        "Si tu web publica metadatos JSON con carátulas, escribe su URL. Vacío usa la API de tu Ownfoil. Otro dominio se consulta sin enviar tu contraseña.");

    m_user = this->Add<SidebarEntryTextInput>(
        "Usuario", m_config.user, "", "", -1, PATH_MAX,
        "Optional: username used to log in to the server."_i18n
    );

    m_pass = this->Add<SidebarEntryTextBase>(
        "Contraseña", m_config.pass.empty() ? "" : "********", [this]() {
            SwkbdConfig keyboard{};
            if (R_FAILED(swkbdCreate(&keyboard, 0))) return;
            ON_SCOPE_EXIT(swkbdClose(&keyboard));
            swkbdConfigMakePresetDefault(&keyboard);
            swkbdConfigSetType(&keyboard, SwkbdType_All);
            swkbdConfigSetInitialText(&keyboard, m_config.pass.c_str());
            swkbdConfigSetStringLenMax(&keyboard, 256);
            keyboard.arg.arg.arg.passwordFlag = 1;
            char password[1025]{};
            if (R_SUCCEEDED(swkbdShow(&keyboard, password, sizeof(password)))) {
                m_config.pass = password;
                m_pass->SetValue(m_config.pass.empty() ? "" : "********");
            }
            std::fill(std::begin(password), std::end(password), 0);
        }, "Se usa la misma cuenta que en tu web/Ownfoil. Se guarda en esta microSD; la contraseña se oculta en pantalla."
    );

    this->Add<SidebarEntryArray>("Tipo de catálogo",
        SidebarEntryArray::Items{"Automático", "Ownfoil API", "Índice JSON", "Carpetas Ownfoil clásico"},
        [this](s64& index) { m_config.backend = static_cast<Backend>(index); },
        static_cast<s64>(m_config.backend), "Automático admite Ownfoil actual, índices JSON y carpetas de versiones anteriores.");

    const auto callback = this->Add<SidebarEntryCallback>("Guardar y conectar", [this, port, path](){
        auto primary = m_local_address->GetValue();
        if (!primary.empty() && (m_port->GetValue() != port || m_path->GetValue() != path)) {
            if (primary.find("://") == std::string::npos) primary = "https://" + primary;
            auto parsed = curl_url();
            if (!parsed) return;
            ON_SCOPE_EXIT(curl_url_cleanup(parsed));
            auto rc = curl_url_set(parsed, CURLUPART_URL, primary.c_str(), 0);
            if (rc == CURLUE_OK && m_port->GetValue() != port) rc = curl_url_set(parsed, CURLUPART_PORT, m_port->GetValue().empty() ? nullptr : m_port->GetValue().c_str(), 0);
            if (rc == CURLUE_OK && m_path->GetValue() != path && !m_path->GetValue().empty()) {
                auto route = m_path->GetValue();
                if (route.front() != '/') route = '/' + route;
                rc = curl_url_set(parsed, CURLUPART_PATH, route.c_str(), 0);
            }
            char* value{};
            if (rc != CURLUE_OK || curl_url_get(parsed, CURLUPART_URL, &value, 0) != CURLUE_OK) {
                App::Notify("Revisa la URL, el puerto y la ruta del catálogo.");
                return;
            }
            primary = value;
            curl_free(value);
        }
        const auto& alternative = m_remote_address->GetValue();
        if (!m_artwork_search_url->GetValue().empty() && !IsSafeAddress(m_artwork_search_url->GetValue())) {
            App::Notify("Revisa la URL pública de carátulas.");
            return;
        }
        if (!m_metadata_url->GetValue().empty() && !IsSafeAddress(m_metadata_url->GetValue())) {
            App::Notify("Revisa la URL de metadatos de tu web.");
            return;
        }
        if ((!primary.empty() && !IsSafeAddress(primary)) || (!alternative.empty() && !IsSafeAddress(alternative))) {
            App::Notify("Introduce una URL HTTP(S) válida. Escribe usuario y contraseña en sus campos.");
            return;
        }
        if (primary != m_config.local_address || alternative != m_config.remote_address) {
            m_config.resolved_url.clear();
            m_config.server_uid.clear();
        }
        m_config.name = m_name->GetValue();
        m_config.local_address = primary;
        m_config.remote_address = m_remote_address->GetValue();
        m_config.user = m_user->GetValue();
        m_config.metadata_url = m_metadata_url->GetValue();
        m_config.artwork_search_url = m_artwork_search_url->GetValue();

        // a brand new entry gets a placeholder uid until its first connect.
        SaveConfig(m_config);
        App::Notify("Perfil de tienda guardado.");

        if (m_on_saved) {
            m_on_saved(m_config);
        }

        this->SetPop();
    },  "Saves the Ownfoil server entry."_i18n);

    callback->Depends([this](){
        return
            !m_name->GetValue().empty() &&
            (!m_local_address->GetValue().empty() || !m_remote_address->GetValue().empty());
    }, "Escribe un nombre y al menos una URL.");
}

auto IsSafeAddress(const std::string& address) -> bool {
    if (address.empty() || address.find_first_of("\r\n\t #") != std::string::npos) return false;
    auto url = address.find("://") == std::string::npos ? "http://" + address : address;
    CURLU* parsed = curl_url();
    if (!parsed) return false;
    ON_SCOPE_EXIT(curl_url_cleanup(parsed));
    if (curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0) != CURLUE_OK) return false;
    char* value{};
    if (curl_url_get(parsed, CURLUPART_USER, &value, 0) == CURLUE_OK) {
        curl_free(value);
        return false;
    }
    if (curl_url_get(parsed, CURLUPART_SCHEME, &value, 0) != CURLUE_OK) return false;
    const bool allowed = !std::strcmp(value, "http") || !std::strcmp(value, "https");
    curl_free(value);
    return allowed;
}

auto IsSameOrigin(const std::string& base, const std::string& target) -> bool {
    if (!IsSafeAddress(base) || !IsSafeAddress(target)) return false;
    CURLU* a = curl_url();
    CURLU* b = curl_url();
    ON_SCOPE_EXIT(if (a) curl_url_cleanup(a); if (b) curl_url_cleanup(b));
    if (!a || !b || curl_url_set(a, CURLUPART_URL, base.c_str(), 0) != CURLUE_OK
        || curl_url_set(b, CURLUPART_URL, target.c_str(), 0) != CURLUE_OK) return false;
    for (const auto part : {CURLUPART_SCHEME, CURLUPART_HOST, CURLUPART_PORT}) {
        char *left{}, *right{};
        const auto l = curl_url_get(a, part, &left, CURLU_DEFAULT_PORT);
        const auto r = curl_url_get(b, part, &right, CURLU_DEFAULT_PORT);
        const auto same = l == CURLUE_OK && r == CURLUE_OK && !strcasecmp(left, right);
        curl_free(left);
        curl_free(right);
        if (!same) return false;
    }
    return true;
}

void LoadConfigs(Configs& out_configs) {
    static const auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto e = static_cast<Configs*>(UserData);
        if (!Section || !Key || !Value) {
            return 1;
        }

        // add new entry if the section (uid) changed.
        if (e->empty() || std::strcmp(Section, e->back().uid.c_str())) {
            e->emplace_back();
            e->back().uid = Section;
        }

        if (!std::strncmp(Key, "favorite_", 9)) {
            if (!std::strcmp(Value, "1")) e->back().favorites.emplace_back(Key + 9);
        } else if (!std::strcmp(Key, "server_uid")) {
            e->back().server_uid = Value;
        } else if (!std::strcmp(Key, "backend")) {
            e->back().backend = !std::strcmp(Value, "json") ? Backend::JsonIndex :
                !std::strcmp(Value, "native") ? Backend::Native : !std::strcmp(Value, "legacy") ? Backend::LegacyHtml : Backend::Auto;
        } else if (!std::strcmp(Key, "name")) {
            e->back().name = Value;
        } else if (!std::strcmp(Key, "local_address")) {
            e->back().local_address = Value;
        } else if (!std::strcmp(Key, "artwork_search_url")) {
            e->back().artwork_search_url = Value;
        } else if (!std::strcmp(Key, "metadata_url")) {
            e->back().metadata_url = Value;
        } else if (!std::strcmp(Key, "remote_address")) {
            e->back().remote_address = Value;
        } else if (!std::strcmp(Key, "user")) {
            e->back().user = Value;
        } else if (!std::strcmp(Key, "pass")) {
            e->back().pass = Value;
        } else if (!std::strcmp(Key, "resolved_url")) {
            e->back().resolved_url = Value;
        } else {
            log_write("[OWNFOIL] Ignoring unknown configuration key\n");
        }

        return 1;
    };

    out_configs.resize(0);
    ini_browse(cb, &out_configs, OWNFOIL_INI_PATH);
    log_write("[OWNFOIL] Found %zu server configs\n", out_configs.size());
}

auto IsPlaceholderUid(const std::string& uid) -> bool {
    return uid.starts_with(PLACEHOLDER_UID_PREFIX);
}

void SaveConfig(Config& config, const std::string& previous_uid) {
    if (config.uid.empty()) {
        config.uid = GeneratePlaceholderUid();
    }

    // the uid is the ini section, so replacing it is a delete plus a re-write.
    if (!previous_uid.empty() && previous_uid != config.uid) {
        log_write("[OWNFOIL] Updating local profile key\n");
        DeleteConfig(previous_uid);
    }

    // Profiles are kept separate even when they use the same server UID.
    fs::FsNativeSd().CreateDirectoryRecursively("/config/NinMagicShop/");
    ini_puts(config.uid.c_str(), "server_uid", config.server_uid.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "backend", config.backend == Backend::JsonIndex ? "json" : config.backend == Backend::Native ? "native" : config.backend == Backend::LegacyHtml ? "legacy" : "auto", OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "name", config.name.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "local_address", config.local_address.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "artwork_search_url", config.artwork_search_url.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "metadata_url", config.metadata_url.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "remote_address", config.remote_address.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "user", config.user.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "pass", config.pass.c_str(), OWNFOIL_INI_PATH);
    ini_puts(config.uid.c_str(), "resolved_url", config.resolved_url.c_str(), OWNFOIL_INI_PATH);
}

void DeleteConfig(const std::string& uid) {
    ini_puts(uid.c_str(), nullptr, nullptr, OWNFOIL_INI_PATH);
}

void SetFavorite(Config& config, const std::string& app_id, bool enabled) {
    if (config.uid.empty() || app_id.empty() || app_id.find_first_of("[]=\r\n") != std::string::npos) return;
    std::erase(config.favorites, app_id);
    if (enabled) config.favorites.push_back(app_id);
    const auto key = "favorite_" + app_id;
    ini_puts(config.uid.c_str(), key.c_str(), enabled ? "1" : nullptr, OWNFOIL_INI_PATH);
}

auto FindByUid(Configs& configs, const std::string& uid) -> Config* {
    if (uid.empty()) {
        return nullptr;
    }

    const auto it = std::find_if(configs.begin(), configs.end(), [&uid](const auto& e) {
        return e.uid == uid || e.server_uid == uid;
    });

    return it == configs.end() ? nullptr : &*it;
}

} // namespace sphaira::ownfoil
