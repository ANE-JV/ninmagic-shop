// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "utils/ownfoil_index.hpp"
#include <array>
#include <bitset>
#include <unordered_set>

namespace sphaira::ownfoil::index {
struct ConsolidationStatus {
    std::size_t folders{}, mapped_folders{}, ambiguous_folders{}, unresolved_folders{};
};

struct RetroSeries {
    std::string_view prefix, platform;
    std::string_view first_suffix, first_title, second_suffix, second_title;
};

// Landmarks from the shared catalog's package names. A mirror must supply two
// matching names and IDs independently for each series it exposes.
inline constexpr RetroSeries RetroSeriesList[]{
    {"05100000", "PSP", "00010000", "1000 Tiny Claws", "00030000", "20Q"},
    {"05103000", "PSP", "00010000", "Arthur and the Minimoys", "00030000", "Exit 2"},
    {"05107000", "PlayStation", "00010000", "007 - Racing", "00030000", "007 - The World Is Not Enough"},
    {"05108000", "Nintendo 3DS / 2DS", "00010000", "Neko Tomo", "00030000", "Dragon Quest Monsters - Joker 3 Professional"},
    {"05122000", "Nintendo 64", "00010000", "007 - The World Is Not Enough", "00030000", "1080 Snowboarding"},
    {"05158000", "Game Boy Advance", "00010000", "007 - Everything or Nothing", "00030000", "007 - Nightfire"},
    {"05269000", "Game Boy Color", "00010000", "10-Pin Bowling", "00030000", "1942"},
    {"05275000", "Nintendo DS", "00010000", "007 - Quantum of Solace", "00030000", "1 vs 100"},
    {"05417000", "Game Gear", "00010000", "Adventures of Batman & Robin, The", "00030000", "Aerial Assault"},
    {"05444000", "Master System", "00010000", "Action Fighter", "00030000", "Aerial Assault"},
    {"05500000", "Dreamcast", "00010000", "102 Dalmatians - Puppies to the Rescue", "00030000", "18 Wheeler - American Pro Trucker (1)"},
    {"05501000", "Dreamcast", "00010000", "GaiaMaster - Kessen! Seiki-ou Densetsu", "00030000", "Gakkyuu-ou Yamazaki - Yamazaki Oukoku Daifunsou!"},
    {"05502000", "Dreamcast", "00010000", "Maboroshi Tsukiyo Taikenban", "00030000", "Maboroshi Tsukiyo"},
    {"05503000", "Dreamcast", "00010000", "Saka Tsuku Tokudai-gou - J.League Pro Soccer Club o Tsukurou!", "00030000", "Saka Tsuku Tokudai-gou 2 - J.League Pro Soccer Club o Tsukurou!"},
    {"05572000", "NES", "00010000", "1942", "00030000", "1943 - The Battle of Midway"},
    {"05574000", "Game Boy", "00010000", "Adventure Island II - Aliens in Paradise", "00030000", "Adventure Island"},
    {"05598000", "Neo Geo", "00010000", "2020 Super Baseball", "00030000", "3 Count Bout"},
    {"05625000", "Mega Drive", "00010000", "3 Ninjas Kick Back", "00030000", "6-Pak"},
    {"05748000", "Sega 32X", "00010000", "After Burner Complete", "00030000", "Amazing Spider-Man, The - Web of Fire"},
    {"05758000", "Sega CD", "00010000", "3 Ninjas Kick Back", "00030000", "A-X-101"},
    {"05852000", "Super Nintendo", "00010000", "3x3 Eyes - Juuma Houkan", "00030000", "7th Saga Redux, The"},
    {"05871000", "Commodore 64", "00010000", "007 - The Living Daylights", "00030000", "100000 Pyramid The"},
    {"05872000", "ColecoVision", "00010000", "3-D Genesis", "00030000", "3-D Ghost Attack"},
    {"05919000", "Atari 7800", "00010000", "Ace of Aces", "00030000", "Alien Brigade Joystick"},
    {"05920000", "PC Engine", "00010000", "Aero Blasters", "00030000", "Air Zonk"},
    {"05921000", "Atari Jaguar", "00100000", "Aircars", "00300000", "Alien vs Predator"},
};
using ConfirmedRetroSeries = std::bitset<std::size(RetroSeriesList)>;

inline auto FindRetroSeries(std::string_view id) -> const RetroSeries* {
    if (!id.starts_with("05") || id.size() != 16
        || !std::all_of(id.begin(), id.end(), [](char c) { return HexDigit(c) >= 0; })) return nullptr;
    const auto prefix = id.substr(0, 8);
    const auto series = std::lower_bound(std::begin(RetroSeriesList), std::end(RetroSeriesList), prefix,
        [](const auto& entry, std::string_view value) { return entry.prefix < value; });
    return series != std::end(RetroSeriesList) && series->prefix == prefix ? series : nullptr;
}

// The prefixes describe these collections rather than arbitrary Switch IDs.
inline auto NeroMagicPlatform(std::string_view server, std::string_view id, std::string_view name) -> std::string_view {
    if (server.ends_with('/')) server.remove_suffix(1);
    if (server != "https://neromagic.switchip.es" && server != "https://neromagic.switchip.es:443") return {};
    if (id.size() != 16 || !std::all_of(id.begin(), id.end(), [](char c) { return HexDigit(c) >= 0; })) return {};

    if (const auto series = FindRetroSeries(id)) return series->platform;
    return BrandedRetroPlatform(name);
}

inline bool IsNinMagicArtwork(std::string_view artwork_provider) {
    if (artwork_provider.ends_with('/')) artwork_provider.remove_suffix(1);
    return artwork_provider == "https://ninmagic.com/juegos" || artwork_provider == "https://www.ninmagic.com/juegos";
}

template<class Rows, class Eligible>
inline auto ApplyVerifiedRetroSeries(Rows& files, std::string_view artwork_provider, Eligible eligible,
    ConfirmedRetroSeries verified = {}) -> ConfirmedRetroSeries {
    if (!IsNinMagicArtwork(artwork_provider)) return {};
    ConfirmedRetroSeries first, second;
    for (const auto& file : files) {
        if (!eligible(file)) continue;
        const auto series = FindRetroSeries(file.id);
        if (!series || (!file.platform.empty() && file.platform != series->platform)) continue;
        const auto index = static_cast<size_t>(series - RetroSeriesList);
        const auto suffix = std::string_view{file.id}.substr(8);
        if (suffix == series->first_suffix && file.name == series->first_title) first.set(index);
        if (suffix == series->second_suffix && file.name == series->second_title) second.set(index);
    }
    verified |= first & second;
    for (auto& file : files) {
        if (!eligible(file)) continue;
        const auto series = FindRetroSeries(file.id);
        if (file.platform.empty() && series && verified[static_cast<size_t>(series - RetroSeriesList)]) {
            file.platform = series->platform;
            file.platform_learned = true;
        } else if (file.platform.empty() || (file.platform_learned && file.platform == "Nintendo Switch")) {
            if (const auto branded = BrandedRetroPlatform(file.name); !branded.empty()) {
                file.platform = branded;
                file.platform_learned = true;
            }
        }
    }
    return verified;
}

// The NinMagic NES set uses its own forwarder IDs. These are not console
// identifiers outside this collection. Its public catalogue identifies the
// following three titles by both ID and name, including on private mirrors.
template<class Rows, class Eligible>
inline bool ApplyNinMagicCollection(Rows& files, std::string_view artwork_provider, Eligible eligible, bool previously_confirmed = false) {
    if (!IsNinMagicArtwork(artwork_provider)) return false;

    constexpr std::array<std::pair<std::string_view, std::string_view>, 3> landmarks{{
        {"0557200000010000", "1942"},
        {"0557200003870000", "Linus Spacehead's Cosmic Crusade"},
        {"0557200007570000", "Zooming Secretary"},
    }};
    std::array<bool, landmarks.size()> present{};
    for (const auto& file : files) {
        if (!eligible(file) || (!file.platform.empty() && file.platform != "NES")) continue;
        for (size_t i = 0; i < landmarks.size(); ++i) {
            if (file.id == landmarks[i].first && file.name == landmarks[i].second) present[i] = true;
        }
    }
    const bool known_shop = previously_confirmed || std::count(present.begin(), present.end(), true) >= 2;
    for (auto& file : files) {
        if (!file.platform.empty() || !eligible(file)) continue;
        const std::string_view id = file.id;
        if (id.size() != 16 || !id.starts_with("05572000") || !id.ends_with("0000")) continue;
        const auto ordinal = id.substr(8, 4);
        if (!std::all_of(ordinal.begin(), ordinal.end(), [](char c) { return HexDigit(c) >= 0; })
            || !(HexDigit(ordinal.back()) & 1)) continue;
        if (!known_shop && std::none_of(landmarks.begin(), landmarks.end(), [&](const auto& confirmed) {
            return id == confirmed.first && file.name == confirmed.second;
        })) continue;
        file.platform = "NES";
        file.platform_learned = true;
    }
    return known_shop;
}

inline void ApplyNinMagicPlatforms(std::vector<File>& files, std::string_view artwork_provider) {
    const auto eligible = [](const File& file) {
        return !file.directory && (file.kind == Kind::Base || file.kind == Kind::Unknown);
    };
    ApplyVerifiedRetroSeries(files, artwork_provider, eligible);
    ApplyNinMagicCollection(files, artwork_provider, eligible);
}

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
        card.genre = metadata->genre;
        card.description = metadata->description;
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
