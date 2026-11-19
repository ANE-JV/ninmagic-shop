#include "utils/ownfoil_api.hpp"
#include "utils/ownfoil_index.hpp"

#include "download.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"

#include <yyjson.h>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <chrono>

namespace sphaira::ownfoil::api {
namespace {

// every address ends up as the bare root url of its server.
auto WithScheme(const char* scheme, const std::string& address) -> std::string {
    auto url = scheme + address;
    return url;
}

// A missing scheme means HTTPS. LAN installations can explicitly use http://;
// never retry an HTTPS credential request through cleartext automatically.
auto BuildUrls(const std::string& address) -> std::vector<std::string> {
    if (!IsSafeAddress(address)) return {};
    return {WithScheme(address.find("://") == std::string::npos ? "https://" : "", address)};
}

// the url that answered last time, as the entry remembers it: probing costs a
// full connect timeout per address that isn't there.
struct Resolved {
    std::string url{};
    bool used_remote{};
};

// honoured only while the entry's own addresses still produce it, so it can only
// reorder candidates the probe would try anyway - a re-pointed entry can never
// reach the machine it used to name.
auto ResolvedFrom(const Config& config) -> Resolved {
    if (config.resolved_url.empty()) {
        return {};
    }

    for (const auto used_remote : {false, true}) {
        const auto& address = used_remote ? config.remote_address : config.local_address;
        if (address.empty()) {
            continue;
        }
        for (const auto& url : BuildUrls(address)) {
            if (url == config.resolved_url) {
                return {config.resolved_url, used_remote};
            }
        }
    }

    return {};
}

// a toast draws one line, so a multi-line server message is folded into one.
auto Flatten(const std::string& str) -> std::string {
    std::string out;
    out.reserve(str.size());

    for (const auto c : str) {
        const auto ch = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        if (ch == ' ' && (out.empty() || out.back() == ' ')) {
            continue;
        }
        out += ch;
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

auto GetBool(yyjson_val* obj, const char* key) -> bool {
    const auto v = yyjson_obj_get(obj, key);
    return v && yyjson_is_bool(v) && yyjson_get_bool(v);
}

// empty when missing or null, which the shop's catalogue fields often are.
auto GetString(yyjson_val* obj, const char* key) -> std::string {
    const auto v = yyjson_obj_get(obj, key);
    return v && yyjson_is_str(v) ? yyjson_get_str(v) : "";
}

// a non-200 carries the server's own {"error": "..."}, but a reverse proxy in
// front of it answers for itself, so anything unparseable yields nothing.
auto ParseError(const std::vector<u8>& data) -> std::string {
    if (data.empty()) {
        return {};
    }

    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return {};
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return {};
    }

    if (const auto v = yyjson_obj_get(root, "error"); v && yyjson_is_str(v)) {
        return Flatten(yyjson_get_str(v));
    }

    return {};
}

auto ParseHandshake(const std::vector<u8>& data, ConnectResult& out) -> bool {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        out.error = "Could not parse the server's response"_i18n;
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        out.error = "Could not parse the server's response"_i18n;
        return false;
    }

    if (const auto v = yyjson_obj_get(root, "uid"); v && yyjson_is_str(v)) {
        out.info.uid = yyjson_get_str(v);
    }

    const auto name = yyjson_obj_get(root, "name");
    if (!name || !yyjson_is_str(name)) {
        out.error = "This does not look like an Ownfoil server"_i18n;
        return false;
    }
    out.info.name = yyjson_get_str(name);

    if (const auto v = yyjson_obj_get(root, "version"); v && yyjson_is_str(v)) {
        out.info.version = yyjson_get_str(v);
    }
    if (const auto v = yyjson_obj_get(root, "protocol_version"); v && yyjson_is_int(v)) {
        out.info.protocol_version = yyjson_get_sint(v);
    }
    if (const auto v = yyjson_obj_get(root, "motd"); v && yyjson_is_str(v)) {
        out.info.motd = yyjson_get_str(v);
    }
    out.info.is_public = GetBool(root, "public");

    // a shop that moved says so here, which is the only way a console off its
    // network can hear about it.
    if (const auto v = yyjson_obj_get(root, "remote"); v && yyjson_is_str(v)) {
        out.info.remote_address = yyjson_get_str(v);
    }

    if (const auto features = yyjson_obj_get(root, "features"); features && yyjson_is_obj(features)) {
        out.info.features.shop = GetBool(features, "shop");
        out.info.features.dumps_upload = GetBool(features, "dumps_upload");
        out.info.features.save_backup = GetBool(features, "save_backup");
        out.info.features.resumable_download = GetBool(features, "resumable_download");
        out.info.features.resumable_upload = GetBool(features, "resumable_upload");
    }

    return true;
}

// Resolve relative paths with RFC URL rules. A leading slash is relative to
// the origin, not to the configured reverse-proxy path.
auto ResolveUrl(const std::string& base, const std::string& relative) -> std::string {
    if (relative.empty()) return {};
    CURLU* parsed = curl_url();
    if (!parsed) return {};
    ON_SCOPE_EXIT(curl_url_cleanup(parsed));
    auto escaped = relative;
    size_t space{};
    while ((space = escaped.find(' ', space)) != std::string::npos) {
        escaped.replace(space, 1, "%20");
        space += 3;
    }
    if (curl_url_set(parsed, CURLUPART_URL, base.c_str(), 0) != CURLUE_OK
        || curl_url_set(parsed, CURLUPART_URL, escaped.c_str(), 0) != CURLUE_OK) return {};
    char* value{};
    if (curl_url_get(parsed, CURLUPART_URL, &value, 0) != CURLUE_OK) return {};
    std::string url = value;
    curl_free(value);
    return IsSafeAddress(url) ? url : std::string{};
}

using IndexCache = index::CatalogCache;
std::mutex g_index_mutex;
IndexCache g_index_cache;
std::mutex g_platform_mutex;
std::unordered_map<std::string, std::string> g_learned_platforms;

void LearnPlatform(const std::string& provider, const std::string& id, const std::string& platform) {
    if (platform.empty()) return;
    {
        std::scoped_lock lock(g_platform_mutex);
        if (g_learned_platforms.size() >= 32768) g_learned_platforms.clear();
        g_learned_platforms[provider + '#' + id] = platform;
    }
    std::scoped_lock lock(g_index_mutex);
    if (!g_index_cache.files) return;
    const auto matches = [&](const auto& file) { return file.id == id && (file.platform.empty() || file.platform_learned) && file.platform != platform; };
    if (std::none_of(g_index_cache.files->begin(), g_index_cache.files->end(), matches)) return;
    // Readers retain the prior immutable snapshot while the session learns a
    // console; no network crawl is needed and no reader sees a partial update.
    auto updated = std::make_shared<std::vector<index::File>>(*g_index_cache.files);
    for (auto& file : *updated) if (matches(file)) { file.platform = platform; file.platform_learned = true; }
    g_index_cache.files = updated;
}

struct ArtworkCache {
    std::string root, metadata_url, user, pass;
    std::shared_ptr<const std::vector<index::Artwork>> records;
};
std::mutex g_artwork_mutex;
ArtworkCache g_artwork_cache;

auto FormValue(const std::string& value) -> std::string {
    std::string encoded;
    constexpr char HEX[] = "0123456789ABCDEF";
    for (const auto c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' || byte == '~') encoded += c;
        else { encoded += '%'; encoded += HEX[byte >> 4]; encoded += HEX[byte & 15]; }
    }
    return encoded;
}

// Older Ownfoil's web already publishes the artwork at /api/titles, behind its
// existing /login session. Reuse the saved account; create no users or web data.
// A custom public JSON endpoint may be on another origin, which gets no login.
auto FetchWebArtwork(const std::string& directory, const Config& config, std::stop_token token,
    bool refresh, std::shared_ptr<const std::vector<index::Artwork>>& records) -> bool {
    auto root = config.resolved_url.empty() ? directory : config.resolved_url;
    if (!root.ends_with('/')) root += '/';
    auto endpoint = config.metadata_url.empty() ? ResolveUrl(root, "api/titles") : config.metadata_url;
    if (endpoint.find("://") == std::string::npos) endpoint = "https://" + endpoint;
    if (!IsSafeAddress(endpoint)) return false;
    {
        std::scoped_lock lock(g_artwork_mutex);
        if (!refresh && g_artwork_cache.records && g_artwork_cache.root == root && g_artwork_cache.metadata_url == endpoint
            && g_artwork_cache.user == config.user && g_artwork_cache.pass == config.pass) {
            records = g_artwork_cache.records;
            return true;
        }
    }
    constexpr s64 LIMIT = 32 * 1024 * 1024;
    const auto bounded = [](s64 total, s64 now, s64, s64) { return total <= LIMIT && now <= LIMIT; };
    const auto local = IsSameOrigin(root, endpoint);
    const auto request = [&](const std::string& cookie) {
        curl::Header headers;
        headers.m_map.emplace("Accept", "application/json");
        if (!cookie.empty()) headers.m_map.emplace("Cookie", cookie);
        return curl::Api().ToMemory(curl::Url{endpoint}, std::move(headers),
            curl::UserPass{local ? config.user : "", local ? config.pass : ""}, curl::PreemptiveAuth{local},
            curl::Flags{curl::Flag_KeepErrorBody | curl::Flag_NoRedirect}, curl::StopToken{token}, curl::OnProgress{bounded});
    };
    auto result = request({});
    if (token.stop_requested()) return false;
    if (local && config.metadata_url.empty() && result.code == 302 && (!config.user.empty() || !config.pass.empty())) {
        const auto login_url = ResolveUrl(root, "login");
        const auto fields = "user=" + FormValue(config.user) + "&password=" + FormValue(config.pass);
        const auto login = curl::Api().ToMemory(curl::Url{login_url}, curl::Fields{fields},
            curl::Header{{{"Content-Type", "application/x-www-form-urlencoded"}}},
            curl::Flags{curl::Flag_KeepErrorBody | curl::Flag_NoRedirect}, curl::StopToken{token}, curl::OnProgress{bounded});
        if (token.stop_requested() || login.code != 302) return false;
        const auto cookie_header = login.header.Find("Set-Cookie");
        if (cookie_header == login.header.m_map.end()) return false;
        // The cookie lives only for this metadata read, never in a file/cache.
        const auto cookie = cookie_header->second.substr(0, cookie_header->second.find(';'));
        if (cookie.empty() || cookie.find_first_of("\r\n") != std::string::npos) return false;
        result = request(cookie);
    }
    if (token.stop_requested() || !result.success || result.code != 200 || result.data.size() > LIMIT) return false;
    auto parsed = std::make_shared<std::vector<index::Artwork>>();
    if (!index::ParseArtwork(reinterpret_cast<const char*>(result.data.data()), result.data.size(), *parsed)) return false;
    for (auto& art : *parsed) {
        art.icon = ResolveUrl(endpoint, art.icon);
        art.banner = ResolveUrl(endpoint, art.banner);
    }
    records = parsed;
    std::scoped_lock lock(g_artwork_mutex);
    g_artwork_cache = {root, endpoint, config.user, config.pass, parsed};
    return true;
}

auto FetchIndex(const std::string& url, const Config& config, std::stop_token token,
    std::shared_ptr<const std::vector<index::File>>& files, std::string& error, bool refresh = false, bool* legacy_result = nullptr) -> bool {
    if (!refresh) {
        std::scoped_lock lock(g_index_mutex);
        if (g_index_cache.Matches(url, config.user, config.pass)) {
            files = g_index_cache.files;
            if (legacy_result) *legacy_result = g_index_cache.legacy;
            return true;
        }
    }
    // Catalogues are metadata, not packages: refuse an accidental file endpoint
    // before it can grow the metadata buffer to gigabytes.
    constexpr s64 MAX_INDEX_BYTES = 32 * 1024 * 1024;
    const auto request = [&](bool legacy) {
        return curl::Api().ToMemory(curl::Url{url}, curl::UserPass{config.user, config.pass},
            curl::PreemptiveAuth{true}, curl::Flags{static_cast<u32>(curl::Flag_KeepErrorBody | (legacy ? curl::Flag_LegacyOwnfoil : 0))}, curl::StopToken{token},
            curl::OnProgress{[](s64 total, s64 now, s64, s64) { return total <= MAX_INDEX_BYTES && now <= MAX_INDEX_BYTES; }});
    };
    bool legacy = config.legacy_html || config.backend == Backend::LegacyHtml;
    auto result = request(legacy);
    if (token.stop_requested()) return false;
    if (result.code == 401 || result.code == 403) {
        error = "Incorrect username or password"_i18n;
        return false;
    }
    auto parsed = std::make_shared<std::vector<index::File>>();
    bool parsed_ok{};
    if (!legacy && result.success && result.code == 200 && result.data.size() <= MAX_INDEX_BYTES) {
        parsed_ok = index::Parse(reinterpret_cast<const char*>(result.data.data()), result.data.size(), *parsed, error);
    }
    if (!legacy && !parsed_ok && config.backend != Backend::JsonIndex) {
        legacy = true;
        result = request(true);
    }
    if (!result.success || result.code != 200 || result.data.size() > MAX_INDEX_BYTES) {
        error = "Could not load the shop index (HTTP error or metadata size limit)"_i18n;
        return false;
    }
    if (legacy) parsed_ok = index::ParseHtml(reinterpret_cast<const char*>(result.data.data()), result.data.size(), *parsed, error);
    if (parsed_ok && (legacy || !config.metadata_url.empty())) {
        std::shared_ptr<const std::vector<index::Artwork>> artwork;
        if (FetchWebArtwork(url, config, token, refresh, artwork)) index::ApplyArtwork(*parsed, *artwork);
    }
    if (token.stop_requested()) return false;
    if (!parsed_ok) { error = i18n::get(error); return false; }
    error.clear();
    if (legacy_result) *legacy_result = legacy;
    const auto directory_base = legacy && !url.ends_with('/') ? url + '/' : url;
    for (auto& file : *parsed) {
        file.url = ResolveUrl(directory_base, file.url);
        file.icon = ResolveUrl(url, file.icon);
        file.banner = ResolveUrl(url, file.banner);
        if (file.platform.empty()) file.platform = index::PlatformFromPath(file.url);
    }
    {
        std::scoped_lock lock(g_platform_mutex);
        for (auto& file : *parsed) {
            const auto found = g_learned_platforms.find(config.artwork_search_url + '#' + file.id);
            if (file.platform.empty() && found != g_learned_platforms.end()) { file.platform = found->second; file.platform_learned = true; }
        }
    }
    std::erase_if(*parsed, [](const auto& file) { return file.url.empty(); });
    files = parsed;
    std::scoped_lock lock(g_index_mutex);
    g_index_cache = {url, config.user, config.pass, files, legacy};
    return true;
}

auto IndexId(const index::File& file) -> std::string {
    return index::Key(file);
}

auto IndexApp(const index::File& file, const std::string& base_url) -> ShopApp {
    ShopApp app;
    app.directory = file.directory;
    app.type = file.kind == index::Kind::Update ? AppType::Update : file.kind == index::Kind::Dlc ? AppType::Dlc : AppType::Base;
    app.app_id = IndexId(file);
    app.artwork_id = file.id;
    app.title_id = file.title_id.empty() ? app.app_id : file.title_id;
    app.name = file.directory ? "[+] " + file.name : file.name;
    app.publisher = file.publisher;
    app.platform = file.platform;
    app.platform_learned = file.platform_learned;
    app.version = file.version ? "v" + std::to_string(file.version) : "";
    app.icon = {file.icon, IsSameOrigin(base_url, file.icon)};
    app.banner = {file.banner, IsSameOrigin(base_url, file.banner)};
    app.download = {file.url, file.extension, file.size, file.sha256};
    return app;
}

auto Contains(const std::vector<std::string>& values, const std::string& id) -> bool {
    return std::find(values.begin(), values.end(), id) != values.end();
}

auto FetchIndexApps(const std::string& base_url, const Config& config, std::stop_token token,
    const CatalogQuery& query, std::vector<ShopApp>& out, s64& total, std::string& error) -> bool {
    std::shared_ptr<const std::vector<index::File>> files;
    if (!FetchIndex(base_url, config, token, files, error)) return false;
    std::vector<const index::File*> selected;
    std::unordered_map<std::string, size_t> by_id;
    const auto search = index::Lower(query.search);
    for (const auto& file : *files) {
        bool include{};
        const auto base = file.kind == index::Kind::Base || file.kind == index::Kind::Unknown;
        switch (query.category) {
            case Category::All: include = base; break;
            case Category::NewGames: include = base && !Contains(query.app_ids, file.id); break;
            case Category::Updates: include = file.kind == index::Kind::Update && Contains(query.app_ids, file.id); break;
            case Category::Dlc: include = file.kind == index::Kind::Dlc && Contains(query.title_ids, file.title_id) && !Contains(query.app_ids, file.id); break;
            case Category::Search: include = base && !search.empty() &&
                (index::Lower(file.name).find(search) != std::string::npos || index::Lower(file.id).find(search) != std::string::npos); break;
            case Category::Favorites: include = base && Contains(query.app_ids, IndexId(file)); break;
        }
        if (file.directory && query.category != Category::Search && query.category != Category::Favorites) include = true;
        if (!query.platform.empty()) {
            const auto matches = query.platform == "__unknown__" ? file.platform.empty() : file.platform == query.platform;
            include = include && (matches || (file.directory && file.platform.empty()));
        }
        if (!include) continue;
        const auto key = IndexId(file);
        if (const auto found = by_id.find(key); found != by_id.end()) {
            if (file.version > selected[found->second]->version) selected[found->second] = &file;
        } else {
            by_id.emplace(key, selected.size());
            selected.push_back(&file);
        }
    }
    // Generic indices have no uniform added/release date field. Preserve source
    // order for those choices instead of claiming fabricated dates.
    if (!std::strcmp(query.order_field, "NAME")) {
        std::stable_sort(selected.begin(), selected.end(), [](const auto a, const auto b) {
            if (a->directory != b->directory) return a->directory;
            return index::Lower(a->name) < index::Lower(b->name);
        });
    }
    if (query.descending) std::reverse(selected.begin(), selected.end());
    total = static_cast<s64>(selected.size());
    const auto count = std::clamp<s64>(query.page_size, 1, 1000);
    const auto begin = std::min(total, std::max<s64>(0, query.page - 1) * count);
    for (auto i = begin; i < std::min(total, begin + count); ++i) out.push_back(IndexApp(*selected[i], base_url));
    return true;
}

auto FetchIndexUpdates(const std::string& base_url, const Config& config, std::stop_token token,
    const std::vector<std::string>& ids, std::vector<ShopUpdate>& out, std::string& error) -> bool {
    std::shared_ptr<const std::vector<index::File>> files;
    if (!FetchIndex(base_url, config, token, files, error)) return false;
    std::unordered_map<std::string, size_t> newest;
    for (const auto& file : *files) {
        if (file.kind != index::Kind::Update || !Contains(ids, file.title_id)) continue;
        if (const auto found = newest.find(file.id); found != newest.end()) {
            out[found->second].version = std::max<s64>(out[found->second].version, file.version);
        } else {
            newest.emplace(file.id, out.size());
            out.push_back({file.id, file.title_id, file.version});
        }
    }
    return true;
}

auto FetchIndexTitle(const std::string& base_url, const Config& config, std::stop_token token,
    const std::string& id, ShopTitle& out, std::string& error) -> bool {
    std::shared_ptr<const std::vector<index::File>> files;
    if (!FetchIndex(base_url, config, token, files, error)) return false;
    bool found{};
    std::unordered_map<std::string, size_t> addons;
    for (const auto& file : *files) {
        const auto own_id = IndexId(file);
        if (own_id != id && file.title_id != id) continue;
        found = true;
        if (own_id == id) {
            out.name = file.name;
            out.publisher = file.publisher;
            out.banner = {file.banner, IsSameOrigin(base_url, file.banner)};
            out.full_banner = out.banner;
            out.download_size = file.size;
        }
        if (file.kind == index::Kind::Dlc && own_id != id) {
            if (const auto prior = addons.find(file.id); prior != addons.end()) {
                auto& row = out.dlc[prior->second];
                if (file.version > row.version) {
                    row.version = file.version;
                    row.download = {file.url, file.extension, file.size, file.sha256};
                }
            } else {
                addons.emplace(file.id, out.dlc.size());
                ShopContent row;
                row.app_id = file.id; row.name = file.name; row.version = file.version;
                row.banner = {file.banner, IsSameOrigin(base_url, file.banner)};
                row.download = {file.url, file.extension, file.size, file.sha256};
                out.dlc.push_back(std::move(row));
            }
        } else if (file.kind != index::Kind::Dlc) {
            const auto effective_version = file.kind == index::Kind::Unknown ? 0 : file.version;
            const auto base = file.kind == index::Kind::Base || file.kind == index::Kind::Unknown;
            const auto duplicate = std::find_if(out.versions.begin(), out.versions.end(), [&](const auto& version) { return version.version == effective_version && version.base == base; });
            if (duplicate == out.versions.end()) out.versions.push_back({effective_version, {}, {file.url, file.extension, file.size, file.sha256}, file.kind == index::Kind::Unknown ? "" : file.id, base, file.kind != index::Kind::Unknown && file.version_known});
            out.available_version = std::max<s64>(out.available_version, effective_version);
        }
    }
    std::sort(out.versions.begin(), out.versions.end(), [](const auto& a, const auto& b) { return a.base != b.base ? a.base : a.version < b.version; });
    if (!found) error = "This package is no longer in the index"_i18n;
    return found;
}

// Native API first; plain JSON fallback never drops the configured credentials.
auto TryConnect(const std::string& url, const Config& config, std::stop_token token, ConnectResult& out) -> bool {
    if (token.stop_requested() || !IsSafeAddress(url)) return false;
    log_write("[OWNFOIL] Connecting to configured server\n");
    if (config.backend != Backend::JsonIndex && config.backend != Backend::LegacyHtml) {
        const auto result = curl::Api().ToMemory(curl::Url{url}, curl::CustomRequest{"OPTIONS"},
            curl::UserPass{config.user, config.pass}, curl::PreemptiveAuth{true},
            curl::Flags{curl::Flag_KeepErrorBody}, curl::StopToken{token},
            curl::OnProgress{[](s64 total, s64 now, s64, s64) { return total <= 1024 * 1024 && now <= 1024 * 1024; }});
        out.reached = result.code != 0;
        if (result.code == 401 || result.code == 403) {
            out.refused = true;
            out.error = "Incorrect username or password"_i18n;
            return false;
        }
        if (result.success && result.code == 200 && ParseHandshake(result.data, out)) {
            out.base_url = url.ends_with('/') ? url : url + '/';
            out.success = true;
            return true;
        }
        if (config.backend == Backend::Native) {
            if (out.error.empty()) out.error = "Could not connect to the Ownfoil API"_i18n;
            return false;
        }
    }
    std::shared_ptr<const std::vector<index::File>> files;
    out.error.clear();
    if (!FetchIndex(url, config, token, files, out.error, true, &out.legacy_html)) return false;
    out.success = true;
    out.json_index = true;
    out.base_url = out.legacy_html && !url.ends_with('/') ? url + '/' : url;
    out.info = {};
    out.info.name = config.name;
    out.info.features.shop = true;
    return true;
}

// tries one address, over each scheme it could plausibly be served on.
auto TryAddress(const std::string& address, const Config& config, std::stop_token token, ConnectResult& out) -> bool {
    for (const auto& url : BuildUrls(address)) {
        ConnectResult attempt{};
        if (TryConnect(url, config, token, attempt)) {
            out = std::move(attempt);
            return true;
        }

        // once something has answered, its reply is the account of what went
        // wrong; trying another scheme only buries it.
        const auto reached = attempt.reached;
        if (reached || !out.reached) {
            out = std::move(attempt);
        }
        if (reached) {
            break;
        }
    }

    return false;
}

// the shop returns its own stored copies as a path relative to its root, so
// they only become fetchable joined onto the address that answered.
auto ParseImage(yyjson_val* obj, const std::string& base_url, ShopImage& out) -> void {
    if (!obj || !yyjson_is_obj(obj)) {
        return;
    }

    const auto url = yyjson_obj_get(obj, "url");
    if (!url || !yyjson_is_str(url) || !*yyjson_get_str(url)) {
        return;
    }

    const auto local = yyjson_obj_get(obj, "local");
    out.local = local && yyjson_is_bool(local) && yyjson_get_bool(local);
    out.url = yyjson_get_str(url);

    // base_url already carries its trailing '/'.
    out.url = ResolveUrl(base_url, out.url);
    out.local = out.local && IsSameOrigin(base_url, out.url);
}

// the same, for the Image stored under `key`.
auto ParseImage(yyjson_val* src, const char* key, const std::string& base_url, ShopImage& out) -> void {
    ParseImage(yyjson_obj_get(src, key), base_url, out);
}

// posts one graphql document. FAILONERROR turns an error status into a failed
// transfer, so the status is the thing to read first either way.
auto PostQuery(const std::string& url, const Config& config, std::stop_token token, const char* body, std::vector<u8>& data, std::string& error) -> bool {
    auto result = curl::Api().ToMemory(
        curl::Url{url},
        curl::Fields{body},
        curl::Header{{"Content-Type", "application/json"}},
        curl::UserPass{config.user, config.pass},
        curl::PreemptiveAuth{true},
        curl::Flags{curl::Flag_KeepErrorBody},
        curl::StopToken{token},
        curl::OnProgress{[](s64 total, s64 now, s64, s64) { return total <= 32 * 1024 * 1024 && now <= 32 * 1024 * 1024; }}
    );

    if (token.stop_requested()) {
        return false;
    }

    if (result.code && result.code != 200) {
        error = ParseError(result.data);
        if (error.empty()) {
            if (result.code == 401 || result.code == 403) {
                error = "Incorrect username or password"_i18n;
            } else {
                error = "Server returned an error"_i18n + " (" + std::to_string(result.code) + ")";
            }
        }
        return false;
    }

    if (!result.success) {
        error = "Could not reach the server"_i18n;
        return false;
    }

    data = std::move(result.data);
    return true;
}

// posts one document to the shop's graphql endpoint. `variables` is bare,
// un-braced `"key":value` json, as `IdArray`/`JsonString` produce.
auto FetchGraphql(const std::string& base_url, const Config& config, std::stop_token token, const std::string& query, const std::string& variables, std::vector<u8>& data, std::string& error) -> bool {
    const auto url = base_url + "api/graphql";
    const auto body = "{\"query\":\"" + query + "\",\"variables\":{" + variables + "}}";
    return PostQuery(url, config, token, body.c_str(), data, error);
}

// graphql answers its own failures with a 200, so every reply goes through this
// first and reports whatever the server put in `errors`.
auto OpenData(yyjson_doc* doc, std::string& error) -> yyjson_val* {
    const auto root = doc ? yyjson_doc_get_root(doc) : nullptr;
    if (!root || !yyjson_is_obj(root)) {
        error = "Could not parse the server's response"_i18n;
        return nullptr;
    }

    if (const auto errors = yyjson_obj_get(root, "errors"); errors && yyjson_is_arr(errors) && yyjson_arr_size(errors)) {
        const auto first = yyjson_arr_get_first(errors);
        const auto message = first ? yyjson_obj_get(first, "message") : nullptr;
        error = message && yyjson_is_str(message) ? Flatten(yyjson_get_str(message)) : "The server rejected the request"_i18n;
        return nullptr;
    }

    const auto data_obj = yyjson_obj_get(root, "data");
    if (!data_obj || !yyjson_is_obj(data_obj)) {
        error = "Unexpected response from the server"_i18n;
        return nullptr;
    }

    return data_obj;
}

// unwraps the {data:{apps:{items}}} envelope a page arrives in.
auto OpenApps(yyjson_doc* doc, s64& total, std::string& error) -> yyjson_val* {
    const auto data_obj = OpenData(doc, error);
    if (!data_obj) {
        return nullptr;
    }

    const auto conn = yyjson_obj_get(data_obj, "apps");
    const auto items = conn && yyjson_is_obj(conn) ? yyjson_obj_get(conn, "items") : nullptr;
    if (!items || !yyjson_is_arr(items)) {
        error = "Unexpected response from the server"_i18n;
        return nullptr;
    }

    if (const auto v = yyjson_obj_get(conn, "total"); v && yyjson_is_int(v)) {
        total = yyjson_get_sint(v);
    }

    return items;
}

// the newest update the shop can serve, falling back to what the base file
// reports. either can be absent until metadata extraction has run, and a title
// naming no version is left blank: the raw number is 0 on nearly every game.
auto ParseVersion(yyjson_val* item) -> std::string {
    for (const auto src : {yyjson_obj_get(item, "latestOwnedVersion"), item}) {
        if (!src || !yyjson_is_obj(src)) {
            continue;
        }
        if (const auto v = yyjson_obj_get(src, "displayVersion"); v && yyjson_is_str(v) && *yyjson_get_str(v)) {
            return "v" + std::string{yyjson_get_str(v)};
        }
    }
    return {};
}

// a path under the shop's own root, as its images are, so joined onto it.
auto ParseDownload(yyjson_val* item, const std::string& base_url) -> ShopDownload {
    ShopDownload out{};
    out.url = GetString(item, "downloadUrl");
    out.extension = index::Lower(GetString(item, "downloadExtension"));
    if (out.extension != "nsp" && out.extension != "nsz" && out.extension != "xci" && out.extension != "xcz") out.extension.clear();
    out.url = ResolveUrl(base_url, out.url);
    out.size = index::Integer(item, "downloadSize");
    return out;
}

// reads one page of the apps connection into `out`.
auto ParseApps(const std::vector<u8>& data, const std::string& base_url, AppType type, std::vector<ShopApp>& out, s64& total, std::string& error) -> bool {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto items = OpenApps(doc, total, error);
    if (!items) {
        return false;
    }

    size_t idx, max;
    yyjson_val* item;
    yyjson_arr_foreach(items, idx, max, item) {
        if (!yyjson_is_obj(item)) {
            continue;
        }

        ShopApp app{};
        app.type = type;
        app.app_id = GetString(item, "appId");
        app.artwork_id = app.app_id;
        // only update and dlc cards ask: a base game's app id is its title id.
        app.title_id = GetString(item, "titleId");
        if (app.title_id.empty()) {
            app.title_id = app.app_id;
        }
        app.version = ParseVersion(item);
        // only a dlc card asks for it.
        app.download = ParseDownload(item, base_url);

        // `titledb` is the app's own catalogue row, `title` the game's. a base
        // game's app id *is* its title id, so that query asks for only the
        // first; updates and dlc need both, each filling what the other lacks.
        for (const auto key : {"titledb", "title"}) {
            const auto src = yyjson_obj_get(item, key);
            if (!src || !yyjson_is_obj(src)) {
                continue;
            }

            if (app.name.empty()) {
                if (const auto v = yyjson_obj_get(src, "name"); v && yyjson_is_str(v)) {
                    app.name = yyjson_get_str(v);
                }
            }
            if (app.publisher.empty()) {
                if (const auto v = yyjson_obj_get(src, "publisher"); v && yyjson_is_str(v)) {
                    app.publisher = yyjson_get_str(v);
                }
            }
            if (app.icon.url.empty()) {
                ParseImage(src, "icon", base_url, app.icon);
            }
            if (app.banner.url.empty()) {
                ParseImage(src, "banner", base_url, app.banner);
            }
        }

        // the dlc's own name won above, so its game's is kept apart for the page
        // that says what the dlc needs.
        if (type == AppType::Dlc) {
            if (const auto game = yyjson_obj_get(item, "title"); game && yyjson_is_obj(game)) {
                app.game_name = GetString(game, "name");
            }
        }

        if (app.name.empty()) {
            app.name = app.app_id;
        }

        out.emplace_back(std::move(app));
    }

    return true;
}

// reads the id-and-version rows the update pre-pass asks for.
auto ParseUpdates(const std::vector<u8>& data, std::vector<ShopUpdate>& out, s64& total, std::string& error) -> bool {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto items = OpenApps(doc, total, error);
    if (!items) {
        return false;
    }

    size_t idx, max;
    yyjson_val* item;
    yyjson_arr_foreach(items, idx, max, item) {
        if (!yyjson_is_obj(item)) {
            continue;
        }

        ShopUpdate update{};
        if (const auto v = yyjson_obj_get(item, "appId"); v && yyjson_is_str(v)) {
            update.app_id = yyjson_get_str(v);
        }
        if (const auto v = yyjson_obj_get(item, "titleId"); v && yyjson_is_str(v)) {
            update.title_id = yyjson_get_str(v);
        }
        if (const auto v = yyjson_obj_get(item, "appVersion"); v && yyjson_is_int(v)) {
            update.version = yyjson_get_sint(v);
        }

        out.emplace_back(std::move(update));
    }

    return true;
}

// reads the one title a page asks for.
auto ParseTitle(const std::vector<u8>& data, const std::string& base_url, ShopTitle& out, std::string& error) -> bool {
    auto doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), YYJSON_READ_NOFLAG);
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    const auto data_obj = OpenData(doc, error);
    if (!data_obj) {
        return false;
    }

