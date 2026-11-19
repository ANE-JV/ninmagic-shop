// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/ownfoil_index.hpp"
#include <unordered_set>

namespace sphaira::ownfoil::index {
struct ConsolidationStatus {
    std::size_t folders{}, mapped_folders{}, ambiguous_folders{}, unresolved_folders{};
};

// Convert a library's per-game directory into a game card only when the
// server supplies one exact, unique BASE name and a real application identity.
// Names are case sensitive. No punctuation stripping, fuzzy matching or custom
// ID arithmetic is used. Installation still resolves and validates containers.
inline auto AddGameFolderCards(std::vector<File>& files, const std::vector<Artwork>& records) -> ConsolidationStatus {
    std::unordered_map<std::string, const Artwork*> by_name, by_id;
    for (const auto& record : records) {
        if (record.kind != Kind::Base || record.id.empty()) continue;
        by_id.emplace(record.id, &record);
        if (record.name.empty()) continue;
        const auto [prior, fresh] = by_name.emplace(record.name, &record);
        if (!fresh && prior->second && prior->second->id != record.id) prior->second = nullptr;
    }
    ConsolidationStatus status;
    std::vector<File> cards;
    for (const auto& folder : files) {
        if (!folder.directory) continue;
        ++status.folders;
        const Artwork* metadata{};
        const auto explicit_id = Id(folder.name);
        if (!explicit_id.empty()) {
            if (const auto found = by_id.find(explicit_id); found != by_id.end()) metadata = found->second;
        } else if (const auto found = by_name.find(folder.name); found != by_name.end()) {
            metadata = found->second;
            if (!metadata) ++status.ambiguous_folders;
        }
        if (!metadata) { ++status.unresolved_folders; continue; }
        File card;
        card.url = folder.url;
        card.source_folder = folder.url;
        card.name = metadata->name.empty() ? folder.name : metadata->name;
        card.id = metadata->id;
        card.title_id = metadata->id;
        card.kind = Kind::Base;
        card.icon = metadata->icon.empty() ? metadata->banner : metadata->icon;
        card.banner = metadata->banner.empty() ? metadata->icon : metadata->banner;
        card.platform = folder.platform.empty() ? metadata->platform : folder.platform;
        cards.push_back(std::move(card));
        ++status.mapped_folders;
    }
    files.insert(files.end(), std::make_move_iterator(cards.begin()), std::make_move_iterator(cards.end()));
    return status;
}

inline auto BelongsToGame(const File& file, const std::string& id) -> bool {
    return !file.directory && file.source_folder.empty() && (Key(file) == id || file.id == id || file.title_id == id);
}

// A selected game may have separate update/DLC directories. This bounded queue
// is also used by fixtures so a truncated subtree cannot look like a complete
// package list. URL origin and subtree containment are checked by the caller.
class GameFolderQueue {
public:
    struct Entry { std::string url; unsigned depth{}; };
    static constexpr std::size_t MaxDirectories = 16;
    static constexpr unsigned MaxDepth = 3;
    explicit GameFolderQueue(std::string root) { Add(std::move(root), 0); }
    void Add(std::string url, unsigned depth) {
        if (m_scheduled.contains(url)) return;
        if (depth > MaxDepth || m_entries.size() >= MaxDirectories) { m_incomplete = true; return; }
        m_scheduled.insert(url);
        m_entries.push_back({std::move(url), depth});
    }
    auto Pop(Entry& entry) -> bool {
        if (m_cursor >= m_entries.size()) return false;
        entry = m_entries[m_cursor++];
        return true;
    }
    auto Incomplete() const -> bool { return m_incomplete; }
private:
    std::vector<Entry> m_entries;
    std::unordered_set<std::string> m_scheduled;
    std::size_t m_cursor{};
    bool m_incomplete{};
};
} // namespace sphaira::ownfoil::index
