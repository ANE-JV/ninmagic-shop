// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/ownfoil_index.hpp"
#include "utils/ownfoil_query.hpp"
#include <stop_token>
#include <unordered_set>

namespace sphaira::ownfoil::index {

// Search names as typed on the console keyboard, with or without Latin accents.
inline auto SearchText(std::string_view text) -> std::string {
    std::string key;
    key.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xc3 && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            if (next >= 0x80 && next <= 0xbf) {
                // U+00C0..00DF and U+00E0..00FF share these accent positions.
                constexpr std::string_view replacements{"aaaaaa_ceeeeiiiidnooooo_ouuuuy__"};
                static_assert(replacements.size() == 32);
                const auto replacement = replacements[(next - 0x80) % 32];
                if (replacement != '_') {
                    key += replacement;
                    ++i;
                    continue;
                }
            }
        }
        key += c < 128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    }
    return key;
}

// All indices address one immutable catalog snapshot. Build search keys once
// and reuse the same ordering when only the visible page changes.
class BrowseIndex {
public:
    using PlatformsById = std::unordered_map<std::string, std::string>;
    explicit BrowseIndex(const std::vector<File>& files, PlatformsById learned = {}) : m_learned(std::move(learned)) {
        m_names.reserve(files.size());
        m_ids.reserve(files.size());
        for (const auto& file : files) {
            m_names.push_back(SearchText(file.name));
            m_ids.push_back(Lower(file.id));
            if (!file.source_folder.empty()) m_mapped.insert(file.source_folder);
            if (file.directory) {
                ++folder_count;
            } else {
                const auto platform = PlatformOf(file);
                const std::string_view label = platform.empty() ? "__unknown__" : platform;
                if (std::find(platforms.begin(), platforms.end(), label) == platforms.end()) platforms.emplace_back(label);
            }
        }
        std::sort(platforms.begin(), platforms.end());
        mapped_folders = m_mapped.size();
    }

    std::vector<std::string> platforms;
    size_t folder_count{}, mapped_folders{};

    auto PlatformOf(const File& file) const -> std::string_view {
        if (file.platform.empty() || file.platform_learned) {
            if (const auto found = m_learned.find(file.id); found != m_learned.end()) return found->second;
        }
        return file.platform;
    }

    auto Select(const std::vector<File>& files, const api::CatalogQuery& query, std::stop_token token = {}) const -> std::vector<size_t> {
        using api::Category;
        std::vector<size_t> rows;
        std::unordered_map<std::string, size_t> by_id;
        const std::unordered_set<std::string_view> app_ids(query.app_ids.begin(), query.app_ids.end());
        const std::unordered_set<std::string_view> title_ids(query.title_ids.begin(), query.title_ids.end());
        const auto term = SearchText(query.search);
        for (size_t i = 0; i < files.size(); ++i) {
            if (token.stop_requested()) return {};
            const auto& file = files[i];
            if (query.consolidated && file.directory && (query.category != Category::Search || m_mapped.contains(file.url))) continue;
            if (!query.consolidated && !file.source_folder.empty()) continue;
            if (!query.platform.empty()) {
                const auto platform = PlatformOf(file);
                const auto matches = query.platform == "__unknown__" ? platform.empty() : platform == query.platform;
                if (!matches && (query.consolidated || !file.directory || !platform.empty())) continue;
            }
            const auto base = file.kind == Kind::Base || file.kind == Kind::Unknown;
            bool include{};
            switch (query.category) {
                case Category::All: include = base; break;
                case Category::NewGames: include = base && !app_ids.contains(file.id); break;
                case Category::Updates: include = file.kind == Kind::Update && app_ids.contains(file.id); break;
                case Category::Dlc: include = file.kind == Kind::Dlc && title_ids.contains(file.title_id) && !app_ids.contains(file.id); break;
                case Category::Favorites: include = base && app_ids.contains(Key(file)); break;
                case Category::Search: include = base && !term.empty() && (m_names[i].find(term) != std::string::npos || m_ids[i].find(term) != std::string::npos); break;
            }
            if (!include && !(file.directory && query.category != Category::Search && query.category != Category::Favorites)) continue;
            const auto [found, fresh] = by_id.try_emplace(Key(file), rows.size());
            if (fresh) rows.push_back(i);
            else if (file.version > files[rows[found->second]].version) rows[found->second] = i;
        }
        if (std::string_view{query.order_field} == "NAME") {
            std::stable_sort(rows.begin(), rows.end(), [&](size_t a, size_t b) {
                if (files[a].directory != files[b].directory)
                    return query.consolidated ? !files[a].directory : files[a].directory;
                return m_names[a] < m_names[b];
            });
        }
        if (query.descending) std::reverse(rows.begin(), rows.end());
        return rows;
    }
private:
    std::vector<std::string> m_names, m_ids;
    std::unordered_set<std::string> m_mapped;
    PlatformsById m_learned;
};
}