    const auto title = yyjson_obj_get(data_obj, "title");
    if (!title || !yyjson_is_obj(title)) {
        error = "The shop has no details for this title"_i18n;
        return false;
    }

    out.name = GetString(title, "name");
    out.publisher = GetString(title, "publisher");
    ParseImage(title, "banner", base_url, out.banner);
    out.intro = GetString(title, "intro");
    out.description = GetString(title, "description");
    out.release_date = GetString(title, "releaseDate");
    out.players = GetString(title, "numberOfPlayers");
    out.rating = GetString(title, "rating");
    out.region = GetString(title, "region");
    // a string in the catalogue, not reliably numeric: unparseable means no size.
    out.size = std::strtoll(GetString(title, "size").c_str(), nullptr, 10);

    size_t idx, max;
    yyjson_val* val;

    if (const auto categories = yyjson_obj_get(title, "category"); categories && yyjson_is_arr(categories)) {
        yyjson_arr_foreach(categories, idx, max, val) {
            if (!yyjson_is_str(val)) {
                continue;
            }
            if (!out.genre.empty()) {
                out.genre += ", ";
            }
            out.genre += yyjson_get_str(val);
        }
    }

    ParseImage(title, "fullBanner", base_url, out.full_banner);

    const auto parse_images = [&](const char* key, std::vector<ShopImage>& images) {
        const auto arr = yyjson_obj_get(title, key);
        if (!arr || !yyjson_is_arr(arr)) {
            return;
        }

        size_t i, n;
        yyjson_val* element;
        yyjson_arr_foreach(arr, i, n, element) {
            ShopImage image{};
            ParseImage(element, base_url, image);
            if (!image.url.empty()) {
                images.emplace_back(std::move(image));
            }
        }
    };

