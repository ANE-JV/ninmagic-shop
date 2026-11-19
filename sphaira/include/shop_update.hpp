// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "shop_update_network.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace sphaira::shop_update {
inline constexpr char ManifestUrl[] = "https://raw.githubusercontent.com/ANE-JV/ninmagic-shop/main/update.json";
inline constexpr char Target[] = "/switch/NinMagicShop/NinMagic-Shop.nro";
inline constexpr char Download[] = "/config/NinMagicShop/update/download.pending";
inline constexpr char Candidate[] = "/switch/NinMagicShop/NinMagic-Shop.pending.nro";
inline constexpr char Rollback[] = "/switch/NinMagicShop/NinMagic-Shop.previous.nro";
inline constexpr char Recovery[] = "/switch/NinMagicShop/NinMagic-Shop.recovery.nro";
inline constexpr char Journal[] = "/config/NinMagicShop/update/install.json";
inline constexpr char JournalPending[] = "/config/NinMagicShop/update/install.pending.json";
inline constexpr uint64_t MaximumNroBytes = 64ULL * 1024 * 1024;
inline constexpr size_t MaximumManifestBytes = 32768;

struct Manifest {
    std::string version, download_url, sha256, notes;
    uint64_t bytes{};
};
struct Identity {
    std::string version, sha256;
    uint64_t bytes{};
    bool operator==(const Identity&) const = default;
};

bool IsVersionNewer(std::string_view installed, std::string_view published);
bool IsImmutableDownloadUrl(std::string_view url);
bool IsShopIdentity(std::string_view name, std::string_view author, std::string_view version);
bool ParseManifest(std::string_view json, Manifest& result, std::string& error);

// Small filesystem boundary shared by the native adapter and deterministic
// fault-injection tests. Operations never follow or create arbitrary paths.
enum class Entry { Missing, File, Other };
struct Storage {
    virtual ~Storage() = default;
    virtual bool Probe(const std::string&, Entry&, std::string&) = 0;
    virtual bool ReadText(const std::string&, size_t maximum, std::string&, std::string&) = 0;
    virtual bool WriteNew(const std::string&, std::string_view, std::string&) = 0;
    virtual bool CopyNew(const std::string& source, const std::string& destination, std::string&) = 0;
    virtual bool Move(const std::string& source, const std::string& destination, std::string&) = 0;
    virtual bool Remove(const std::string&, std::string&) = 0;
    virtual bool Commit(std::string&) = 0;
    // Must validate NRO layout, exact NACP identity and architecture before
    // reporting a streaming SHA-256, version and total file size.
    virtual bool Inspect(const std::string&, Identity&, std::string&) = 0;
};
using Fetch = std::function<bool(const std::string& destination, std::string& error)>;
bool Recover(Storage&, std::string& error);
bool Apply(Storage&, const Manifest&, const Fetch&, std::string& error);

// Native Switch entry points. Socket/curl services belong to App; these only
// create request handles and touch the Shop's fixed installation/cache paths.
bool Check(Manifest&, std::string& error, TransferHooks hooks = {});
bool RecoverInstalled(std::string& error);
bool Install(const Manifest&, std::string& error, TransferHooks hooks = {});
}
