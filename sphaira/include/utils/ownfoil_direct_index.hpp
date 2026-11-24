// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/ownfoil_index.hpp"
#include <limits>
#include <unordered_set>

namespace sphaira::ownfoil::direct_index {
inline constexpr std::size_t MaxBytes = 16 * 1024 * 1024;
inline constexpr std::size_t MaxRows = 65536;

inline std::string_view Text(yyjson_val* object, const char* key) {
    const auto value = yyjson_obj_get(object, key);
    return yyjson_is_str(value) ? std::string_view{yyjson_get_str(value), yyjson_get_len(value)} : std::string_view{};
}

// Compare the configured shop and the export, retaining a reverse proxy's
// case-sensitive base path. Absolute API routes are resolved at the origin.
inline std::string CanonicalServer(std::string_view value) {
    if (value.size() < 9 || value.size() > 2048 || index::Lower(std::string{value.substr(0, 8)}) != "https://"
        || value.find_first_of("?#@\\") != value.npos
        || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c <= 32 || c == 127; })) return {};
    const auto slash = value.find('/', 8);
    auto authority = value.substr(8, slash == value.npos ? value.npos : slash - 8);
    if (authority.empty() || authority.find('%') != authority.npos) return {};
    if (authority.ends_with(":443")) authority.remove_suffix(4);
    if (authority.empty()) return {};
    auto path = slash == value.npos ? std::string_view{} : value.substr(slash);
    if (path.ends_with('/')) path.remove_suffix(1);
    return "https://" + index::Lower(std::string{authority}) + std::string{path};
}

inline bool TitleId(std::string_view id) {
    return id.size() == 16 && id.find_first_not_of('0') != id.npos
        && std::all_of(id.begin(), id.end(), [](char c) { return index::HexDigit(c) >= 0; });
}

inline bool DirectUrl(std::string_view url, std::uint64_t& file_id) {
    constexpr std::string_view prefix = "/api/get_game/";
    const auto hash = url.find('#');
    if (!url.starts_with(prefix) || hash == url.npos || hash <= prefix.size() || url.size() > 4096) return false;
    const auto id = url.substr(prefix.size(), hash - prefix.size());
    const auto end = id.data() + id.size();
    const auto number = std::from_chars(id.data(), end, file_id);
    if (number.ec != std::errc{} || number.ptr != end || !file_id ||
        file_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
    const auto filename = url.substr(hash + 1);
    for (size_t i = 0; i < filename.size(); ++i) {
        unsigned char c = filename[i];
        if (c == '%') {
            if (i + 2 >= filename.size()) return false;
            const auto high = index::HexDigit(filename[i+1]), low = index::HexDigit(filename[i+2]);
            if (high < 0 || low < 0) return false;
            c = static_cast<unsigned char>(high * 16 + low);
            i += 2;
        } else if (c == '?' || c == '#') return false;
        if (c < 32 || c == 127 || c == '/' || c == '\\') return false;
    }
    return !index::FilenameExtension(index::Decode(filename)).empty();
}

inline bool Parse(std::string_view json, std::string_view profile_root, std::vector<index::File>& out, std::string& error) {
    out.clear();
    const auto fail = [&](const char* reason) { error = reason; return false; };
    if (json.empty() || json.size() > MaxBytes) return fail("Tamaño de catálogo directo no válido.");
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc{yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG), yyjson_doc_free};
    const auto root = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    const auto schema = yyjson_obj_get(root, "schema");
    if (!yyjson_is_uint(schema) || yyjson_get_uint(schema) != 1) return fail("Formato de catálogo directo no compatible.");
    const auto server = CanonicalServer(profile_root);
    if (server.empty() || CanonicalServer(Text(root, "server")) != server) return fail("El catálogo pertenece a otra dirección de tienda.");
    const auto origin = server.substr(0, server.find('/', 8));
    const auto files = yyjson_obj_get(root, "files");
    if (!yyjson_is_arr(files) || yyjson_arr_size(files) > MaxRows) return fail("Lista de archivos del catálogo directo no válida.");

    std::vector<index::File> parsed;
    if (!index::ParseRoot(root, parsed, error)) return false;
    if (parsed.size() != yyjson_arr_size(files)) return fail("El catálogo contiene archivos que no se pueden instalar.");
    size_t i, count;
    yyjson_val* item;
    std::unordered_set<std::uint64_t> used_ids;
    yyjson_arr_foreach(files, i, count, item) {
        const auto url = Text(item, "url"), app = Text(item, "app_id"), title = Text(item, "title_id"), kind = Text(item, "kind");
        std::uint64_t file_id{};
        const auto version = yyjson_obj_get(item, "version"), size = yyjson_obj_get(item, "size");
        if (!DirectUrl(url, file_id) || !used_ids.insert(file_id).second || !TitleId(app) || !TitleId(title) ||
            !yyjson_is_uint(version) || yyjson_get_uint(version) > UINT32_MAX ||
            !yyjson_is_uint(size) || !yyjson_get_uint(size) || yyjson_get_uint(size) > INT64_MAX ||
            parsed[i].directory) return fail("El catálogo contiene una descarga o identidad de paquete no válida.");
        auto& file = parsed[i];
        file.id = index::Id(std::string{app});
        file.title_id = index::Id(std::string{title});
        if (kind == "base" && file.id == file.title_id) file.kind = index::Kind::Base;
        else if (kind == "update" && file.id != file.title_id) file.kind = index::Kind::Update;
        else if (kind == "dlc" && file.id != file.title_id) file.kind = index::Kind::Dlc;
        else return fail("El tipo de paquete y su juego no coinciden en el catálogo.");
        file.url = origin + std::string{url.substr(0, url.find('#'))};
        // The direct export identifies packages. Artwork has its own provider.
        file.icon.clear();
        file.banner.clear();
    }
    out = std::move(parsed);
    error.clear();
    return true;
}
} // namespace sphaira::ownfoil::direct_index