    parse_images("screenshots", out.screenshots);
    parse_images("full", out.full_screenshots);

    // ascending, so the last is the newest the catalogue knows of.
    if (const auto versions = yyjson_obj_get(title, "availableVersions"); versions && yyjson_is_arr(versions)) {
        if (const auto last = yyjson_arr_get_last(versions); last && yyjson_is_obj(last)) {
            if (const auto v = yyjson_obj_get(last, "version"); v && yyjson_is_int(v)) {
                out.latest_version = yyjson_get_sint(v);
                out.latest_date = GetString(last, "releaseDate");
            }
        }
    }

    if (const auto base = yyjson_obj_get(title, "base"); base && yyjson_is_arr(base)) {
        yyjson_arr_foreach(base, idx, max, val) {
            if (!yyjson_is_obj(val)) {
                continue;
            }

            if (const auto v = yyjson_obj_get(val, "downloadSize"); v && yyjson_is_int(v)) {
                out.download_size = yyjson_get_sint(v);
            }

            // the base game is version 0, all the shop offers until it holds an
            // update, and the first thing the version picker lists.
            out.available_version = 0;
            out.available_display = GetString(val, "displayVersion");
            // the shop answers with a row per file, so a base game cataloged
            // twice - an original and a compressed copy - is still one version.
            if (out.versions.empty()) {
                out.versions.emplace_back(ShopVersion{0, out.available_display, ParseDownload(val, base_url), GetString(val, "appId"), true});
            }
            if (const auto owned = yyjson_obj_get(val, "latestOwnedVersion"); owned && yyjson_is_obj(owned)) {
                if (const auto v = yyjson_obj_get(owned, "version"); v && yyjson_is_int(v)) {
                    out.available_version = yyjson_get_sint(v);
                    out.available_display = GetString(owned, "displayVersion");
                }
            }
        }
    }

