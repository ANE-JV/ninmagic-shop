// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/ownfoil_index.hpp"
#include <array>
#include <chrono>
#include <ctime>
#include <string_view>
#include <stop_token>

namespace sphaira::ownfoil::snapshot {
using Digest = std::array<unsigned char, 32>;
inline constexpr std::size_t MaxBytes = 24 * 1024 * 1024;
inline constexpr std::uint32_t MaxRows = 65536;
inline constexpr std::int64_t Lifetime = 60 * 60 * 12;
struct Header {
    std::array<char, 8> magic{};
    Digest index{}, validators{}, payload{};
    std::int64_t saved{};
    std::uint32_t bytes{}, count{}, cookie{};
};
inline constexpr std::array<char, 8> Magic{'N','M','S','A','R','T','0','1'};
inline bool SafeUrl(std::string_view url) {
    if (url.find_first_of("?#\r\n") != url.npos) return false;
    const auto start = url.find("://");
    if (start == url.npos && !url.starts_with("//")) return true;
    const auto host = url.substr(start == url.npos ? 2 : start + 3);
    return host.substr(0, host.find('/')).find('@') == url.npos;
}
inline void Put(std::string& bytes, std::string_view value) {
    const auto len = static_cast<std::uint32_t>(value.size());
    bytes.append(reinterpret_cast<const char*>(&len), sizeof(len));
    bytes.append(value);
}
inline bool Take(std::string_view& bytes, std::string& value) {
    std::uint32_t len{};
    if (bytes.size() < sizeof(len)) return false;
    std::memcpy(&len, bytes.data(), sizeof(len));
    bytes.remove_prefix(sizeof(len));
    if (len > 65536 || bytes.size() < len) return false;
    value.assign(bytes.substr(0, len));
    bytes.remove_prefix(len);
    return true;
}
inline bool Inspect(const std::string& path, const Digest& index, std::int64_t now, Header& header) {
    auto* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    const bool valid = std::fread(&header, sizeof(header), 1, file) == 1 && header.magic == Magic
        && header.index == index && header.saved > 0 && now >= header.saved && now - header.saved <= Lifetime
        && header.bytes <= MaxBytes && header.count > 0 && header.count <= MaxRows && header.cookie <= 1;
    std::fclose(file);
    return valid;
}
template<class Hash>
bool Load(const std::string& path, const Header& expected, std::stop_token token, Hash hash, std::vector<index::Artwork>& result) {
    auto* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    Header header{};
    bool valid = std::fread(&header, sizeof(header), 1, file) == 1
        && header.magic == Magic && header.index == expected.index && header.payload == expected.payload
        && header.bytes == expected.bytes && header.count == expected.count && header.bytes <= MaxBytes && header.count <= MaxRows;
    std::string body;
    if (valid) {
        body.resize(header.bytes);
        valid = std::fread(body.data(), 1, body.size(), file) == body.size() && std::fgetc(file) == EOF && !std::ferror(file);
    }
    std::fclose(file);
    if (!valid || token.stop_requested() || hash(body) != header.payload) return false;
    std::string_view data = body;
    std::vector<index::Artwork> rows;
    rows.reserve(header.count);
    for (std::uint32_t i = 0; i < header.count; ++i) {
        if (token.stop_requested() || data.size() < 2) return false;
        auto& row = rows.emplace_back();
        const auto kind = static_cast<unsigned char>(data[0]);
        if (kind > static_cast<unsigned>(index::Kind::Unknown) || (data[1] != 0 && data[1] != 1)) return false;
        row.kind = static_cast<index::Kind>(kind); row.owned = data[1] != 0;
        data.remove_prefix(2);
        for (auto* text : {&row.id, &row.filename, &row.icon, &row.banner, &row.platform, &row.name, &row.title_id, &row.genre, &row.description})
            if (!Take(data, *text)) return false;
    }
    if (!data.empty()) return false;
    result = std::move(rows);
    return true;
}
// Publish the staged file only after all records and the digest are complete.
// Publication and profile invalidation share a lock in the caller.
template<class Hash>
bool Stage(const std::string& temporary, Header header, const std::vector<index::Artwork>& rows, std::stop_token token, Hash hash) {
    if (rows.empty() || rows.size() > MaxRows || token.stop_requested()) return false;
    std::string bytes;
    bytes.reserve(std::min(MaxBytes, rows.size() * 160));
    for (const auto& row : rows) {
        if (token.stop_requested()) return false;
        bytes += static_cast<char>(row.kind);
        bytes += static_cast<char>(row.owned);
        // A literal # in a basename is part of the title. Remote image URLs
        // with session tokens live in memory only; per-game artwork lookup
        // can fill an empty image again without persisting the token.
        const std::string_view filename = row.filename;
        const auto safe_filename = filename.find('?') == filename.npos && filename.find_first_of("\r\n") == filename.npos
            && ((filename.find("://") == filename.npos && !filename.starts_with("//")) || SafeUrl(filename));
        const auto fields = std::array<std::string_view,9>{row.id, safe_filename ? filename : std::string_view{},
            SafeUrl(row.icon) ? std::string_view{row.icon} : std::string_view{},
            SafeUrl(row.banner) ? std::string_view{row.banner} : std::string_view{},
            row.platform, row.name, row.title_id, row.genre, row.description};
        for (const auto text : fields) {
            if (text.size() > 65536 || bytes.size() + sizeof(std::uint32_t) + text.size() > MaxBytes) return false;
            Put(bytes, text);
        }
    }
    header.magic = Magic;
    header.payload = hash(bytes);
    header.count = static_cast<std::uint32_t>(rows.size());
    header.bytes = static_cast<std::uint32_t>(bytes.size());
    auto* file = std::fopen(temporary.c_str(), "wb");
    if (!file) return false;
    const bool complete = std::fwrite(&header, sizeof(header), 1, file) == 1
        && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (complete && closed && !token.stop_requested()) return true;
    std::remove(temporary.c_str());
    return false;
}
} // namespace sphaira::ownfoil::snapshot
