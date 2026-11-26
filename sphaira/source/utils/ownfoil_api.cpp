#include "utils/ownfoil_api.hpp"
#include "utils/ownfoil_index.hpp"
#include "utils/ownfoil_direct_index.hpp"
#include "utils/ownfoil_snapshot.hpp"
#include "utils/ownfoil_diagnostic.hpp"
#include "utils/ownfoil_catalog.hpp"
#include "utils/ownfoil_browse.hpp"

#include "download.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "fs.hpp"

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
#include <atomic>
#include <cstdint>

namespace sphaira::ownfoil::api {
namespace {

// One report per connection attempt, independent of the general log setting.
// A newer attempt supersedes older workers; no network operation holds this
// mutex. Only fixed labels, booleans and aggregate counts reach the SD card.
std::mutex g_connection_diagnostic_mutex;
std::uint64_t g_connection_diagnostic_generation{};
std::size_t g_connection_diagnostic_bytes{};
constexpr auto CONNECTION_DIAGNOSTIC_PATH = "/config/NinMagicShop/connection-diagnostic.txt";
constexpr std::size_t CONNECTION_DIAGNOSTIC_LIMIT = 16 * 1024;

class ConnectionDiagnostic {
public:
    explicit ConnectionDiagnostic(const Config& config) {
        std::scoped_lock lock(g_connection_diagnostic_mutex);
        m_generation = ++g_connection_diagnostic_generation;
        g_connection_diagnostic_bytes = 0;
        fs::FsNativeSd disk;
        if (R_FAILED(disk.CreateDirectoryRecursively("/config/NinMagicShop/"))) return;
        if (auto file = std::fopen(CONNECTION_DIAGNOSTIC_PATH, "wb")) std::fclose(file);
        else return;
        m_ready = true;
        WriteLocked("NinMagicShop connection diagnostic v1\n");
        const char* backend = config.backend == Backend::Native ? "native" : config.backend == Backend::JsonIndex ? "json" : config.backend == Backend::LegacyHtml ? "legacy" : "auto";
        WriteLocked("config backend=" + std::string(backend) + " user_present=" + std::to_string(!config.user.empty())
            + " password_present=" + std::to_string(!config.pass.empty()) + " local_present=" + std::to_string(!config.local_address.empty())
            + " remote_present=" + std::to_string(!config.remote_address.empty()) + " resolved_present=" + std::to_string(!config.resolved_url.empty())
            + " negotiated_legacy=" + std::to_string(config.legacy_html) + " negotiated_index=" + std::to_string(config.json_index) + "\n");
    }
    void Event(const std::string& fixed_summary) const {
        std::scoped_lock lock(g_connection_diagnostic_mutex);
        WriteLocked(fixed_summary + '\n');
    }
    void Response(const char* phase, const std::string& requested, const curl::ApiResult& result) const {
        const auto& trace = result.diagnostic;
        Event("phase=" + std::string(phase) + " endpoint=" + diagnostic::Endpoint(requested) + " scheme=" + diagnostic::Scheme(requested)
            + " success=" + std::to_string(result.success) + " http=" + std::to_string(result.code)
            + " " + diagnostic::FormatBody(diagnostic::DescribeBody(result.data.empty() ? std::string_view{} : std::string_view{reinterpret_cast<const char*>(result.data.data()), result.data.size()})));
        if (!trace.enabled) { Event("transport_trace=unavailable"); return; }
        // Origins and peer addresses are used only for comparison, never emitted.
        const char* origin = trace.requested_origin.empty() || trace.effective_origin.empty() ? "unavailable" : trace.requested_origin == trace.effective_origin ? "same" : "different";
        Event("transport code=" + std::to_string(trace.transport_code) + " http_version=" + std::to_string(trace.http_version)
            + " redirects=" + std::to_string(trace.redirect_count) + " new_connections=" + std::to_string(trace.new_connections) + " effective_endpoint=" + diagnostic::EndpointFromTransport(trace.effective_endpoint_kind)
            + " effective_origin=" + origin + " legacy=" + std::to_string(trace.legacy) + " debug=" + std::to_string(trace.debug_enabled)
            + " auth_user=" + std::to_string(trace.user_present) + " auth_password=" + std::to_string(trace.password_present));
        auto version = trace.curl_version.substr(0, 512);
        std::erase_if(version, [](unsigned char c) { return !std::isalnum(c) && std::string_view{" /._-()+"}.find(c) == std::string_view::npos; });
        Event("curl_version=" + version);
        for (std::size_t i{}; i < trace.request_header_names.size(); ++i) {
            const auto names = diagnostic::DescribeHeaders(trace.request_header_names[i]);
            const auto method = i < trace.request_methods.size() ? trace.request_methods[i] : std::string{};
            const char* safe_method = method == "GET" ? "GET" : method == "POST" ? "POST" : method == "OPTIONS" ? "OPTIONS" : method == "HEAD" ? "HEAD" : "other";
            Event("request=" + std::to_string(i) + " method=" + safe_method + " header_names=" + names.known + " unknown=" + std::to_string(names.unknown));
        }
        const auto names = diagnostic::DescribeHeaders(trace.response_header_names);
        Event("response header_names=" + names.known + " unknown=" + std::to_string(names.unknown + trace.unknown_response_headers)
            + " unknown_request_headers=" + std::to_string(trace.unknown_request_headers));
        for (const auto code : trace.response_codes) Event("response_code=" + std::to_string(code));
    }
private:
    void WriteLocked(const std::string& text) const {
        if (!m_ready || m_generation != g_connection_diagnostic_generation || g_connection_diagnostic_bytes >= CONNECTION_DIAGNOSTIC_LIMIT) return;
        if (text.size() > CONNECTION_DIAGNOSTIC_LIMIT - g_connection_diagnostic_bytes) {
            constexpr auto marker = "report_truncated=1\n";
            if (std::strlen(marker) <= CONNECTION_DIAGNOSTIC_LIMIT - g_connection_diagnostic_bytes) {
                if (auto file = std::fopen(CONNECTION_DIAGNOSTIC_PATH, "ab")) { std::fwrite(marker, 1, std::strlen(marker), file); std::fclose(file); }
            }
            g_connection_diagnostic_bytes = CONNECTION_DIAGNOSTIC_LIMIT;
            return;
        }
        if (auto file = std::fopen(CONNECTION_DIAGNOSTIC_PATH, "ab")) {
            g_connection_diagnostic_bytes += std::fwrite(text.data(), 1, text.size(), file);
            std::fclose(file);
        }
    }
    std::uint64_t m_generation{};
    bool m_ready{};
};

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
    out.info.motd = index::MessageOfTheDay(yyjson_obj_get(root, "motd"));
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
// Opening one game must not replace the root snapshot used by console lists.
struct DetailIndexCache { IndexCache catalog; std::size_t bytes{}; };
std::unordered_map<std::string, DetailIndexCache> g_detail_index_cache;
enum class IndexScope { Catalog, GameFolder };
std::mutex g_platform_mutex;
std::unordered_map<std::string, index::BrowseIndex::PlatformsById> g_learned_platforms;
size_t g_learned_count{};
std::uint64_t g_learned_revision{};

void LearnPlatform(const std::string& provider, const std::string& id, const std::string& platform) {
    if (platform.empty()) return;
    std::scoped_lock lock(g_platform_mutex);
    if (g_learned_count >= 32768) { g_learned_platforms.clear(); g_learned_count = 0; }
    auto& learned = g_learned_platforms[provider];
    const auto [entry, fresh] = learned.try_emplace(id, platform);
    if (!fresh && entry->second == platform) return;
    if (fresh) ++g_learned_count;
    else entry->second = platform;
    ++g_learned_revision;
}

struct ArtworkSnapshot {
    std::vector<index::Artwork> records;
    index::ArtworkLookup by_identity;
};
struct ArtworkCache {
    std::string root, metadata_url, user, pass;
    std::shared_ptr<const ArtworkSnapshot> records;
};
std::mutex g_artwork_mutex;
ArtworkCache g_artwork_cache;

constexpr auto CatalogDirectory = "/switch/NinMagicShop/cache/ownfoil/catalog";
std::atomic<std::uint64_t> g_catalog_generation{}, g_catalog_stage{};

auto Digest(std::string_view bytes) -> snapshot::Digest {
    snapshot::Digest value{};
    sha256CalculateHash(value.data(), bytes.data(), bytes.size());
    return value;
}

auto CatalogSnapshotPath(std::string root, const Config& config) -> std::string {
    if (!root.ends_with('/')) root += '/';
    std::string identity;
    for (auto field : std::array<std::string_view, 6>{config.uid, root, config.user, config.pass, config.metadata_url, config.artwork_search_url})
        snapshot::Put(identity, field);
    snapshot::Put(identity, std::to_string(static_cast<int>(config.backend)));
    const auto key = Digest(identity);
    static constexpr char hex[] = "0123456789abcdef";
    std::string path = CatalogDirectory;
    path.push_back('/');
    for (auto byte : key) { path += hex[byte >> 4]; path += hex[byte & 15]; }
    return path + ".catalog";
}

auto HeaderValue(const curl::Header& headers, const char* key) -> std::string {
    const auto found = headers.Find(key);
    return found == headers.m_map.end() ? std::string{} : found->second;
}

auto Validators(const curl::Header& headers) -> snapshot::Digest {
    std::string combined;
    snapshot::Put(combined, HeaderValue(headers, "ETag"));
    snapshot::Put(combined, HeaderValue(headers, "Last-Modified"));
    return Digest(combined);
}

auto Cacheable(const curl::Header& headers) -> bool {
    const auto control = index::Lower(HeaderValue(headers, "Cache-Control"));
    return control.find("no-store") == control.npos;
}


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
    bool refresh, std::string_view index_body, std::shared_ptr<const ArtworkSnapshot>& records, const ConnectionDiagnostic* diagnostic = nullptr) -> bool {
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
    const auto generation = g_catalog_generation.load();
    const bool root_page = directory == root || directory + '/' == root || diagnostic;
    const auto path = root_page && !config.uid.empty() && snapshot::SafeUrl(root) && snapshot::SafeUrl(endpoint)
        ? CatalogSnapshotPath(root, config) : std::string{};
    const auto index_digest = path.empty() ? snapshot::Digest{} : Digest(index_body);
    const auto now = std::time(nullptr);
    snapshot::Header saved{};
    std::string candidate = path;
    bool warm = !path.empty() && snapshot::Inspect(candidate, index_digest, now, saved);
    if (!warm && !path.empty()) {
        candidate = path + ".bak";
        warm = snapshot::Inspect(candidate, index_digest, now, saved);
    }
    const auto request = [&](const std::string& cookie, bool check_only) {
        curl::Header headers;
        headers.m_map.emplace("Accept", "application/json");
        if (!cookie.empty()) headers.m_map.emplace("Cookie", cookie);
        auto result = curl::Api().ToMemory(curl::Url{endpoint}, std::move(headers),
            curl::UserPass{local ? config.user : "", local ? config.pass : ""}, curl::PreemptiveAuth{local},
            curl::Flags{static_cast<u32>(curl::Flag_KeepErrorBody | curl::Flag_NoRedirect | (check_only ? curl::Flag_NoBody : 0) | (diagnostic ? curl::Flag_ConnectionDiagnostic : 0))}, curl::StopToken{token}, curl::MemoryLimit{LIMIT}, curl::OnProgress{bounded});
        if (diagnostic) diagnostic->Response(check_only ? "artwork-validate" : cookie.empty() ? "artwork-get" : "artwork-session-get", endpoint, result);
        return result;
    };
    std::string cookie;
    const bool can_login = local && config.metadata_url.empty() && (!config.user.empty() || !config.pass.empty());
    const auto login_session = [&]() {
        const auto login_url = ResolveUrl(root, "login");
        const auto fields = "user=" + FormValue(config.user) + "&password=" + FormValue(config.pass);
        const auto login = curl::Api().ToMemory(curl::Url{login_url}, curl::Fields{fields},
            curl::Header{{{"Content-Type", "application/x-www-form-urlencoded"}}},
            curl::Flags{static_cast<u32>(curl::Flag_KeepErrorBody | curl::Flag_NoRedirect | (diagnostic ? curl::Flag_ConnectionDiagnostic : 0))}, curl::StopToken{token}, curl::MemoryLimit{LIMIT}, curl::OnProgress{bounded});
        if (diagnostic) diagnostic->Response("artwork-login", login_url, login);
        if (token.stop_requested() || login.code != 302) return false;
        const auto cookie_header = login.header.Find("Set-Cookie");
        if (cookie_header == login.header.m_map.end()) return false;
        // The cookie lives only for this metadata read, never in a file/cache.
        cookie = cookie_header->second.substr(0, cookie_header->second.find(';'));
        return !cookie.empty() && cookie.find_first_of("\r\n") == cookie.npos;
    };
    // Reauthenticate once, skipping the expected initial redirect when this
    // endpoint required a web session on its previous successful request.
    if (warm && saved.cookie && can_login && !login_session()) return false;
    auto result = request(cookie, warm);
    if (token.stop_requested()) return false;
    if (can_login && cookie.empty() && result.code == 302) {
        if (!login_session()) return false;
        result = request(cookie, warm);
    }
    auto parsed = std::make_shared<ArtworkSnapshot>();
    bool hit{};
    if (warm) {
        if (token.stop_requested() || result.code == 401 || result.code == 403 || result.code == 302) return false;
        const auto mime = index::Lower(HeaderValue(result.header, "Content-Type"));
        // A login HTML page cannot release cached metadata from the SD card.
        const bool metadata_access = result.success && result.code == 200 && mime.find("json") != mime.npos;
        hit = metadata_access && Cacheable(result.header) && Validators(result.header) == saved.validators
            && snapshot::Load(candidate, saved, token, Digest, parsed->records);
        if (diagnostic) diagnostic->Event(hit ? "metadata_snapshot=hit" : "metadata_snapshot=refresh");
        if (!hit && !token.stop_requested()) result = request(cookie, false);
    }
    if (token.stop_requested()) return false;
    if (!hit) {
        if (!result.success || result.code != 200 || result.data.size() > LIMIT) return false;
        if (!index::ParseArtwork(reinterpret_cast<const char*>(result.data.data()), result.data.size(), parsed->records)) return false;
        for (auto& art : parsed->records) {
            art.icon = ResolveUrl(endpoint, art.icon);
            art.banner = ResolveUrl(endpoint, art.banner);
        }
    }
    parsed->by_identity = index::IndexArtwork(parsed->records);
    if (token.stop_requested()) return false;
    std::string staged;
    if (!hit && !path.empty() && now > 0 && Cacheable(result.header)
            && index::Lower(HeaderValue(result.header, "Content-Type")).find("json") != std::string::npos
            && R_SUCCEEDED(fs::FsNativeSd().CreateDirectoryRecursively(CatalogDirectory))) {
        snapshot::Header header{};
        header.index = index_digest;
        header.validators = Validators(result.header);
        header.cookie = !cookie.empty();
        header.saved = now;
        auto temp = path + "." + std::to_string(g_catalog_stage++) + ".part";
        if (snapshot::Stage(temp, header, parsed->records, token, Digest)) staged = std::move(temp);
    }
    std::scoped_lock lock(g_artwork_mutex);
    if (!staged.empty()) {
        if (!token.stop_requested() && generation == g_catalog_generation.load()) {
            const auto previous = path + ".bak";
            // FAT can refuse a rename over an existing file.
            if (std::rename(staged.c_str(), path.c_str()) == 0) std::remove(previous.c_str());
            else {
                std::remove(previous.c_str());
                if (std::rename(path.c_str(), previous.c_str()) == 0) {
                    if (std::rename(staged.c_str(), path.c_str()) == 0) std::remove(previous.c_str());
                    else std::rename(previous.c_str(), path.c_str());
                }
            }
        }
        std::remove(staged.c_str());
    }
    if (token.stop_requested() || generation != g_catalog_generation.load()) return false;
    records = parsed;
    g_artwork_cache = {root, endpoint, config.user, config.pass, parsed};
    return true;
}

auto ReadDirectCatalog(const std::string& url, const Config& config, std::stop_token token,
    std::vector<index::File>& files, std::string& error) -> bool {
    if (!profile::ValidDirectCatalogName(config.direct_catalog)) {
        error = "El nombre del catálogo de la microSD no es válido.";
        return false;
    }
    const auto path = std::string{profile::DirectCatalogDirectory} + config.direct_catalog;
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file{std::fopen(path.c_str(), "rb"), &std::fclose};
    if (!file) {
        error = "No encontré el catálogo en /config/NinMagicShop/indices/.";
        return false;
    }
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
        error = "No se pudo leer el catálogo de la microSD.";
        return false;
    }
    const auto size = std::ftell(file.get());
    if (size <= 0 || static_cast<std::size_t>(size) > direct_index::MaxBytes) {
        error = "El catálogo de la microSD está vacío o supera el tamaño admitido.";
        return false;
    }
    std::string data(static_cast<std::size_t>(size), '\0');
    if (std::fseek(file.get(), 0, SEEK_SET) != 0 ||
        std::fread(data.data(), 1, data.size(), file.get()) != data.size() ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get())) {
        error = "No se pudo leer el catálogo de la microSD.";
        return false;
    }
    return !token.stop_requested() && direct_index::Parse(data, url, files, error);
}