    // what the install menu offers a choice of: the base game at version 0,
    // then each update the shop holds. the shop returns a row per version it
    // holds, so a version held as two files arrives twice and is listed once.
    if (const auto updates = yyjson_obj_get(title, "updates"); updates && yyjson_is_arr(updates)) {
        yyjson_arr_foreach(updates, idx, max, val) {
            if (!yyjson_is_obj(val)) {
                continue;
            }

            const auto v = yyjson_obj_get(val, "appVersion");
            if (!v || !yyjson_is_int(v)) {
                continue;
            }

            const auto version = yyjson_get_sint(v);
            const auto seen = std::any_of(out.versions.begin(), out.versions.end(), [version](const auto& e) {
                return e.version == version;
            });
            if (!seen) {
                out.versions.emplace_back(ShopVersion{version, GetString(val, "displayVersion"), ParseDownload(val, base_url), GetString(val, "appId"), false});
            }
        }
    }

    // the shop answers in no particular order; the picker offers oldest first.
    std::sort(out.versions.begin(), out.versions.end(), [](const auto& a, const auto& b) {
        return a.version < b.version;
    });

    // a row per version held, so a dlc held at two arrives twice and is listed
    // once, with the newer to install.
    if (const auto dlc = yyjson_obj_get(title, "dlc"); dlc && yyjson_is_arr(dlc)) {
        yyjson_arr_foreach(dlc, idx, max, val) {
            if (!yyjson_is_obj(val)) {
                continue;
            }

            ShopContent content{};
            content.app_id = GetString(val, "appId");
            if (content.app_id.empty()) {
                continue;
            }

            if (const auto v = yyjson_obj_get(val, "appVersion"); v && yyjson_is_int(v)) {
                content.version = yyjson_get_sint(v);
            }
            content.download = ParseDownload(val, base_url);

            const auto seen = std::find_if(out.dlc.begin(), out.dlc.end(), [&](const auto& e) {
                return e.app_id == content.app_id;
            });
            if (seen != out.dlc.end()) {
                if (content.version > seen->version) {
                    seen->version = content.version;
                    seen->download = std::move(content.download);
                }
                continue;
            }

            if (const auto db = yyjson_obj_get(val, "titledb"); db && yyjson_is_obj(db)) {
                content.name = GetString(db, "name");
                content.intro = GetString(db, "intro");
                ParseImage(db, "banner", base_url, content.banner);
            }
            if (content.name.empty()) {
                content.name = content.app_id;
            }

            out.dlc.emplace_back(std::move(content));
        }
    }

