#pragma once

#include "ui/sidebar.hpp"
#include "utils/ownfoil_profile.hpp"
#include <string>
#include <vector>
#include <functional>

namespace sphaira::ownfoil {

void LoadConfigs(Configs& out_configs);
auto IsSafeAddress(const std::string& address) -> bool;
auto IsSameOrigin(const std::string& base, const std::string& target) -> bool;

// true for a locally generated stand-in uid. the `pending-` namespace is
// reserved by the API spec, so it can never collide with a real one.
auto IsPlaceholderUid(const std::string& uid) -> bool;

// creates or updates a saved server, keyed by config.uid (the ini section), and
// assigns a placeholder uid when it is empty. pass the entry's former uid as
// `previous_uid` so the old section is removed - the only way a key ever moves.
// Locally generated keys preserve separate URL/account profiles.
auto SaveConfig(Config& config, const std::string& previous_uid = {}) -> bool;

void DeleteConfig(const std::string& uid);
void SetFavorite(Config& config, const std::string& app_id, bool enabled);

// an empty uid matches nothing, and a placeholder never matches a real uid.
auto FindByUid(Configs& configs, const std::string& uid) -> Config*;

// the one way a saved entry is ever created or changed.
struct OwnfoilForm final : public ui::FormSidebar {
    using OnSaved = std::function<void(const Config&)>;

    explicit OwnfoilForm(const Config& config = {}, const OnSaved& on_saved = {});

private:
    void SetupButtons();

private:
    Config m_config{};
    const Config m_original{};
    std::optional<std::string> m_password_edit{};
    const OnSaved m_on_saved{};

    ui::SidebarEntryTextInput* m_name{};
    ui::SidebarEntryTextInput* m_local_address{};
    ui::SidebarEntryTextInput* m_remote_address{};
    ui::SidebarEntryTextInput* m_artwork_search_url{};
    ui::SidebarEntryTextInput* m_metadata_url{};
    ui::SidebarEntryTextInput* m_direct_catalog{};
    ui::SidebarEntryTextInput* m_port{};
    ui::SidebarEntryTextInput* m_path{};
    ui::SidebarEntryTextInput* m_user{};
    ui::SidebarEntryTextBase* m_pass{};
};

} // namespace sphaira::ownfoil
