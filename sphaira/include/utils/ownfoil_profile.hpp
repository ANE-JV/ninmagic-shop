// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::ownfoil {
enum class Backend { Auto, Native, JsonIndex, LegacyHtml };
struct Config {
    std::string uid, server_uid, name;
    std::string local_address;
    std::string artwork_search_url{"https://ninmagic.com/juegos"};
    std::string metadata_url, remote_address, user, pass;
    Backend backend{Backend::Auto};
    std::vector<std::string> favorites;
    bool revalidate_artwork{}, legacy_html{}, legacy_headers{}, json_index{};
    std::string resolved_url;
};
using Configs = std::vector<Config>;

namespace profile {
struct Edit {
    std::string name, local_address, remote_address, user, metadata_url, artwork_search_url;
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
    next.backend = edit.backend;
    const bool connection_changed = next.local_address != original.local_address || next.remote_address != original.remote_address ||
        next.user != original.user || next.pass != original.pass || next.backend != original.backend;
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
    const char* backend = config.backend == Backend::JsonIndex ? "json" : config.backend == Backend::Native ? "native" : config.backend == Backend::LegacyHtml ? "legacy" : "auto";
    for (const auto& field : std::vector<std::pair<std::string, std::string>>{
        {"server_uid", config.server_uid}, {"backend", backend}, {"name", config.name},
        {"local_address", config.local_address}, {"remote_address", config.remote_address},
        {"user", config.user}, {"pass", config.pass}, {"resolved_url", config.resolved_url},
        {"metadata_url", config.metadata_url}, {"artwork_search_url", config.artwork_search_url}}) {
        if (!write(config.uid, field.first, field.second)) return false;
    }
    return true;
}
}
}