    return true;
}

// every id was formatted here from a u64, so there is nothing to escape.
auto IdArray(const std::vector<std::string>& ids) -> std::string {
    std::string out{"["};
    for (const auto& id : ids) {
        if (out.size() > 1) {
            out += ',';
        }
        out += '"';
        out += id;
        out += '"';
    }
    out += ']';
    return out;
}

// text somebody typed, so escaped rather than merely quoted. json takes utf-8
// raw, leaving the quote, the backslash and the control characters.
auto JsonString(const std::string& str) -> std::string {
    std::string out{"\""};
    for (const auto c : str) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", u);
            out += escaped;
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

// what a card draws, per category, and nothing beyond it. a base game's own
// catalogue row carries the lot; an update's app id has no row, so its game's
// fills in, and `titleId` is what opens that game's page; a dlc has a row but no
// icon in it, and carries its game's `titleId` for the installed check plus its
// own download, which the shop won't answer for under a dlc's id. artwork is
// THUMB, the size a card draws it at.
constexpr const char* BASE_FIELDS = "appId displayVersion latestOwnedVersion{displayVersion} titledb{name publisher icon(size:THUMB){url local} banner(size:THUMB){url local}}";
constexpr const char* UPDATE_FIELDS = "appId titleId displayVersion title{name publisher icon(size:THUMB){url local} banner(size:THUMB){url local}}";
constexpr const char* DLC_FIELDS = "appId titleId displayVersion downloadUrl downloadExtension titledb{name publisher banner(size:THUMB){url local}} title{name icon(size:THUMB){url local}}";

// what a page draws beyond its card, at CLIENT for the page and SCREEN for the
// viewer. `availableVersions` is the catalogue's ascending list, so its last is
// the newest version there is, while the base app's `latestOwnedVersion` is the
// newest the shop holds - and only a held version reports a display string, which
// is why both are asked. base, updates and dlc are separate aliases, each asking
// for what is read of it: a row per update for the install menu's picker, a dlc's
// row fields at THUMB, and a download apiece for Install.
// note this needs a shop with the aliased-nested-selection fix: an older one
// hydrates every alias with the first's fields and the dlc arrive nameless.
constexpr const char* TITLE_QUERY = "query($id:ID!){title(titleId:$id){name publisher intro description releaseDate category numberOfPlayers size rating region banner(size:CLIENT){url local} fullBanner:banner(size:SCREEN){url local} availableVersions{version releaseDate} screenshots(size:CLIENT){url local} full:screenshots(size:SCREEN){url local} base:apps(owned:true,appType:[BASE]){appId displayVersion downloadUrl downloadExtension downloadSize latestOwnedVersion{version displayVersion}} updates:apps(owned:true,appType:[UPDATE]){appId appVersion displayVersion downloadUrl downloadExtension} dlc:apps(owned:true,appType:[DLC]){appId appVersion downloadUrl downloadExtension titledb{name intro banner(size:THUMB){url local}}}}}";

// the update pre-pass: ids and versions only, for exactly the title ids asked.
constexpr const char* UPDATES_QUERY = "query($page:Int!,$pageSize:Int!,$titleIds:[String!]){apps(owned:true,appType:[UPDATE],groupByAppId:true,filter:{titleId:{in:$titleIds}},page:$page,pageSize:$pageSize){total items{appId titleId appVersion}}}";

// one shape for every category, with the content type, fields and filter each
// needs. the ids ride as variables, not in the document, so the shop's parser
// and validation caches keep hitting however long the console's library gets.
// fills `variables` with this page's, in `FetchGraphql`'s bare-json shape.
auto BuildPageQuery(const CatalogQuery& query, std::string& variables) -> std::string {
    const char* app_type{"BASE"};
    const char* fields{BASE_FIELDS};
    std::string params{"$page:Int!,$pageSize:Int!"};
    // the arguments this category adds to the apps() call, spliced in whole.
    std::string args{};
    variables = "\"page\":" + std::to_string(query.page) + ",\"pageSize\":" + std::to_string(query.page_size);

    switch (query.category) {
        case Category::Favorites:
            params += ",$appIds:[String!]";
            args = ",filter:{appId:{in:$appIds}}";
            variables += ",\"appIds\":" + IdArray(query.app_ids);
            break;
        case Category::All:
            break;

        case Category::NewGames:
            params += ",$appIds:[String!]";
            args = ",filter:{appId:{notIn:$appIds}}";
            variables += ",\"appIds\":" + IdArray(query.app_ids);
            break;

        case Category::Updates:
            app_type = "UPDATE";
            fields = UPDATE_FIELDS;
            params += ",$appIds:[String!]";
            args = ",filter:{appId:{in:$appIds}}";
            variables += ",\"appIds\":" + IdArray(query.app_ids);
            break;

        case Category::Dlc:
            app_type = "DLC";
            fields = DLC_FIELDS;
            params += ",$titleIds:[String!],$appIds:[String!]";
            args = ",filter:{titleId:{in:$titleIds},appId:{notIn:$appIds}}";
            variables += ",\"titleIds\":" + IdArray(query.title_ids) + ",\"appIds\":" + IdArray(query.app_ids);
            break;

        // an argument of its own, not a filter field: it is an OR across the
        // name and either id, which a filter's all-AND fields can't express.
        case Category::Search:
            params += ",$search:String";
            args = ",search:$search";
            variables += ",\"search\":" + JsonString(query.search);
            break;
    }

    // `total` is cheap enough for every page to ask: a HAVING would make the
    // server count a grouped query as a derived table, but `owned:true` is
    // filtered before the grouping, so it is a flat COUNT(DISTINCT appId).
    return "query(" + params + "){apps(owned:true,appType:[" + app_type + "],groupByAppId:true" + args
        + ",orderBy:{field:" + query.order_field + ",direction:" + (query.descending ? "DESC" : "ASC")
        + "},page:$page,pageSize:$pageSize){total items{" + fields + "}}}";
}

} // namespace

