// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::ownfoil {
enum class Backend { Auto, Native, JsonIndex, LegacyHtml };
struct Config {
    std::string uid, server_uid, name;
    std::string local_address;
    std::string artwork_search_url{"https://ninmagic.com/juegos"};
    std::string metadata_url, remote_address, user, pass;
    std::string direct_catalog{};
    Backend backend{Backend::Auto};
    std::vector<std::string> favorites;
    bool revalidate_artwork{}, legacy_html{}, legacy_headers{}, json_index{};
    std::string resolved_url;
};
using Configs = std::vector<Config>;

namespace profile {
inline constexpr std::size_t DirectCatalogNameLimit = 128;
inline constexpr std::string_view DirectCatalogDirectory = "/config/NinMagicShop/indices/";

// Only a basename can be joined to the microSD catalog directory. The empty
// profile setting means the server supplies its regular catalog.
inline bool ValidDirectCatalogName(std::string_view name) {
    constexpr std::string_view extension = ".json";
    constexpr std::string_view capitals = ".JSON";
    constexpr std::string_view allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
    if (name.size() <= extension.size() || name.size() > DirectCatalogNameLimit) return false;
    const auto start = name.size() - extension.size();
    if (name.substr(0, start).find_first_not_of(allowed) != std::string_view::npos) return false;
    for (std::size_t i = 0; i < extension.size(); ++i)
        if (name[start + i] != extension[i] && name[start + i] != capitals[i]) return false;
    return true;
}

struct Edit {
    std::string name, local_address, remote_address, user, metadata_url, artwork_search_url;
    std::string direct_catalog{};
    Backend backend{Backend::Auto};
    // nullopt means the password control was not changed; an explicitly empty
    // value clears it. The visible ******** placeholder never enters storage.
    std::optional<std::string> password;
};
inline auto Apply(const Config& original, const Edit& edit) -> Config {
    auto next = original;
    next.name = edit.name;
    next.local_address = edit.local_address;
    next.remote_address = edit.remote_address;
    next.user = edit.user;
    next.pass = edit.password.value_or(original.pass);
    next.metadata_url = edit.metadata_url;
    next.artwork_search_url = edit.artwork_search_url;
    next.direct_catalog = edit.direct_catalog;
    next.backend = edit.backend;
    const bool connection_changed = next.local_address != original.local_address || next.remote_address != original.remote_address ||
        next.user != original.user || next.pass != original.pass || next.backend != original.backend ||
        next.direct_catalog != original.direct_catalog;
    if (connection_changed) {
        next.resolved_url.clear();
        next.server_uid.clear();
        next.legacy_html = false;
        next.legacy_headers = false;
        next.json_index = false;
    }
    if (connection_changed || next.metadata_url != original.metadata_url || next.artwork_search_url != original.artwork_search_url)
        next.revalidate_artwork = true;
    // UID, favorites and every preference outside the edited fields retain
    // their original profile; another account's section is never re-keyed.
    return next;
}
template<class Writer>
bool WriteFields(const Config& config, Writer&& write) {
    if (!config.direct_catalog.empty() && !ValidDirectCatalogName(config.direct_catalog)) return false;
    const char* backend = config.backend == Backend::JsonIndex ? "json" : config.backend == Backend::Native ? "native" : config.backend == Backend::LegacyHtml ? "legacy" : "auto";
    for (const auto& field : std::vector<std::pair<std::string, std::string>>{
        {"server_uid", config.server_uid}, {"backend", backend}, {"name", config.name},
        {"local_address", config.local_address}, {"remote_address", config.remote_address},
        {"user", config.user}, {"pass", config.pass}, {"resolved_url", config.resolved_url},
        {"metadata_url", config.metadata_url}, {"artwork_search_url", config.artwork_search_url},
        {"direct_catalog", config.direct_catalog}}) {
        if (!write(config.uid, field.first, field.second)) return false;
    }
    return true;
}
}
}
