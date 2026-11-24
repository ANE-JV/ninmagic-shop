#pragma once

// Plain Tinfoil shop indices. This parser has no console or network dependency.
#include <yyjson.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <iterator>
#include <utility>
#include <unordered_map>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::ownfoil::index {
enum class Kind { Base, Update, Dlc, Unknown };
struct File {
    std::string url, name, id, title_id, publisher, icon, banner, extension, sha256, platform;
    // A metadata-backed game card whose actual containers are read on demand.
    // This URL is never exposed as a downloadable package.
    std::string source_folder;
    std::string genre, description;
    int64_t version{};
    int64_t size{};
    Kind kind{Kind::Unknown};
    bool directory{};
    bool platform_learned{};
    bool version_known{};
};
inline auto Key(const File& file) -> std::string {
    if (!file.id.empty() && file.kind != Kind::Unknown) return file.id;
    // Stable across builds so generic packages retain their own favorite key.
    uint64_t hash = 14695981039346656037ULL;
    for (const auto c : file.url) { hash ^= static_cast<unsigned char>(c); hash *= 1099511628211ULL; }
    char key[32]{};
    std::snprintf(key, sizeof(key), "package-%016llx", static_cast<unsigned long long>(hash));
    return key;
}
struct CatalogCache {
    std::string url, user, pass;
    std::shared_ptr<const std::vector<File>> files;
    bool legacy{}, legacy_headers{};
    std::string direct_catalog{};
    auto Matches(const std::string& requested_url, const std::string& requested_user, const std::string& requested_pass,
                 std::string_view requested_catalog = {}) const -> bool {
        return files && url == requested_url && user == requested_user && pass == requested_pass && direct_catalog == requested_catalog;
    }
    void Invalidate(const std::string& requested_url, const std::string& requested_user, const std::string& requested_pass,
                    std::string_view requested_catalog = {}) {
        if (Matches(requested_url, requested_user, requested_pass, requested_catalog)) *this = {};
    }
};
inline auto String(yyjson_val* obj, const char* key) -> std::string {
    const auto value = yyjson_obj_get(obj, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : "";
}
inline auto Image(yyjson_val* obj, const char* key) -> std::string {
    const auto value = yyjson_obj_get(obj, key);
    if (yyjson_is_str(value)) return yyjson_get_str(value);
    if (yyjson_is_obj(value)) {
        auto url = String(value, "url");
        return url.empty() ? String(value, "src") : url;
    }
    return {};
}
// Ownfoil uses an array named category; the NinMagic website uses a genre string.
inline auto MetadataText(yyjson_val* field, size_t byte_limit) -> std::string {
    if (!yyjson_is_str(field)) return {};
    const auto data = yyjson_get_str(field);
    auto size = yyjson_get_len(field);
    if (size > byte_limit) {
        size = byte_limit;
        while (size && (static_cast<unsigned char>(data[size]) & 0xc0) == 0x80) --size;
    }
    return std::string(data, size);
}
inline auto Genre(yyjson_val* record) -> std::string {
    for (const auto key : {"genre", "category", "genres"}) {
        const auto value = yyjson_obj_get(record, key);
        if (auto text = MetadataText(value, 256); !text.empty()) return text;
        if (!yyjson_is_arr(value)) continue;
        std::string joined;
        size_t i, count;
        yyjson_val* item;
        yyjson_arr_foreach(value, i, count, item) {
            const auto part = MetadataText(item, 128);
            if (part.empty()) continue;
            if (joined.size() + part.size() + 2 > 256) break;
            if (!joined.empty()) joined += ", ";
            joined += part;
        }
        if (!joined.empty()) return joined;
    }
    return {};
}
inline auto Description(yyjson_val* record) -> std::string {
    for (const auto key : {"description", "intro", "summary"}) {
        if (auto text = MetadataText(yyjson_obj_get(record, key), 4096); !text.empty()) return text;
    }
    return {};
}
inline auto Integer(yyjson_val* obj, const char* key, int64_t fallback = 0) -> int64_t {
    const auto value = yyjson_obj_get(obj, key);
    if (yyjson_is_int(value)) return yyjson_get_sint(value);
    if (yyjson_is_str(value)) {
        int64_t number{};
        const auto text = std::string_view{yyjson_get_str(value)};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) return number;
    }
    return fallback;
}
inline auto Lower(std::string text) -> std::string {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}
// Only a dedicated metadata value or a complete directory component identifies
// a platform. NSP/NSZ/XCI is a package format, never a platform classification.
inline auto Platform(std::string value) -> std::string {
    value = Lower(value);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(0, 1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    const auto prefix = value.find(" - ");
    if (prefix != std::string::npos && prefix < 4 && std::all_of(value.begin(), value.begin() + prefix, [](unsigned char c) { return std::isdigit(c); })) value.erase(0, prefix + 3);
    for (const auto& [alias, label] : {
        std::pair{"switch", "Nintendo Switch"}, {"nintendo switch", "Nintendo Switch"},
        {"nes", "NES"}, {"famicom", "NES"}, {"nintendo entertainment system", "NES"},
        {"snes", "Super Nintendo"}, {"super nintendo", "Super Nintendo"}, {"super famicom", "Super Nintendo"},
        {"n64", "Nintendo 64"}, {"nintendo 64", "Nintendo 64"},
        {"gb", "Game Boy"}, {"game boy", "Game Boy"}, {"gbc", "Game Boy Color"}, {"game boy color", "Game Boy Color"},
        {"gba", "Game Boy Advance"}, {"game boy advance", "Game Boy Advance"},
        {"nds", "Nintendo DS"}, {"nintendo ds", "Nintendo DS"},
        {"ps1", "PlayStation"}, {"psx", "PlayStation"}, {"playstation", "PlayStation"},
        {"psp", "PSP"}, {"mega drive", "Mega Drive"}, {"megadrive", "Mega Drive"}, {"genesis", "Mega Drive"},
        {"master system", "Master System"}, {"game gear", "Game Gear"}, {"dreamcast", "Dreamcast"},
        {"arcade", "Arcade"}, {"mame", "Arcade"}, {"neo geo", "Neo Geo"}, {"neogeo", "Neo Geo"}}) {
        if (value == alias) return label;
    }
    return {};
}
inline auto PlatformMetadata(std::string value) -> std::string {
    if (const auto known = Platform(value); !known.empty()) return known;
    std::erase_if(value, [](unsigned char c) { return c < 32 || c == 127; });
    if (value.size() > 64) value.resize(64);
    return value;
}
inline auto HexDigit(char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline auto Decode(std::string_view text) -> std::string {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && HexDigit(text[i+1]) >= 0 && HexDigit(text[i+2]) >= 0) {
            const auto c = static_cast<char>(HexDigit(text[i+1]) * 16 + HexDigit(text[i+2]));
            if (static_cast<unsigned char>(c) >= 32) out += c;
            i += 2;
        } else if (static_cast<unsigned char>(text[i]) >= 32) out += text[i];
    }
    return out;
}
inline auto PlatformFromPath(std::string path) -> std::string {
    path = Decode(path.substr(0, path.find_first_of("?#")));
    const auto scheme = path.find("://");
    if (scheme != std::string::npos) {
        const auto slash = path.find('/', scheme + 3);
        if (slash == std::string::npos) return {};
        path.erase(0, slash + 1);
    }
    // The last component can be a package name; inspect directories only.
    const auto last = path.rfind('/');
    if (last == std::string::npos) return {};
    path.resize(last + 1);
    std::string platform;
    for (size_t begin = 0; begin < path.size();) {
        const auto end = path.find('/', begin);
        if (const auto found = Platform(path.substr(begin, end - begin)); !found.empty()) platform = found;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return platform;
}
inline auto PlatformFromTags(const std::string& filename) -> std::string {
    for (size_t begin = 0; (begin = filename.find('[', begin)) != std::string::npos; ++begin) {
        const auto end = filename.find(']', begin + 1);
        if (end == std::string::npos) break;
        if (const auto platform = Platform(filename.substr(begin + 1, end - begin - 1)); !platform.empty()) return platform;
    }
    return {};
}
inline auto Id(std::string_view text) -> std::string {
    for (size_t i = 0; i + 16 <= text.size(); ++i) {
        if (i && HexDigit(text[i-1]) >= 0) continue;
        if (i + 16 < text.size() && HexDigit(text[i+16]) >= 0) continue;
        const auto part = text.substr(i, 16);
        if (!std::all_of(part.begin(), part.end(), [](char c) { return HexDigit(c) >= 0; })) continue;
        uint64_t numeric{};
        std::from_chars(part.data(), part.data() + part.size(), numeric, 16);
        // Custom forwarders also have 16-digit IDs (often 05...). Identity
        // recognition says nothing about their platform or content relationship.
        if (!numeric) continue;
        auto out = std::string(part);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::toupper(c); });
        return out;
    }
    return {};
}
inline auto InstallTarget(uint64_t parent, const std::string& explicit_id, bool base) -> uint64_t {
    if (explicit_id.size() == 16 && !Id(explicit_id).empty()) {
        uint64_t value{};
        const auto parsed = std::from_chars(explicit_id.data(), explicit_id.data() + explicit_id.size(), value, 16);
        if (parsed.ec == std::errc{} && parsed.ptr == explicit_id.data() + explicit_id.size()) return value;
    }
    if (base) return parent;
    // This fallback is valid only for Nintendo's retail relationship. Custom
    // content without an explicit ID installs the selected container unfiltered.
    return (parent >> 56) == 1 ? parent ^ uint64_t(0x800) : 0;
}
inline auto FilenameExtension(std::string_view text) -> std::string {
    const auto dot = text.rfind('.');
    if (dot == text.npos) return {};
    auto ext = Lower(std::string{text.substr(dot + 1)});
    return ext == "nsp" || ext == "nsz" || ext == "xci" || ext == "xcz" ? ext : "";
}
inline auto Extension(std::string_view url) -> std::string {
    return FilenameExtension(url.substr(0, url.find_first_of("?#")));
}
inline auto CleanName(std::string text) -> std::string {
    if (const auto dot = text.rfind('.'); dot != std::string::npos) {
        const auto extension = Lower(text.substr(dot + 1));
        if (extension == "nsp" || extension == "nsz" || extension == "xci" || extension == "xcz") text.erase(dot);
    }
    for (size_t pos = 0; (pos = text.find('[', pos)) != std::string::npos;) {
        const auto end = text.find(']', pos);
        if (end == std::string::npos) break;
        const auto token = text.substr(pos + 1, end - pos - 1);
        const auto version = token.size() > 1 && (token[0] == 'v' || token[0] == 'V')
            && std::all_of(token.begin() + 1, token.end(), [](unsigned char c) { return std::isdigit(c); });
        if ((token.size() == 16 && !Id(token).empty()) || version) text.erase(pos, end - pos + 1);
        else pos = end + 1;
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(0, 1);
    return text;
}
inline auto ParseRoot(yyjson_val* root, std::vector<File>& out, std::string& error) -> bool {
    out.clear();
    const auto files = yyjson_obj_get(root, "files");
    if (!yyjson_is_arr(files)) {
        error = "The JSON index has no files array (encrypted indices are not supported)";
        return false;
    }
    const auto titledb = yyjson_obj_get(root, "titledb");
    size_t i, max;
    yyjson_val* item;
    if (yyjson_arr_size(files) > 100000) { error = "El catálogo supera 100000 entradas."; return false; }
    yyjson_arr_foreach(files, i, max, item) {
        File file;
        file.url = yyjson_is_str(item) ? yyjson_get_str(item) : String(item, "url");
        if (file.url.empty()) continue;
        const auto fragment = file.url.find('#');
        auto filename = fragment == std::string::npos ? file.url.substr(0, file.url.find('?')) : file.url.substr(fragment + 1);
        if (fragment == std::string::npos) filename = filename.substr(filename.find_last_of('/') + 1);
        filename = Decode(filename);
        file.url = file.url.substr(0, fragment);
        file.extension = FilenameExtension(filename);
        if (file.extension.empty()) file.extension = Extension(file.url);
        file.directory = yyjson_is_true(yyjson_obj_get(item, "directory")) || yyjson_is_true(yyjson_obj_get(item, "folder"));
        if (file.extension.empty() && !file.directory) continue;
        file.id = Id(String(item, "app_id"));
        if (file.id.empty()) file.id = Id(String(item, "appId"));
        if (file.id.empty()) file.id = Id(String(item, "titleId"));
        if (file.id.empty()) file.id = Id(String(item, "title_id"));
        if (file.id.empty()) file.id = Id(String(item, "id"));
        if (file.id.empty()) file.id = Id(filename);
        file.version = Integer(item, "version", -1);
        file.version_known = file.version >= 0;
        if (file.version < 0) {
            const auto lower = Lower(filename);
            const auto v = lower.find("[v");
            file.version = 0;
            if (v != std::string::npos) {
                const auto end = lower.find(']', v + 2);
                if (end != std::string::npos) {
                    int64_t number{};
                    const auto parsed = std::from_chars(lower.data() + v + 2, lower.data() + end, number);
                    if (parsed.ec == std::errc{} && parsed.ptr == lower.data() + end && number >= 0) { file.version = number; file.version_known = true; }
                }
            }
        }
        file.size = std::max<int64_t>(0, Integer(item, "size"));
        file.sha256 = Lower(String(item, "sha256"));
        if (file.sha256.size() != 64 || !std::all_of(file.sha256.begin(), file.sha256.end(), [](char c) { return HexDigit(c) >= 0; })) file.sha256.clear();
        if (!file.id.empty()) {
            uint64_t id{};
            std::from_chars(file.id.data(), file.id.data() + file.id.size(), id, 16);
            const auto suffix = id & 0xfff;
            // Retail 01 IDs have Nintendo's base/update/DLC relationship.
            // A custom forwarder's suffix is not evidence of a related update.
            if ((id >> 56) == 1) {
                file.kind = suffix == 0 ? Kind::Base : suffix == 0x800 ? Kind::Update : Kind::Dlc;
                const auto title = file.kind == Kind::Dlc ? (id - 0x1000) & ~uint64_t(0xfff) : id & ~uint64_t(0xfff);
                char title_text[17]{};
                std::snprintf(title_text, sizeof(title_text), "%016llX", static_cast<unsigned long long>(title));
                file.title_id = title_text;
            }
            auto kind = Lower(String(item, "app_type"));
            if (kind.empty()) kind = Lower(String(item, "appType"));
            if (kind.empty()) kind = Lower(String(item, "type"));
            auto parent = Id(String(item, "parentTitleId"));
            if (parent.empty()) parent = Id(String(item, "baseId"));
            if (parent.empty()) parent = Id(String(item, "title_id"));
            if (kind == "base") { file.kind = Kind::Base; file.title_id = file.id; }
            else if (!parent.empty() && parent != file.id && (kind == "update" || kind == "dlc")) {
                file.kind = kind == "update" ? Kind::Update : Kind::Dlc;
                file.title_id = parent;
            }
        }
        const auto metadata = yyjson_obj_get(titledb, file.id.c_str());
        file.name = String(metadata, "name");
        if (file.name.empty()) file.name = String(item, "name");
        if (file.name.empty()) file.name = CleanName(filename);
        if (file.name.empty()) file.name = file.id.empty() ? "Unnamed package" : file.id;
        file.publisher = String(metadata, "publisher");
        if (file.publisher.empty()) file.publisher = String(item, "publisher");
        for (const auto source : {item, metadata, root}) {
            for (const auto key : {"platform", "console", "system"}) {
                if (file.platform.empty()) file.platform = PlatformMetadata(String(source, key));
            }
        }
        if (file.platform.empty()) file.platform = PlatformFromPath(file.url);
        if (file.platform.empty()) file.platform = PlatformFromTags(filename);
        for (const auto source : {item, metadata}) {
            if (file.genre.empty()) file.genre = Genre(source);
            if (file.description.empty()) file.description = Description(source);
            for (const auto key : {"coverUrl", "cover_url", "cover", "iconUrl", "icon", "thumbnailUrl", "thumbnail", "imageUrl", "image"})
                if (file.icon.empty()) file.icon = Image(source, key);
            for (const auto key : {"bannerUrl", "banner", "heroUrl"})
                if (file.banner.empty()) file.banner = Image(source, key);
        }
        if (file.banner.empty()) file.banner = file.icon;
        out.emplace_back(std::move(file));
    }
    return true;
}
inline auto Parse(const char* bytes, size_t size, std::vector<File>& out, std::string& error) -> bool {
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc{yyjson_read(bytes, size, YYJSON_READ_NOFLAG), yyjson_doc_free};
    if (!doc) { out.clear(); error = "The server did not return a plain JSON shop index"; return false; }
    return ParseRoot(yyjson_doc_get_root(doc.get()), out, error);
}

// Artwork is supplemental metadata: it never changes package identity, kind or
// its install target. A filename match must be supplied explicitly by the web.
struct Artwork {
    std::string id, filename, icon, banner, platform;
    std::string name, title_id;
    Kind kind{Kind::Unknown};
    bool owned{};
    std::string genre, description;
};
// Nlib is a single-record response, unlike Ownfoil's array and the user's
// website. Reject a mismatched record before using either text or media flags.
inline auto ParsePublicTitle(const char* data, size_t size, const std::string& id, Artwork& out) -> bool {
    if (!data || !size || size > 128 * 1024 || id.size() != 16 || Id(id) != id) return false;
    const auto doc = yyjson_read(data, size, YYJSON_READ_NOFLAG);
    if (!doc) return false;
    const auto record = yyjson_doc_get_root(doc);
    const bool matches = String(record, "id") == id;
    if (matches) {
        out.id = id;
        out.genre = Genre(record);
        out.description = Description(record);
        out.icon = Image(record, "icon");
        out.banner = Image(record, "banner");
    }
    yyjson_doc_free(doc);
    return matches;
}
inline auto RealArtwork(std::string url) -> std::string {
    const auto lower = Lower(url);
    for (const auto host : {"//placehold.it/", "//via.placeholder.com/", "//placehold.co/"})
        if (lower.find(host) != std::string::npos) return {};
    return url;
}
inline auto ParseArtwork(const char* bytes, size_t size, std::vector<Artwork>& out) -> bool {
    out.clear();
    // NinMagic's existing /juegos page publishes this exact public JSON block.
    // Reading it does not bypass login, VIP filters, pagination or Turnstile.
    std::string_view payload{bytes, size};
    if (const auto marker = payload.find("id=\"catalogGamesData\""); marker != std::string_view::npos) {
        const auto begin = payload.find('>', marker);
        const auto end = payload.find("</script>", begin);
        if (begin == std::string_view::npos || end == std::string_view::npos) return false;
        payload = payload.substr(begin + 1, end - begin - 1);
    }
    auto doc = yyjson_read(payload.data(), payload.size(), YYJSON_READ_NOFLAG);
    if (!doc) return false;
    const auto root = yyjson_doc_get_root(doc);
    auto records = yyjson_obj_get(root, "games");
    if (!records) records = yyjson_obj_get(root, "files");
    if (!records && !yyjson_obj_get(root, "base")) records = root;
    const auto add = [&](yyjson_val* record) {
        if (!yyjson_is_obj(record)) return;
        Artwork art;
        art.id = Id(String(record, "app_id"));
        if (art.id.empty()) art.id = Id(String(record, "appId"));
        if (art.id.empty()) art.id = Id(String(record, "titleId"));
        if (art.id.empty()) art.id = Id(String(record, "title_id"));
        if (art.id.empty()) art.id = Id(String(record, "id"));
        art.title_id = Id(String(record, "title_id"));
        art.name = String(record, "name");
        if (art.name.empty()) art.name = String(record, "title_name");
        art.genre = Genre(record);
        art.description = Description(record);
        auto kind = Lower(String(record, "app_type"));
        if (kind.empty()) kind = Lower(String(record, "appType"));
        art.kind = kind == "base" ? Kind::Base : kind == "update" ? Kind::Update : kind == "dlc" ? Kind::Dlc : Kind::Unknown;
        art.owned = yyjson_is_true(yyjson_obj_get(record, "owned"));
        art.filename = String(record, "filename");
        if (art.filename.empty()) art.filename = String(record, "fileName");
        for (const auto key : {"coverUrl", "cover_url", "cover", "icon_url", "iconUrl", "icon", "thumbnailUrl", "thumbnail", "image_url", "imageUrl", "image"})
            if (art.icon.empty()) art.icon = RealArtwork(Image(record, key));
        for (const auto key : {"banner_url", "bannerUrl", "banner", "heroUrl"})
            if (art.banner.empty()) art.banner = RealArtwork(Image(record, key));
        for (const auto key : {"platform", "console", "system"})
            if (art.platform.empty()) art.platform = PlatformMetadata(String(record, key));
        if (art.platform.empty()) art.platform = PlatformFromTags(art.name);
        if (art.platform.empty()) art.platform = PlatformFromTags(art.filename);
        if (art.platform.empty()) art.platform = PlatformFromPath(art.filename);
        // Classic Ownfoil also lists custom forwarders as BASE with 01 IDs.
        // A missing TitleDB row and a placeholder are not platform evidence.
        const auto database_id = String(record, "id");
        const bool in_database = !database_id.empty() && Lower(database_id) == Lower(art.id);
        const bool has_art = !art.icon.empty() || !art.banner.empty();
        if (art.platform.empty() && art.kind != Kind::Unknown && art.id.starts_with("01")
            && Lower(database_id).find("not found") == std::string::npos && (in_database || has_art))
            art.platform = "Nintendo Switch";
        if (!art.id.empty() || !art.filename.empty()) out.push_back(std::move(art));
    };
    if (yyjson_is_arr(records)) {
        if (yyjson_arr_size(records) > 100000) { yyjson_doc_free(doc); return false; }
        size_t i, count; yyjson_val* record;
        yyjson_arr_foreach(records, i, count, record) add(record);
    } else if (yyjson_is_obj(records)) {
        if (yyjson_obj_size(records) > 100000) { yyjson_doc_free(doc); return false; }
        size_t i, count; yyjson_val *key, *record;
        yyjson_obj_foreach(records, i, count, key, record) add(record);
    } else if (yyjson_is_obj(yyjson_obj_get(root, "base"))) {
        add(yyjson_obj_get(root, "base"));
        for (const auto key : {"updates", "dlc", "dlcs"}) {
            const auto rows = yyjson_obj_get(root, key);
            if (!yyjson_is_arr(rows)) continue;
            if (yyjson_arr_size(rows) > 100000) { yyjson_doc_free(doc); return false; }
            size_t i, count; yyjson_val* record;
            yyjson_arr_foreach(rows, i, count, record) add(record);
        }
    } else { yyjson_doc_free(doc); return false; }
    yyjson_doc_free(doc);
    return true;
}
struct ArtworkLookup {
    // The caller retains the immutable record snapshot while using this index.
    std::unordered_map<std::string_view, const Artwork*> ids, filenames;
};
inline auto IndexArtwork(const std::vector<Artwork>& artwork) -> ArtworkLookup {
    ArtworkLookup found;
    found.ids.reserve(artwork.size());
    for (const auto& art : artwork) {
        if (!art.id.empty()) found.ids[art.id] = &art;
        if (!art.filename.empty()) {
            const auto [entry, fresh] = found.filenames.emplace(art.filename, &art);
            if (!fresh) entry->second = nullptr; // ambiguous filenames never guess
        }
    }
    return found;
}
inline void ApplyArtwork(std::vector<File>& files, const ArtworkLookup& metadata) {
    const auto& [ids, filenames] = metadata;
    for (auto& file : files) {
        const Artwork* art{};
        if (file.directory) {
            if (const auto found = ids.find(Id(file.name)); found != ids.end()) art = found->second;
        }
        for (const auto& id : {file.id, file.title_id}) {
            if (const auto found = ids.find(id); !art && !id.empty() && found != ids.end()) art = found->second;
        }
        if (!art && !file.directory) {
            auto filename = Decode(file.url.substr(0, file.url.find_first_of("?#")));
            filename = filename.substr(filename.find_last_of('/') + 1);
            if (const auto found = filenames.find(filename); found != filenames.end()) art = found->second;
        }
        if (!art) continue;
        if (file.icon.empty()) file.icon = art->icon.empty() ? art->banner : art->icon;
        if (file.banner.empty()) file.banner = art->banner.empty() ? art->icon : art->banner;
        if (file.platform.empty()) file.platform = art->platform;
        if (file.genre.empty()) file.genre = art->genre;
        if (file.description.empty()) file.description = art->description;
    }
}
inline void ApplyArtwork(std::vector<File>& files, const std::vector<Artwork>& artwork) {
    ApplyArtwork(files, IndexArtwork(artwork));
}

inline auto HtmlText(std::string text) -> std::string {
    for (const auto& [entity, replacement] : {std::pair{"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}}) {
        size_t pos{};
        while ((pos = text.find(entity, pos)) != std::string::npos) {
            text.replace(pos, std::strlen(entity), replacement);
            pos += std::strlen(replacement);
        }
    }
    return text;
}
inline auto JsonQuote(const std::string& text) -> std::string {
    std::string out = "\"";
    for (const auto c : text) {
        if (c == '\\' || c == '"') out += '\\';
        if (static_cast<unsigned char>(c) >= 32) out += c;
    }
    return out + '"';
}
// Recognize the dedicated Ownfoil HTML representation without accepting links
// from the web dashboard, sign-in or a proxy error page as a shop directory.
inline bool IsOwnfoilDirectory(std::string_view body) {
    const auto whitespace = [&] { body.remove_prefix(std::min(body.find_first_not_of(" \r\n\t"), body.size())); };
    const auto take = [&](std::string_view tag) {
        whitespace();
        if (!body.starts_with(tag)) return false;
        body.remove_prefix(tag.size());
        return true;
    };
    take("<!DOCTYPE html>");
    take("<html>");
    take("<body>");
    if (!take("<table>")) return false;
    whitespace();
    return body.starts_with("<a ") || body.starts_with("</table>");
}

// Ownfoil's older Sphaira route is a list of hrefs. It can contain thousands of
// subdirectories; expose those for lazy navigation rather than crawling a NAS.
inline auto ParseHtml(const char* bytes, size_t size, std::vector<File>& out, std::string& error, bool* access_denied = nullptr) -> bool {
    if (access_denied) *access_denied = false;
    const std::string text(bytes, size);
    const auto lower = Lower(text);
    std::string json = "{\"files\":[";
    std::vector<File> folders;
    bool seen{};
    for (size_t pos = 0; (pos = lower.find("<a ", pos)) != std::string::npos;) {
        const auto end = lower.find('>', pos);
        const auto attr = lower.find("href", pos + 3);
        if (end == std::string::npos) break;
        pos = end + 1;
        if (attr == std::string::npos || attr >= end) continue;
        auto begin = lower.find('=', attr + 4);
        if (begin == std::string::npos || begin >= end) continue;
        do { ++begin; } while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])));
        if (begin >= end || (text[begin] != '\'' && text[begin] != '"')) continue;
        const auto finish = text.find(text[begin], begin + 1);
        if (finish == std::string::npos || finish > end) continue;
        auto href = HtmlText(text.substr(begin + 1, finish - begin - 1));
        if (href.starts_with("00 - ERROR")) {
            if (access_denied) *access_denied = true;
            error = "The server rejected the login or this account has no shop access";
            out.clear(); return false;
        }
        if (href.empty() || href == "../" || href == "./" || href == "/") continue;
        // Ownfoil 2.x prints filesystem names directly as hrefs. In a local
        // directory/package name, '#' and '?' are filename bytes, not a URL
        // fragment/query. Preserve them before RFC URL resolution can discard
        // the rest of the path (for example a game named '#Example').
        const auto raw_lower = Lower(href);
        const bool local_path = href.find("://") == std::string::npos && !href.starts_with("//");
        const bool file_path = raw_lower.ends_with(".nsp") || raw_lower.ends_with(".nsz") || raw_lower.ends_with(".xci") || raw_lower.ends_with(".xcz");
        if (local_path && (href.ends_with('/') || file_path)) {
            std::string escaped;
            for (const auto c : href) escaped += c == '#' ? "%23" : c == '?' ? "%3F" : std::string(1, c);
            href = std::move(escaped);
        }
        if (folders.size() > 100000) { error = "El catálogo supera 100000 carpetas."; out.clear(); return false; }
        seen = true;
        if (href.ends_with('/')) {
            File folder;
            folder.url = href;
            folder.name = Decode(href.substr(0, href.size() - 1));
            folder.name = folder.name.substr(folder.name.find_last_of('/') + 1);
            folder.directory = true;
            folder.platform = Platform(folder.name);
            folders.push_back(std::move(folder));
        } else {
            if (json.back() != '[') json += ',';
            json += "{\"url\":" + JsonQuote(href) + '}';
        }
    }
    if (!seen && lower.find("<table>") == std::string::npos) {
        error = "The server returned a web page instead of a shop directory";
        return false;
    }
    json += "]}";
    if (!Parse(json.data(), json.size(), out, error)) return false;
    out.insert(out.end(), std::make_move_iterator(folders.begin()), std::make_move_iterator(folders.end()));
    return true;
}
} // namespace sphaira::ownfoil::index