struct PublicArtworkCacheEntry {
    ShopApp app;
    bool found{};
    std::chrono::steady_clock::time_point saved;
};
std::mutex g_public_artwork_mutex;
std::unordered_map<std::string, PublicArtworkCacheEntry> g_public_artwork_cache;
std::chrono::steady_clock::time_point g_public_artwork_next{};

auto LookupPublicArtwork(const std::string& provider, const std::string& id, std::stop_token token, ShopApp& out) -> bool {
    ON_SCOPE_EXIT(if (!out.platform.empty()) LearnPlatform(provider, id, out.platform));
    if (token.stop_requested() || provider.empty() || id.size() != 16 || index::Id(id) != id || !IsSafeAddress(provider)) return false;
    const auto key = provider + '#' + id;
    {
        std::scoped_lock lock(g_public_artwork_mutex);
        const auto found = g_public_artwork_cache.find(key);
        if (found != g_public_artwork_cache.end() && std::chrono::steady_clock::now() - found->second.saved < std::chrono::minutes(15)) {
            out = found->second.app;
            return found->second.found;
        }
    }
    std::chrono::steady_clock::time_point permitted;
    {
        std::scoped_lock lock(g_public_artwork_mutex);
        permitted = std::max(std::chrono::steady_clock::now(), g_public_artwork_next);
        g_public_artwork_next = permitted + std::chrono::milliseconds(500);
    }
    while (!token.stop_requested() && std::chrono::steady_clock::now() < permitted) svcSleepThread(50'000'000);
    if (token.stop_requested()) return false;
    const auto url = provider + (provider.find('?') == std::string::npos ? "?q=" : "&q=") + id;
    const auto response = curl::Api().ToMemory(curl::Url{url}, curl::Flags{curl::Flag_NoRedirect}, curl::StopToken{token},
        curl::OnProgress{[](s64 total, s64 now, s64, s64) { return total <= 2 * 1024 * 1024 && now <= 2 * 1024 * 1024; }});
    if (token.stop_requested() || !response.success || response.code != 200 || response.data.size() > 2 * 1024 * 1024) return false;
    std::vector<index::Artwork> records;
    if (!index::ParseArtwork(reinterpret_cast<const char*>(response.data.data()), response.data.size(), records)) return false;
    bool matched{};
    for (const auto& record : records) {
        if (record.id != id) continue;
        // An exact ID is required even when a search page offered nearby titles.
        out.icon = {ResolveUrl(provider, record.icon), false};
        out.banner = {ResolveUrl(provider, record.banner), false};
        if (out.icon.url.empty()) out.icon = out.banner;
        if (out.banner.url.empty()) out.banner = out.icon;
        out.platform = record.platform;
        matched = !out.icon.url.empty() || !out.platform.empty();
        break;
    }
    std::scoped_lock lock(g_public_artwork_mutex);
    if (g_public_artwork_cache.size() >= 1024) g_public_artwork_cache.clear();
    g_public_artwork_cache[key] = {out, matched, std::chrono::steady_clock::now()};
    return matched;
}