auto FetchIndex(const std::string& url, const Config& config, std::stop_token token,
    std::shared_ptr<const std::vector<index::File>>& files, std::string& error, bool refresh = false, bool* legacy_result = nullptr,
    const ConnectionDiagnostic* diagnostic = nullptr, IndexScope scope = IndexScope::Catalog, bool* header_result = nullptr,
    bool* refused_result = nullptr, std::string* motd = nullptr) -> bool {
    if (refused_result) *refused_result = false;
    if (motd) motd->clear();
    if (!refresh) {
        std::scoped_lock lock(g_index_mutex);
        const auto detail = g_detail_index_cache.find(url);
        const auto& cache = scope == IndexScope::GameFolder && detail != g_detail_index_cache.end() ? detail->second.catalog : g_index_cache;
        if (cache.Matches(url, config.user, config.pass, config.direct_catalog)) {
            files = cache.files;
            if (legacy_result) *legacy_result = cache.legacy;
            if (header_result) *header_result = cache.legacy_headers;
            return true;
        }
    }
    if (!config.direct_catalog.empty()) {
        auto parsed = std::make_shared<std::vector<index::File>>();
        const bool ok = scope == IndexScope::Catalog && ReadDirectCatalog(url, config, token, *parsed, error);
        if (diagnostic) diagnostic->Event("source=sd_catalog accepted=" + std::to_string(ok)
            + " entries=" + std::to_string(parsed->size()));
        if (!ok || token.stop_requested()) return false;
        index::ApplyNinMagicPlatforms(*parsed, config.artwork_search_url);
        if (legacy_result) *legacy_result = false;
        if (header_result) *header_result = false;
        files = parsed;
        std::scoped_lock lock(g_index_mutex);
        g_index_cache = {url, config.user, config.pass, files, false, false, config.direct_catalog};
        return true;
    }
    // Catalogues are metadata, not packages: refuse an accidental file endpoint
    // before it can grow the metadata buffer to gigabytes.
    const size_t max_index_bytes = scope == IndexScope::GameFolder ? 2 * 1024 * 1024 : 32 * 1024 * 1024;
    const auto request = [&](bool legacy) {
        auto result = curl::Api().ToMemory(curl::Url{url}, curl::UserPass{config.user, config.pass},
            curl::PreemptiveAuth{true}, curl::Flags{static_cast<u32>(curl::Flag_KeepErrorBody | (legacy ? curl::Flag_LegacyOwnfoil : 0) | (diagnostic ? curl::Flag_ConnectionDiagnostic : 0))}, curl::StopToken{token},
            curl::MemoryLimit{max_index_bytes},
            curl::OnProgress{[max_index_bytes](s64 total, s64 now, s64, s64) { return total <= static_cast<s64>(max_index_bytes) && now <= static_cast<s64>(max_index_bytes); }});
        if (diagnostic) diagnostic->Response(legacy ? "legacy-index-get" : "modern-index-get", url, result);
        return result;
    };
    // HTML directory format and header negotiation are independent. Ownfoil 2.4+
    // serves the same listing with a Sphaira user agent; older builds require
    // the minimal header set. Retain the working mode for folders and files.
    bool strict = config.legacy_html ? config.legacy_headers : config.backend == Backend::LegacyHtml;
    auto result = request(strict);
    bool legacy{};
    bool refused{};
    auto parsed = std::make_shared<std::vector<index::File>>();
    auto parse = [&]() {
        if (!result.success || result.code != 200 || result.data.size() > static_cast<std::size_t>(max_index_bytes)) {
            refused = result.code == 401 || result.code == 403;
            error = refused ? "Incorrect username or password"
                : "Could not load the shop index (HTTP error or metadata size limit)";
            return false;
        }
        const auto bytes = reinterpret_cast<const char*>(result.data.data());
        if (!strict && !config.legacy_html && config.backend != Backend::LegacyHtml
                && index::Parse(bytes, result.data.size(), *parsed, error, motd)) return true;
        if (config.backend == Backend::JsonIndex) return false;
        // A normal browser or login page can contain links ending in '/'.
        // Only the dedicated client directory template is eligible here.
        const std::string_view body(bytes, result.data.size());
        if (!index::IsOwnfoilDirectory(body)) {
            error = "The server returned a web page instead of a shop directory";
            return false;
        }
        parsed->clear();
        legacy = index::ParseHtml(bytes, result.data.size(), *parsed, error, &refused);
        return legacy;
    };
    if (token.stop_requested()) return false;
    bool parsed_ok = parse();
    if (!parsed_ok && !refused && !config.legacy_html && config.backend != Backend::JsonIndex) {
        strict = !strict;
        result = request(strict);
        if (token.stop_requested()) return false;
        parsed_ok = parse();
    }
    if (refused_result) *refused_result = refused;
    if (diagnostic) diagnostic->Event("parser=" + std::string(legacy ? "html" : "json")
        + " accepted=" + std::to_string(parsed_ok) + " entries=" + std::to_string(parsed->size()));
    if (parsed_ok && scope == IndexScope::GameFolder && parsed->size() > 4096) {
        error = "The game folder exceeds the safe catalog scan limit"_i18n;
        return false;
    }
    if (parsed_ok && scope == IndexScope::Catalog && (legacy || !config.metadata_url.empty())) {
        std::shared_ptr<const ArtworkSnapshot> artwork;
        if (FetchWebArtwork(url, config, token, refresh, {reinterpret_cast<const char*>(result.data.data()), result.data.size()}, artwork, diagnostic)) {
            index::ApplyArtwork(*parsed, artwork->by_identity);
            index::AddGameFolderCards(*parsed, artwork->records);
        }
    } else if (parsed_ok && scope == IndexScope::GameFolder) {
        std::shared_ptr<const ArtworkSnapshot> artwork;
        {
            std::scoped_lock lock(g_artwork_mutex);
            if (g_artwork_cache.user == config.user && g_artwork_cache.pass == config.pass && IsSameOrigin(g_artwork_cache.root, url)) artwork = g_artwork_cache.records;
        }
        if (artwork) index::ApplyArtwork(*parsed, artwork->by_identity);
    }
    if (token.stop_requested()) return false;
    if (!parsed_ok) { error = i18n::get(error); return false; }
    error.clear();
    if (legacy_result) *legacy_result = legacy;
    if (header_result) *header_result = strict;
    const auto directory_base = legacy && !url.ends_with('/') ? url + '/' : url;
    for (auto& file : *parsed) {
        file.url = ResolveUrl(directory_base, file.url);
        file.icon = ResolveUrl(url, file.icon);
        file.banner = ResolveUrl(url, file.banner);
        file.source_folder = ResolveUrl(directory_base, file.source_folder);
        if (file.platform.empty()) file.platform = index::PlatformFromPath(file.url);
    }
    index::ApplyNinMagicPlatforms(*parsed, config.artwork_search_url);
    {
        std::scoped_lock lock(g_platform_mutex);
        if (const auto provider = g_learned_platforms.find(config.artwork_search_url); provider != g_learned_platforms.end()) {
            for (auto& file : *parsed) {
                const auto found = provider->second.find(file.id);
                if ((file.platform.empty() || file.platform_learned) && found != provider->second.end()) {
                    file.platform = found->second;
                    file.platform_learned = true;
                }
            }
        }
    }
    std::erase_if(*parsed, [](const auto& file) { return file.url.empty(); });
    files = parsed;
    std::scoped_lock lock(g_index_mutex);
    if (scope == IndexScope::Catalog) g_index_cache = {url, config.user, config.pass, files, legacy, strict};
    else {
        std::size_t cached_bytes{};
        for (const auto& entry : g_detail_index_cache) cached_bytes += entry.second.bytes;
        if (g_detail_index_cache.size() >= 32 || cached_bytes + result.data.size() > 8 * 1024 * 1024) g_detail_index_cache.clear();
        g_detail_index_cache[url] = {{url, config.user, config.pass, files, legacy, strict}, result.data.size()};
    }
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
    app.genre = file.genre;
    app.description = file.description;
    app.platform = file.platform;
    app.platform_learned = file.platform_learned;
    app.version = file.version ? "v" + std::to_string(file.version) : "";
    app.icon = {file.icon, IsSameOrigin(base_url, file.icon)};
    app.banner = {file.banner, IsSameOrigin(base_url, file.banner)};
    if (file.source_folder.empty()) app.download = {file.url, file.extension, file.size, file.sha256};
    return app;
}