void InvalidateCatalog(const std::string& base_url, const Config& config) {
    std::scoped_lock lock(g_index_mutex, g_artwork_mutex, g_public_artwork_mutex);
    std::erase_if(g_public_artwork_cache, [&config](const auto& entry) { return entry.first.starts_with(config.artwork_search_url + '#'); });
    g_index_cache.Invalidate(base_url, config.user, config.pass);
    if (g_artwork_cache.user == config.user && g_artwork_cache.pass == config.pass) g_artwork_cache = {};
}

auto CachedPlatforms(const std::string& base_url, const Config& config) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::scoped_lock lock(g_index_mutex);
    if (!g_index_cache.files || g_index_cache.url != base_url || g_index_cache.user != config.user || g_index_cache.pass != config.pass) return out;
    for (const auto& file : *g_index_cache.files) {
        const auto label = file.platform.empty() ? std::string{"__unknown__"} : file.platform;
        if (std::find(out.begin(), out.end(), label) == out.end()) out.push_back(label);
    }
    std::sort(out.begin(), out.end());
    return out;
}

auto Connect(const Config& config, std::stop_token token) -> ConnectResult {
    ConnectResult out{};
    out.error = "No address configured"_i18n;

    // whatever answered last time goes first, so a reconnect costs one request
    // rather than a walk down every address and scheme again.
    if (const auto resolved = ResolvedFrom(config); !resolved.url.empty()) {
        ConnectResult attempt{};
        if (TryConnect(resolved.url, config, token, attempt)) {
            attempt.used_remote = resolved.used_remote;
            return attempt;
        }

        if (attempt.refused) {
            return attempt;
        }
    }

    if (!config.local_address.empty() && TryAddress(config.local_address, config, token, out)) {
        out.used_remote = false;
        return out;
    }

    // the remote address is the same shop and would answer the same way, while
    // whatever proxies it may answer less helpfully and bury the reason.
    if (out.refused) {
        return out;
    }

    if (!config.remote_address.empty()) {
        ConnectResult remote_attempt{};
        if (TryAddress(config.remote_address, config, token, remote_attempt)) {
            remote_attempt.used_remote = true;
            return remote_attempt;
        }

        // whichever address got an answer explains the failure better than the
        // one that stayed silent.
        if (remote_attempt.reached || !out.reached) {
            return remote_attempt;
        }
    }

    return out;
}

auto FetchApps(const std::string& base_url, const Config& config, std::stop_token token, const CatalogQuery& query, std::vector<ShopApp>& out, s64& total, std::string& error) -> bool {
    out.clear();
    total = 0;
    if (config.json_index) return FetchIndexApps(base_url, config, token, query, out, total, error);

    // an empty list is no constraint to the shop, so a category narrowed by one
    // the console can't supply would answer with the whole catalog rather than
    // the nothing it is; an empty search term matches everything the same way.
    // New games is the exception: with nothing installed, every game is new.
    if ((query.category == Category::Favorites && query.app_ids.empty())
        || (query.category == Category::Updates && query.app_ids.empty())
        || (query.category == Category::Dlc && query.title_ids.empty())
        || (query.category == Category::Search && query.search.empty())) {
        return true;
    }

    std::string variables;
    const auto doc = BuildPageQuery(query, variables);

    log_write("[OWNFOIL] fetching apps page %d (%d per page, category %d, %s %s)\n",
        static_cast<int>(query.page), static_cast<int>(query.page_size),
        static_cast<int>(query.category), query.order_field, query.descending ? "desc" : "asc");

    std::vector<u8> data;
    if (!FetchGraphql(base_url, config, token, doc, variables, data, error)) {
        return false;
    }

    // each category asks for one kind of content, so the query says what every
    // card on the page is.
    const auto type = query.category == Category::Updates ? AppType::Update
        : query.category == Category::Dlc ? AppType::Dlc
        : AppType::Base;
    return ParseApps(data, base_url, type, out, total, error);
}

auto FetchUpdates(const std::string& base_url, const Config& config, std::stop_token token, const std::vector<std::string>& title_ids, std::vector<ShopUpdate>& out, std::string& error) -> bool {
    out.clear();
    if (config.json_index) return FetchIndexUpdates(base_url, config, token, title_ids, out, error);

    // as above: asking with an empty list would name every update in the shop.
    if (title_ids.empty()) {
        return true;
    }

    const auto ids = IdArray(title_ids);

    // bounded by the console's library rather than the shop, but pageSize is
    // capped server-side all the same, so it is paged.
    static constexpr s64 PAGE_SIZE = 1000;

    for (s64 page = 1; page <= 100; page++) {
        const auto variables = "\"page\":" + std::to_string(page) + ",\"pageSize\":" + std::to_string(PAGE_SIZE) + ",\"titleIds\":" + ids;

        std::vector<u8> data;
        if (!FetchGraphql(base_url, config, token, UPDATES_QUERY, variables, data, error)) {
            return false;
        }

        const auto before = out.size();
        s64 total{};
        if (!ParseUpdates(data, out, total, error)) {
            return false;
        }

        // a page that added nothing would loop forever on a total that
        // disagrees with what the shop is sending.
        if (out.size() == before || static_cast<s64>(out.size()) >= total) {
            break;
        }
        if (page == 100) { error = "El catálogo de actualizaciones supera el límite de seguridad."; return false; }
    }

    log_write("[OWNFOIL] shop has updates for %zu of %zu installed titles\n", out.size(), title_ids.size());
    return true;
}

auto FetchTitle(const std::string& base_url, const Config& config, std::stop_token token, const std::string& id, ShopTitle& out, std::string& error) -> bool {
    out = {};

    if (config.json_index) return FetchIndexTitle(base_url, config, token, id, out, error);
    log_write("[OWNFOIL] Fetching title metadata\n");

    // the id rides as a variable, so every page asks the one document the shop's
    // caches already hold.
    const auto variables = "\"id\":" + JsonString(id);

    std::vector<u8> data;
    if (!FetchGraphql(base_url, config, token, TITLE_QUERY, variables, data, error)) {
        return false;
    }

    return ParseTitle(data, base_url, out, error);
}

} // namespace sphaira::ownfoil::api