auto Contains(const std::vector<std::string>& values, const std::string& id) -> bool {
    return std::find(values.begin(), values.end(), id) != values.end();
}

struct BrowseCache {
    std::weak_ptr<const std::vector<index::File>> source;
    std::shared_ptr<const index::BrowseIndex> index;
    std::shared_ptr<const std::vector<size_t>> rows;
    CatalogQuery query;
    std::uint64_t platform_revision{};
};
std::mutex g_browse_mutex;
BrowseCache g_browse;

auto FetchIndexApps(const std::string& base_url, const Config& config, std::stop_token token,
    const CatalogQuery& query, std::vector<ShopApp>& out, s64& total, std::string& error) -> bool {
    std::shared_ptr<const std::vector<index::File>> files;
    if (!FetchIndex(base_url, config, token, files, error)) return false;
    std::shared_ptr<const index::BrowseIndex> browse;
    std::shared_ptr<const std::vector<size_t>> rows;
    index::BrowseIndex::PlatformsById learned;
    std::uint64_t revision{};
    {
        std::scoped_lock lock(g_browse_mutex, g_platform_mutex);
        revision = g_learned_revision;
        if (g_browse.source.lock() == files) {
            const bool same = g_browse.query.SameSelection(query);
            // Keep page boundaries stable while artwork enriches other cards.
            if (revision == g_browse.platform_revision || (same && query.page > 1)) {
                browse = g_browse.index;
                if (same) rows = g_browse.rows;
            }
        }
        if (!browse) {
            const auto provider = g_learned_platforms.find(config.artwork_search_url);
            if (provider != g_learned_platforms.end()) learned = provider->second;
        }
    }
    if (!rows) {
        if (!browse) browse = std::make_shared<index::BrowseIndex>(*files, std::move(learned));
        auto selected = browse->Select(*files, query, token);
        if (token.stop_requested()) return false;
        rows = std::make_shared<const std::vector<size_t>>(std::move(selected));
        std::scoped_lock lock(g_browse_mutex);
        g_browse = {files, browse, rows, query, revision};
    }
    total = static_cast<s64>(rows->size());
    const auto count = std::clamp<s64>(query.page_size, 1, 1000);
    const auto begin = std::min(total, std::max<s64>(0, query.page - 1) * count);
    for (auto i = begin; i < std::min(total, begin + count); ++i) {
        const auto& file = (*files)[(*rows)[i]];
        auto app = IndexApp(file, base_url);
        const auto platform = browse->PlatformOf(file);
        if (platform != app.platform) {
            app.platform = platform;
            app.platform_learned = true;
        }
        out.push_back(std::move(app));
    }
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
    const auto folder_card = std::find_if(files->begin(), files->end(), [&id](const auto& file) {
        return !file.source_folder.empty() && IndexId(file) == id;
    });
    if (folder_card != files->end()) {
        const auto is_verified_base = [&id](const auto& file) {
            return index::BelongsToGame(file, id) && !file.url.empty() && !file.extension.empty()
                && (file.kind == index::Kind::Base || file.kind == index::Kind::Unknown);
        };
        bool has_base = std::any_of(files->begin(), files->end(), is_verified_base);
        const auto& folder = folder_card->source_folder;
        if (!IsSameOrigin(base_url, folder)) { error = "The game folder is outside the configured server"_i18n; return false; }
        // Read only this selected game's bounded subtree. No startup NAS crawl,
        // package HEAD/download or fabricated file URL is needed for a card.
        index::GameFolderQueue pending{folder};
        index::GameFolderQueue::Entry current;
        auto expanded = std::make_shared<std::vector<index::File>>();
        std::copy_if(files->begin(), files->end(), std::back_inserter(*expanded),
            [&id](const auto& file) { return index::BelongsToGame(file, id); });
        while (pending.Pop(current)) {
            if (token.stop_requested()) return false;
            std::shared_ptr<const std::vector<index::File>> contents;
            if (!FetchIndex(current.url, config, token, contents, error, false, nullptr, nullptr, IndexScope::GameFolder)) {
                if (!has_base || token.stop_requested()) return false;
                out.catalog_partial = true;
                error.clear();
                break;
            }
            for (const auto& item : *contents) {
                if (item.directory && IsSameOrigin(base_url, item.url) && item.url.starts_with(folder)) {
                    pending.Add(item.url, current.depth + 1);
                } else if (index::BelongsToGame(item, id)) {
                    has_base = has_base || is_verified_base(item);
                    expanded->push_back(item);
                }
            }
        }
        if (pending.Incomplete()) {
            if (!has_base) { error = "Se alcanzó el límite de subcarpetas sin encontrar el juego base"; return false; }
            out.catalog_partial = true;
        }
        if (std::none_of(expanded->begin(), expanded->end(), [&id](const auto& file) { return index::BelongsToGame(file, id); })) {
            error = "No se encontró un paquete del juego en su carpeta. Comprueba los archivos en el servidor y actualiza el catálogo.";
            return false;
        }
        files = std::move(expanded);
    }
    bool found{};
    std::unordered_map<std::string, size_t> addons;
    for (const auto& file : *files) {
        if (!index::BelongsToGame(file, id)) continue;
        const auto own_id = IndexId(file);
        found = true;
        if (own_id == id) {
            out.name = file.name;
            out.publisher = file.publisher;
            out.genre = file.genre;
            out.description = file.description;
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
auto TryConnect(const std::string& url, const Config& config, std::stop_token token, ConnectResult& out, const ConnectionDiagnostic& diagnostic) -> bool {
    if (token.stop_requested() || !IsSafeAddress(url)) return false;
    log_write("[OWNFOIL] Connecting to configured server\n");
    if (config.direct_catalog.empty() && config.backend != Backend::JsonIndex && config.backend != Backend::LegacyHtml) {
        const auto result = curl::Api().ToMemory(curl::Url{url}, curl::CustomRequest{"OPTIONS"},
            curl::UserPass{config.user, config.pass}, curl::PreemptiveAuth{true},
            curl::Flags{curl::Flag_KeepErrorBody | curl::Flag_ConnectionDiagnostic}, curl::StopToken{token}, curl::MemoryLimit{1024 * 1024},
            curl::OnProgress{[](s64 total, s64 now, s64, s64) { return total <= 1024 * 1024 && now <= 1024 * 1024; }});
        diagnostic.Response("native-options", url, result);
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
    std::string motd;
    out.error.clear();
    if (!FetchIndex(url, config, token, files, out.error, true, &out.legacy_html, &diagnostic, IndexScope::Catalog,
            &out.legacy_headers, &out.refused, &motd)) {
        out.reached = out.reached || out.refused;
        return false;
    }
    out.success = true;
    out.json_index = true;
    out.base_url = out.legacy_html && !url.ends_with('/') ? url + '/' : url;
    out.info = {};
    out.info.name = config.name;
    out.info.motd = std::move(motd);
    out.info.features.shop = true;
    return true;
}

// tries one address, over each scheme it could plausibly be served on.
auto TryAddress(const std::string& address, const Config& config, std::stop_token token, ConnectResult& out, const ConnectionDiagnostic& diagnostic) -> bool {
    for (const auto& url : BuildUrls(address)) {
        ConnectResult attempt{};
        if (TryConnect(url, config, token, attempt, diagnostic)) {
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
            if (app.genre.empty()) app.genre = index::Genre(src);
            if (app.description.empty()) app.description = index::Description(src);
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
        for (const auto src : {item, yyjson_obj_get(item, "titledb"), yyjson_obj_get(item, "title")}) {
            for (const auto key : {"platform", "console", "system"}) {
                if (app.platform.empty()) app.platform = index::PlatformMetadata(GetString(src, key));
            }
        }
        if (type == AppType::Base) {
            if (const auto platform = index::NeroMagicPlatform(base_url, app.app_id, app.name); !platform.empty()) {
                app.platform = platform;
            }
        }
        if (app.platform.empty()) app.platform = index::PlatformFromTags(app.name);
        const auto db = yyjson_obj_get(item, "titledb");
        const auto db_name = index::Lower(GetString(db, "name"));
        if (app.platform.empty() && app.app_id.starts_with("01") && !db_name.empty() && db_name != "unrecognized" && db_name != "unknown"
            && db_name.find("not found in titledb") == std::string::npos
            && (!app.publisher.empty() || !app.icon.url.empty() || !app.banner.url.empty())) {
            app.platform = "Nintendo Switch";
            app.platform_learned = true; // an explicit identification by the configured catalog takes precedence
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
constexpr const char* BASE_FIELDS = "appId displayVersion latestOwnedVersion{displayVersion} titledb{name publisher category intro icon(size:THUMB){url local} banner(size:THUMB){url local}}";
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
auto BuildPageQuery(const CatalogQuery& query, std::string& variables, bool identities_only = false) -> std::string {
    const char* app_type{"BASE"};
    const char* fields{identities_only ? "appId titledb{name publisher icon(size:THUMB){url} banner(size:THUMB){url}}" : BASE_FIELDS};
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
        case Category::Search: // search uses the library index
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
struct NativePlatformGroups {
    struct Game {
        std::string id, name, platform;
        std::string search_name;
        bool platform_learned{};
    };
    std::vector<Game> games;
    std::unordered_map<std::string, std::vector<size_t>> platforms;
};
struct NativeLibraryCache {
    std::string url, user, pass, provider;
    CatalogQuery query;
    std::shared_ptr<const NativePlatformGroups> groups;
    bool Account(const std::string& address, const Config& config) const {
        return url == address && user == config.user && pass == config.pass && provider == config.artwork_search_url;
    }
};
std::mutex g_native_library_mutex;
NativeLibraryCache g_native_library;
std::mutex g_public_artwork_mutex;
std::unordered_map<std::string, PublicArtworkCacheEntry> g_public_artwork_cache;
std::chrono::steady_clock::time_point g_public_artwork_next{};

static auto FetchPublicCatalogRecords(const std::string& provider, const std::string& query, std::stop_token token,
    std::vector<index::Artwork>& out) -> bool {
    if (token.stop_requested() || provider.empty() || !IsSafeAddress(provider)) return false;
    // Reserve a slot only when issuing the request. A rapidly cancelled page
    // must not leave several seconds of empty slots in front of the next one.
    while (!token.stop_requested()) {
        {
            std::scoped_lock lock(g_public_artwork_mutex);
            const auto now = std::chrono::steady_clock::now();
            if (now >= g_public_artwork_next) {
                g_public_artwork_next = now + std::chrono::milliseconds(250);
                break;
            }
        }
        svcSleepThread(25'000'000);
    }
    if (token.stop_requested()) return false;
    const auto url = provider + (provider.find('?') == std::string::npos ? "?q=" : "&q=") + curl::EscapeString(query);
    constexpr size_t max_bytes = 2 * 1024 * 1024;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto response = curl::Api().ToMemory(curl::Url{url}, curl::Flags{curl::Flag_NoRedirect}, curl::StopToken{token},
        curl::MemoryLimit{max_bytes}, curl::OnProgress{[deadline](s64 total, s64 now, s64, s64) {
            return total <= static_cast<s64>(max_bytes) && now <= static_cast<s64>(max_bytes)
                && std::chrono::steady_clock::now() < deadline;
        }});
    if (token.stop_requested() || !response.success || response.code != 200 || response.data.size() > max_bytes) return false;
    return index::ParseArtwork(reinterpret_cast<const char*>(response.data.data()), response.data.size(), out);
}

auto LookupPublicArtwork(const std::string& provider, const std::string& id, std::stop_token token, ShopApp& out) -> bool {
    ON_SCOPE_EXIT(if (!out.platform.empty()) LearnPlatform(provider, id, out.platform));
    if (token.stop_requested() || provider.empty() || id.size() != 16 || index::Id(id) != id) return false;
    const auto key = provider + '#' + id;
    {
        std::scoped_lock lock(g_public_artwork_mutex);
        const auto found = g_public_artwork_cache.find(key);
        if (found != g_public_artwork_cache.end() && std::chrono::steady_clock::now() - found->second.saved < std::chrono::minutes(15)) {
            out = found->second.app;
            return found->second.found;
        }
    }
    std::vector<index::Artwork> records;
    if (!FetchPublicCatalogRecords(provider, id, token, records)) return false;
    bool matched{};
    for (const auto& record : records) {
        if (record.id != id) continue;
        // An exact ID is required even when a search page offered nearby titles.
        out.icon = {ResolveUrl(provider, record.icon), false};
        out.banner = {ResolveUrl(provider, record.banner), false};
        if (out.icon.url.empty()) out.icon = out.banner;
        if (out.banner.url.empty()) out.banner = out.icon;
        out.platform = record.platform;
        out.name = record.name;
        out.genre = record.genre;
        out.description = record.description;
        matched = !out.icon.url.empty() || !out.platform.empty() || !out.genre.empty() || !out.description.empty();
        break;
    }
    std::scoped_lock lock(g_public_artwork_mutex);
    if (g_public_artwork_cache.size() >= 1024) g_public_artwork_cache.clear();
    g_public_artwork_cache[key] = {out, matched, std::chrono::steady_clock::now()};
    return matched;
}

auto FallbackArtworkUrl(const std::string& id, bool banner) -> std::string {
    if (id.size() != 16 || !id.starts_with("01") || index::Id(id).empty()) return {};
    return "https://api.nlib.cc/nx/" + id + (banner ? "/banner/360p" : "/icon/240");
}

auto LookupGamePresentation(const Config& config, const ShopApp& card, std::stop_token token, ShopApp& out) -> bool {
    const auto& identity = card.artwork_id.empty() ? card.app_id : card.artwork_id;
    if (identity.size() != 16 || token.stop_requested()) return false;
    const auto id = index::Id(identity);
    if (id.empty()) return false;
    out = card;
    // An explicit console or cover from a shop wins. The configured website
    // also identifies retro packages whose IDs carry no console information.
    if (!config.artwork_search_url.empty() && (out.icon.url.empty() || out.platform.empty() || !id.starts_with("01"))) {
        ShopApp web;
        if (LookupPublicArtwork(config.artwork_search_url, id, token, web)) {
            if (out.icon.url.empty()) out.icon = std::move(web.icon);
            if (out.banner.url.empty()) out.banner = std::move(web.banner);
            if (out.genre.empty()) out.genre = std::move(web.genre);
            if (out.description.empty()) out.description = std::move(web.description);
            if ((out.platform.empty() || out.platform_learned) && !web.platform.empty()) out.platform = std::move(web.platform);
        }
    }
    if (token.stop_requested()) return false;
    if (!id.starts_with("01") || (!out.platform.empty() && out.platform != "Nintendo Switch")
        || (!out.genre.empty() && !out.description.empty())) return true;

    // Nlib's per-title endpoint supplies an actual synopsis even when older
    // Ownfoil versions only expose the genre. Never query by a fuzzy name.
    const auto key = "nlib-es:" + id;
    ShopApp info;
    bool cached{};
    {
        std::scoped_lock lock(g_public_artwork_mutex);
        const auto entry = g_public_artwork_cache.find(key);
        if (entry != g_public_artwork_cache.end() && std::chrono::steady_clock::now() - entry->second.saved < std::chrono::minutes(15)) {
            info = entry->second.app;
            cached = true;
        }
    }
    if (!cached) {
        const auto url = "https://api.nlib.cc/nx/" + id + "?lang=es&fields=name,description,intro,category,icon,banner";
        const auto data = curl::Api().ToMemory(curl::Url{url}, curl::Flags{curl::Flag_NoRedirect}, curl::StopToken{token},
            curl::OnProgress{[](s64 total, s64 now, s64, s64) { return total <= 128 * 1024 && now <= 128 * 1024; }});
        if (token.stop_requested()) return false;
        bool matched{};
        index::Artwork record;
        if (data.success && data.code == 200
            && index::ParsePublicTitle(reinterpret_cast<const char*>(data.data.data()), data.data.size(), id, record)) {
            info.genre = std::move(record.genre);
            info.description = std::move(record.description);
            // Nlib currently emits some http media URLs behind its reverse
            // proxy. Use the provider's documented HTTPS routes instead.
            if (!record.icon.empty()) info.icon.url = FallbackArtworkUrl(id, false);
            if (!record.banner.empty()) info.banner.url = FallbackArtworkUrl(id, true);
            matched = true;
        }
        if (matched || data.code == 404) {
            std::scoped_lock lock(g_public_artwork_mutex);
            if (g_public_artwork_cache.size() >= 1024) g_public_artwork_cache.clear();
            g_public_artwork_cache[key] = {info, matched, std::chrono::steady_clock::now()};
        }
    }
    if (out.genre.empty()) out.genre = std::move(info.genre);
    if (out.description.empty()) out.description = std::move(info.description);
    if (out.icon.url.empty()) out.icon = std::move(info.icon);
    if (out.banner.url.empty()) out.banner = std::move(info.banner);
    return true;
}

void InvalidateCatalog(const std::string& base_url, const Config& config) {
    std::scoped_lock lock(g_index_mutex, g_artwork_mutex, g_public_artwork_mutex, g_native_library_mutex);
    ++g_catalog_generation;
    const auto path = CatalogSnapshotPath(config.resolved_url.empty() ? base_url : config.resolved_url, config);
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
    if (g_native_library.Account(base_url, config)) g_native_library = {};
    std::erase_if(g_public_artwork_cache, [&config](const auto& entry) { return entry.first.starts_with(config.artwork_search_url + '#'); });
    g_index_cache.Invalidate(base_url, config.user, config.pass, config.direct_catalog);
    std::erase_if(g_detail_index_cache, [&base_url, &config](const auto& entry) {
        return entry.second.catalog.user == config.user && entry.second.catalog.pass == config.pass && IsSameOrigin(base_url, entry.first);
    });
    if (g_artwork_cache.user == config.user && g_artwork_cache.pass == config.pass) g_artwork_cache = {};
}

auto CachedPlatforms(const std::string& base_url, const Config& config) -> std::vector<std::string> {
    if (!config.json_index) {
        std::scoped_lock lock(g_native_library_mutex);
        std::vector<std::string> platforms;
        if (g_native_library.Account(base_url, config) && g_native_library.groups) {
            for (const auto& [label, games] : g_native_library.groups->platforms)
                if (!games.empty()) platforms.push_back(label.empty() ? "__unknown__" : label);
            std::sort(platforms.begin(), platforms.end());
        }
        return platforms;
    }
    std::scoped_lock lock(g_index_mutex, g_browse_mutex);
    if (g_index_cache.Matches(base_url, config.user, config.pass, config.direct_catalog) && g_browse.source.lock() == g_index_cache.files && g_browse.index)
        return g_browse.index->platforms;
    return {};
}

auto CachedCatalogStatus(const std::string& base_url, const Config& config) -> CatalogStatus {
    CatalogStatus result;
    std::scoped_lock lock(g_index_mutex, g_browse_mutex);
    if (!g_index_cache.Matches(base_url, config.user, config.pass, config.direct_catalog) || g_browse.source.lock() != g_index_cache.files || !g_browse.index) return result;
    result.folders = static_cast<s64>(g_browse.index->folder_count);
    result.mapped_folders = static_cast<s64>(g_browse.index->mapped_folders);
    result.unresolved_folders = std::max<s64>(0, result.folders - result.mapped_folders);
    result.partial = result.unresolved_folders != 0;
    return result;
}

auto Connect(const Config& config, std::stop_token token) -> ConnectResult {
    const ConnectionDiagnostic diagnostic{config};
    const auto finish = [&](ConnectResult result) {
        diagnostic.Event("result accepted=" + std::to_string(result.success) + " cancelled=" + std::to_string(token.stop_requested())
            + " refused=" + std::to_string(result.refused) + " reached=" + std::to_string(result.reached)
            + " legacy=" + std::to_string(result.legacy_html) + " index=" + std::to_string(result.json_index)
            + " remote=" + std::to_string(result.used_remote));
        return result;
    };
    ConnectResult out{};
    out.error = "No address configured"_i18n;

    // whatever answered last time goes first, so a reconnect costs one request
    // rather than a walk down every address and scheme again.
    if (const auto resolved = ResolvedFrom(config); !resolved.url.empty()) {
        ConnectResult attempt{};
        diagnostic.Event("candidate=resolved");
        if (TryConnect(resolved.url, config, token, attempt, diagnostic)) {
            attempt.used_remote = resolved.used_remote;
            return finish(std::move(attempt));
        }

        if (attempt.refused) {
            return finish(std::move(attempt));
        }
    }

    diagnostic.Event("candidate=local");
    if (!config.local_address.empty() && TryAddress(config.local_address, config, token, out, diagnostic)) {
        out.used_remote = false;
        return finish(std::move(out));
    }

    // the remote address is the same shop and would answer the same way, while
    // whatever proxies it may answer less helpfully and bury the reason.
    if (out.refused) {
        return finish(std::move(out));
    }

    if (!config.remote_address.empty()) {
        ConnectResult remote_attempt{};
        diagnostic.Event("candidate=remote");
        if (TryAddress(config.remote_address, config, token, remote_attempt, diagnostic)) {
            remote_attempt.used_remote = true;
            return finish(std::move(remote_attempt));
        }

        // whichever address got an answer explains the failure better than the
        // one that stayed silent.
        if (remote_attempt.reached || !out.reached) {
            return finish(std::move(remote_attempt));
        }
    }

    return finish(std::move(out));
}

static auto FetchNativePage(const std::string& base_url, const Config& config, std::stop_token token, const CatalogQuery& query,
    std::vector<ShopApp>& out, s64& total, std::string& error, bool identities_only = false) -> bool {
    std::string variables;
    const auto doc = BuildPageQuery(query, variables, identities_only);

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
    if (!ParseApps(data, base_url, type, out, total, error)) return false;
    std::scoped_lock lock(g_platform_mutex);
    if (const auto provider = g_learned_platforms.find(config.artwork_search_url); provider != g_learned_platforms.end()) {
        for (auto& app : out) {
            if (!app.platform.empty() && !app.platform_learned) continue;
            if (const auto known = provider->second.find(app.artwork_id); known != provider->second.end()) {
                app.platform = known->second;
                app.platform_learned = true;
            }
        }
    }
    return true;
}

static auto FetchNativeDirectoryMetadata(const std::string& base_url, const Config& config, std::stop_token token) -> std::vector<index::File> {
    // The same authenticated shop may also publish filenames via its Sphaira
    // view. Only read the root; the native API remains the source of game IDs.
    constexpr size_t limit = 16 * 1024 * 1024;
    const auto response = curl::Api().ToMemory(curl::Url{base_url}, curl::UserPass{config.user, config.pass},
        curl::PreemptiveAuth{true}, curl::Flags{curl::Flag_NoRedirect}, curl::StopToken{token}, curl::MemoryLimit{limit});
    if (!response.success || response.code != 200 || response.data.empty() || token.stop_requested()) return {};

    const std::string_view body(reinterpret_cast<const char*>(response.data.data()), response.data.size());
    if (!index::IsOwnfoilDirectory(body)) return {};
    std::vector<index::File> files;
    std::string error;
    if (!index::ParseHtml(body.data(), body.size(), files, error)) return {};
    return files;
}

static auto FetchScopedNativePage(const std::string& base_url, const Config& config, std::stop_token token, const CatalogQuery& query,
    std::vector<ShopApp>& out, s64& total, std::string& error) -> bool {
    // Use one ordered index for browsing and search. Ownfoil's search does not
    // fold accents, and classifying only its matches can omit retro landmarks.
    CatalogQuery selection;
    selection.order_field = query.order_field;
    selection.descending = query.descending;
    std::shared_ptr<const NativePlatformGroups> groups;
    {
        std::scoped_lock lock(g_native_library_mutex);
        if (g_native_library.Account(base_url, config) && g_native_library.query.SameSelection(selection)) {
            groups = g_native_library.groups;
        }
    }
    if (!groups) {
        auto found = std::make_shared<NativePlatformGroups>();
        auto& games = found->games;
        auto batch = selection;
        batch.page_size = 1000;
        s64 consumed{}, reported{};
        for (batch.page = 1; ; ++batch.page) {
            if (token.stop_requested()) return false;
            std::vector<ShopApp> rows;
            if (!FetchNativePage(base_url, config, token, batch, rows, reported, error, true)) return false;
            if (reported > 100000 || (rows.empty() && consumed < reported)) {
                error = "El servidor devolvió un catálogo demasiado grande o incompleto.";
                return false;
            }
            consumed += static_cast<s64>(rows.size());
            for (auto& app : rows) {
                if (app.app_id.empty()) continue;
                games.push_back({std::move(app.app_id), std::move(app.name), std::move(app.platform), {}, app.platform_learned});
            }
            if (consumed >= reported || rows.empty()) break;
        }
        if (std::any_of(games.begin(), games.end(), [](const auto& game) { return index::IsPlaceholderName(game.name, game.id); })) {
            index::ApplyDirectoryNames(games, FetchNativeDirectoryMetadata(base_url, config, token));
            if (token.stop_requested()) return false;
            for (auto& game : games) {
                if (game.platform.empty()) game.platform = index::PlatformFromTags(game.name);
            }
        }
        const auto eligible = [](const NativePlatformGroups::Game&) { return true; };
        index::ApplyVerifiedRetroSeries(games, config.artwork_search_url, eligible);
        index::ApplyNinMagicCollection(games, config.artwork_search_url, eligible);
        for (size_t i{}; i < games.size(); ++i) {
            auto& game = games[i];
            game.search_name = index::SearchText(game.name);
            found->platforms[game.platform].push_back(i);
        }
        if (token.stop_requested()) return false;
        groups = std::move(found);
        std::scoped_lock lock(g_native_library_mutex);
        g_native_library = {base_url, config.user, config.pass, config.artwork_search_url, selection, groups};
    }
    const auto count = std::clamp<s64>(query.page_size, 1, 1000);
    const auto page_index = std::max<s64>(1, query.page) - 1;
    const auto term = index::SearchText(query.search);
    const bool id_term = std::all_of(term.begin(), term.end(), [](char c) { return index::HexDigit(c) >= 0; });
    const bool search = query.category == Category::Search;
    std::vector<index::Artwork> public_records;
    std::unordered_map<std::string_view, std::string_view> alternate_names;
    // Localized names only supplement the authenticated catalog. The exact ID
    // must still appear in this account's library and selected platform.
    if (search && !id_term && index::IsNinMagicArtwork(config.artwork_search_url)
        && FetchPublicCatalogRecords(config.artwork_search_url, query.search, token, public_records)) {
        for (const auto& record : public_records) {
            if (record.kind == index::Kind::Base && record.id.size() == 16
                && index::SearchText(record.name).find(term) != std::string::npos) {
                alternate_names.try_emplace(record.id, record.name);
            }
        }
    }
    if (token.stop_requested()) return false;
    total = 0;
    std::vector<const NativePlatformGroups::Game*> matches;
    const auto select = [&](const NativePlatformGroups::Game& game) {
        if (search && game.search_name.find(term) == std::string::npos
            && (!id_term || index::Lower(game.id).find(term) == std::string::npos)
            && !alternate_names.contains(game.id)) return;
        if (total / count == page_index) matches.push_back(&game);
        ++total;
    };
    if (query.platform.empty()) {
        for (const auto& game : groups->games) select(game);
    } else {
        const auto label = query.platform == "__unknown__" ? std::string{} : query.platform;
        if (const auto group = groups->platforms.find(label); group != groups->platforms.end()) {
            for (const auto i : group->second) select(groups->games[i]);
        }
    }
    if (token.stop_requested()) return false;
    if (matches.empty()) return true;
    auto page = selection;
    page.category = Category::Favorites;
    page.page_size = static_cast<s64>(matches.size());
    for (const auto* game : matches) page.app_ids.push_back(game->id);
    std::vector<ShopApp> rows;
    s64 reported{};
    if (!FetchNativePage(base_url, config, token, page, rows, reported, error)) return false;
    std::unordered_map<std::string, size_t> positions;
    for (size_t i{}; i < rows.size(); ++i) positions.emplace(rows[i].app_id, i);
    for (const auto* game : matches) {
        const auto match = positions.find(game->id);
        if (match == positions.end()) continue;
        auto& row = rows[match->second];
        // Keep the same classification as the library for these exact IDs.
        row.platform = game->platform;
        row.platform_learned = false;
        if (const auto alternate = alternate_names.find(game->id);
            alternate != alternate_names.end() && game->search_name.find(term) == std::string::npos) {
            row.name = alternate->second;
        } else if (index::IsPlaceholderName(row.name, row.app_id)) {
            row.name = game->name;
        }
        out.push_back(std::move(row));
    }
    return true;
}

auto FetchApps(const std::string& base_url, const Config& config, std::stop_token token, const CatalogQuery& query, std::vector<ShopApp>& out, s64& total, std::string& error) -> bool {
    out.clear();
    total = 0;
    if (config.json_index) return FetchIndexApps(base_url, config, token, query, out, total, error);
    if ((query.category == Category::Favorites && query.app_ids.empty())
        || (query.category == Category::Updates && query.app_ids.empty())
        || (query.category == Category::Dlc && query.title_ids.empty())
        || (query.category == Category::Search && query.search.empty())) return true;
    if (query.category == Category::Search || query.category == Category::All)
        return FetchScopedNativePage(base_url, config, token, query, out, total, error);
    return FetchNativePage(base_url, config, token, query, out, total, error);
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
