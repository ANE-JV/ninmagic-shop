// SPDX-License-Identifier: GPL-3.0-or-later
#include "shop_update.hpp"
#include <yyjson.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <map>
#include <vector>

#ifndef NMSHOP_UPDATE_HOST_TEST
#include "fs.hpp"
#include "nro.hpp"
#include "nacp_compat.hpp"
#include <switch.h>
#include <cstdio>
#endif

namespace sphaira::shop_update {
namespace {
using Version = std::array<uint32_t, 3>;
bool ParseVersion(std::string_view text, Version& out) {
    for (size_t index = 0; index != out.size(); ++index) {
        const auto dot = text.find('.');
        const auto component = text.substr(0, dot);
        if (component.empty() || component.size() > 5 || (component.size() > 1 && component.front() == '0')) return false;
        auto result = std::from_chars(component.data(), component.data() + component.size(), out[index]);
        if (result.ec != std::errc{} || result.ptr != component.data() + component.size() || out[index] > 65535) return false;
        if (index == 2) return dot == std::string_view::npos;
        if (dot == std::string_view::npos) return false;
        text.remove_prefix(dot + 1);
    }
    return false;
}
bool HashValid(std::string_view hash) {
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
bool ReleaseValid(const Manifest& release) {
    Version version{};
    return ParseVersion(release.version, version) && IsImmutableDownloadUrl(release.download_url) &&
        HashValid(release.sha256) && release.bytes >= 0x4080 + 56 && release.bytes <= MaximumNroBytes;
}
bool JsonFields(yyjson_val* object, std::initializer_list<std::string_view> names) {
    if (!yyjson_is_obj(object) || yyjson_obj_size(object) != names.size()) return false;
    std::map<std::string, bool> seen;
    size_t index, count; yyjson_val *key, *value;
    yyjson_obj_foreach(object, index, count, key, value) {
        (void)value;
        std::string name(yyjson_get_str(key), yyjson_get_len(key));
        if (std::find(names.begin(), names.end(), name) == names.end() || !seen.emplace(name, true).second) return false;
    }
    return true;
}
bool JsonText(yyjson_val* object, const char* key, size_t limit, std::string& text) {
    const auto value = yyjson_obj_get(object, key);
    if (!yyjson_is_str(value) || yyjson_get_len(value) > limit) return false;
    text.assign(yyjson_get_str(value), yyjson_get_len(value));
    return text.find('\0') == std::string::npos;
}
bool JsonNumber(yyjson_val* object, const char* key, uint64_t& number) {
    const auto value = yyjson_obj_get(object, key);
    if (!yyjson_is_uint(value)) return false;
    number = yyjson_get_uint(value);
    return true;
}
bool Fail(std::string& error, const char* message) { error = message; return false; }
Identity Published(const Manifest& release) { return {release.version, release.sha256, release.bytes}; }
std::string Backup(const Identity& identity) { return "/config/NinMagicShop/update/backup-" + identity.sha256 + ".nro"; }
bool Matching(Storage& disk, const std::string& path, const Identity& expected, std::string& error) {
    Identity actual;
    return disk.Inspect(path, actual, error) && (actual == expected || Fail(error, "El archivo no coincide con la versión, el tamaño y SHA-256 verificados."));
}
bool RegularOrMissing(Storage& disk, const std::string& path, bool& exists, std::string& error) {
    Entry entry{};
    if (!disk.Probe(path, entry, error)) return false;
    exists = entry != Entry::Missing;
    return entry != Entry::Other || Fail(error, "Una ruta del actualizador no es un archivo regular; se conserva sin cambios.");
}
bool PreservePartial(Storage& disk, const std::string& path, const std::string& tag, std::string& error) {
    for (unsigned serial = 0; serial != 10000; ++serial) {
        const auto destination = "/config/NinMagicShop/update/interrupted-" + tag + "-" + std::to_string(serial) + ".data";
        Entry entry;
        if (!disk.Probe(destination, entry, error)) return false;
        if (entry == Entry::Missing) return disk.Move(path, destination, error);
    }
    return Fail(error, "No queda un nombre libre para conservar el archivo interrumpido.");
}
bool TidyOwned(Storage& disk, const std::string& path, const Identity& expected, const std::string& tag, std::string& error) {
    bool exists;
    if (!RegularOrMissing(disk, path, exists, error)) return false;
    if (!exists) return true;
    std::string reason;
    if (Matching(disk, path, expected, reason)) return disk.Remove(path, error);
    // A durable transaction claimed this scratch path before creating it.
    // Keep incomplete or damaged bytes in a distinct, never-overwritten file.
    return PreservePartial(disk, path, tag, error);
}
struct Transaction { Identity before, after; };
std::string Encode(const Transaction& transaction) {
    // Every interpolated field was validated as decimal version or lowercase
    // hex; the recovery record never contains URLs, notes or arbitrary paths.
    return "{\"schema\":1,\"before_version\":\"" + transaction.before.version +
        "\",\"before_sha256\":\"" + transaction.before.sha256 + "\",\"before_bytes\":" + std::to_string(transaction.before.bytes) +
        ",\"after_version\":\"" + transaction.after.version + "\",\"after_sha256\":\"" + transaction.after.sha256 +
        "\",\"after_bytes\":" + std::to_string(transaction.after.bytes) + "}";
}
bool Decode(std::string_view text, Transaction& transaction) {
    if (text.size() > 2048 || text.find('\0') != std::string_view::npos) return false;
    const auto doc = yyjson_read(text.data(), text.size(), 0);
    if (!doc) return false;
    const auto root = yyjson_doc_get_root(doc);
    uint64_t schema{}; Version before{}, after{};
    const bool ok = JsonFields(root, {"schema", "before_version", "before_sha256", "before_bytes", "after_version", "after_sha256", "after_bytes"}) &&
        JsonNumber(root, "schema", schema) && schema == 1 &&
        JsonText(root, "before_version", 32, transaction.before.version) && JsonText(root, "before_sha256", 64, transaction.before.sha256) && JsonNumber(root, "before_bytes", transaction.before.bytes) &&
        JsonText(root, "after_version", 32, transaction.after.version) && JsonText(root, "after_sha256", 64, transaction.after.sha256) && JsonNumber(root, "after_bytes", transaction.after.bytes) &&
        ParseVersion(transaction.before.version, before) && ParseVersion(transaction.after.version, after) && after > before &&
        HashValid(transaction.before.sha256) && HashValid(transaction.after.sha256) &&
        transaction.before.bytes >= 0x4080 + 56 && transaction.before.bytes <= MaximumNroBytes &&
        transaction.after.bytes >= 0x4080 + 56 && transaction.after.bytes <= MaximumNroBytes;
    yyjson_doc_free(doc);
    return ok;
}
bool LoadTransaction(Storage& disk, Transaction& transaction, bool& exists, std::string& error) {
    exists = false;
    std::string first;
    for (const auto path : {Journal, JournalPending}) {
        bool found;
        if (!RegularOrMissing(disk, path, found, error)) return false;
        if (!found) continue;
        std::string text;
        if (!disk.ReadText(path, 2048, text, error) || !Decode(text, transaction)) return Fail(error, "El registro de actualización está dañado; conserva el NRO y sus respaldos.");
        if (exists && first != text) return Fail(error, "Hay dos registros de actualización diferentes; se conservan para revisión.");
        exists = true; first = std::move(text);
    }
    return true;
}
bool Finish(Storage& disk, const Transaction& transaction, std::string& error) {
    Identity installed;
    if (!disk.Inspect(Target, installed, error) || (installed != transaction.before && installed != transaction.after)) return Fail(error, "El NRO cambió antes de completar el registro; se conserva la recuperación.");
    const auto backup = Backup(transaction.before);
    if (installed == transaction.after) {
        // A replaced application is not complete without its verified backup.
        if (!Matching(disk, backup, transaction.before, error)) return false;
    } else {
        bool exists;
        if (!RegularOrMissing(disk, backup, exists, error)) return false;
        std::string reason;
        if (exists && !Matching(disk, backup, transaction.before, reason) && !PreservePartial(disk, backup, "backup-" + transaction.before.sha256, error)) return false;
    }
    if (!TidyOwned(disk, Candidate, transaction.after, "candidate-" + transaction.after.sha256, error) ||
        !TidyOwned(disk, Rollback, transaction.before, "rollback-" + transaction.before.sha256, error) ||
        !TidyOwned(disk, Recovery, transaction.before, "recovery-" + transaction.before.sha256, error) || !disk.Commit(error)) return false;
    for (const auto path : {JournalPending, Journal}) {
        bool exists;
        if (!RegularOrMissing(disk, path, exists, error) || (exists && !disk.Remove(path, error))) return false;
    }
    return disk.Commit(error);
}
}

bool IsVersionNewer(std::string_view installed, std::string_view published) {
    Version old{}, next{};
    return ParseVersion(installed, old) && ParseVersion(published, next) && next > old;
}
bool IsImmutableDownloadUrl(std::string_view url) {
    constexpr std::string_view prefix = "https://raw.githubusercontent.com/ANE-JV/ninmagic-shop/";
    constexpr std::string_view filename = "/NinMagic-Shop.nro";
    if (url.size() > 2048 || !url.starts_with(prefix) || !url.ends_with(filename)) return false;
    const auto rest = url.substr(prefix.size());
    if (rest.size() <= 41 + filename.size() || rest[40] != '/' || rest.substr(0, 40).find_first_not_of("0123456789abcdef") != std::string_view::npos) return false;
    if (url.find_first_of("?#@%\\") != std::string_view::npos || url.find("/../") != std::string_view::npos || url.find("/./") != std::string_view::npos) return false;
    return std::none_of(url.begin(), url.end(), [](unsigned char byte) { return byte <= 32 || byte == 127; });
}
bool IsShopIdentity(std::string_view name, std::string_view author, std::string_view version) {
    Version parsed{};
    return name == "NinMagic Shop" && author == "NinMagic" && ParseVersion(version, parsed);
}
bool ParseManifest(std::string_view json, Manifest& result, std::string& error) {
    error.clear(); result = {};
    if (json.empty() || json.size() > MaximumManifestBytes || json.find('\0') != std::string_view::npos) return Fail(error, "El manifiesto de NinMagic Shop está vacío o supera sus límites.");
    const auto doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) return Fail(error, "No se pudo leer el JSON de actualización de NinMagic Shop.");
    const auto root = yyjson_doc_get_root(doc);
    Manifest release; uint64_t schema{}; std::string app;
    const bool ok = JsonFields(root, {"schema", "app", "version", "download_url", "bytes", "sha256", "notes"}) &&
        JsonNumber(root, "schema", schema) && schema == 1 && JsonText(root, "app", 64, app) && app == "NinMagic Shop" &&
        JsonText(root, "version", 32, release.version) && JsonText(root, "download_url", 2048, release.download_url) &&
        JsonNumber(root, "bytes", release.bytes) && JsonText(root, "sha256", 64, release.sha256) && JsonText(root, "notes", 16384, release.notes) && ReleaseValid(release);
    yyjson_doc_free(doc);
    if (!ok) return Fail(error, "El manifiesto no corresponde a NinMagic Shop o contiene una descarga sin fijar a un commit.");
    result = std::move(release);
    return true;
}
bool Recover(Storage& disk, std::string& error) {
    error.clear(); Transaction transaction; bool pending;
    if (!LoadTransaction(disk, transaction, pending, error)) {
        // A power cut can leave the first journal write incomplete. Without
        // any renamed/staged executable, a valid installed Shop proves there
        // is no target replacement to roll back. Preserve damaged records.
        for (const auto path : {Candidate, Rollback, Recovery}) {
            bool exists; std::string reason;
            if (!RegularOrMissing(disk, path, exists, reason) || exists) return false;
        }
        Identity installed; std::string reason;
        if (!disk.Inspect(Target, installed, reason)) return false;
        for (const auto path : {JournalPending, Journal}) {
            bool exists;
            if (!RegularOrMissing(disk, path, exists, reason) || (exists && !PreservePartial(disk, path, "journal", reason))) return false;
        }
        error.clear(); return disk.Commit(error);
    }
    if (!pending) {
        for (const auto path : {Candidate, Rollback, Recovery}) {
            bool exists;
            if (!RegularOrMissing(disk, path, exists, error)) return false;
            if (exists) return Fail(error, "Hay un NRO temporal sin registro de actualización; se conserva para revisión.");
        }
        return true;
    }
    bool installed;
    if (!RegularOrMissing(disk, Target, installed, error)) return false;
    if (installed) {
        Identity actual;
        if (!disk.Inspect(Target, actual, error)) return false;
        if (actual != transaction.before && actual != transaction.after) return Fail(error, "El NRO instalado cambió fuera de esta actualización; no se sustituirá.");
    } else {
        if (!Matching(disk, Backup(transaction.before), transaction.before, error)) return false;
        bool previous;
        if (!RegularOrMissing(disk, Rollback, previous, error)) return false;
        if (previous) {
            if (!Matching(disk, Rollback, transaction.before, error) || !disk.Move(Rollback, Target, error)) return false;
        } else {
            // Keep the download candidate separate from recovery staging.
            // The immutable backup remains present after the final rename.
            bool recovery;
            if (!RegularOrMissing(disk, Recovery, recovery, error)) return false;
            if (recovery) {
                std::string reason;
                if (!Matching(disk, Recovery, transaction.before, reason)) {
                    if (!PreservePartial(disk, Recovery, "recovery-" + transaction.before.sha256, error)) return false;
                    recovery = false;
                }
            }
            if (!recovery && !disk.CopyNew(Backup(transaction.before), Recovery, error)) return false;
            if (!Matching(disk, Recovery, transaction.before, error) || !disk.Move(Recovery, Target, error)) return false;
        }
        if (!disk.Commit(error) || !Matching(disk, Target, transaction.before, error)) return false;
    }
    return Finish(disk, transaction, error);
}
bool Apply(Storage& disk, const Manifest& release, const Fetch& fetch, std::string& error) {
    error.clear();
    if (!ReleaseValid(release)) return Fail(error, "La actualización no tiene una identidad, URL o SHA-256 válidos.");
    if (!Recover(disk, error)) return false;
    Identity installed;
    if (!disk.Inspect(Target, installed, error)) return false;
    if (!IsVersionNewer(installed.version, release.version)) return Fail(error, "La versión publicada ya está instalada o es anterior. Cierra y abre NinMagic Shop si acabas de actualizar.");
    bool cached;
    if (!RegularOrMissing(disk, Download, cached, error) || (cached && !disk.Remove(Download, error))) return false;
    if (!fetch(Download, error) || !Matching(disk, Download, Published(release), error)) return false;
    const Transaction transaction{installed, Published(release)};
    const auto backup = Backup(installed);
    bool backed_up;
    if (!RegularOrMissing(disk, backup, backed_up, error)) return false;
    if (backed_up && !Matching(disk, backup, installed, error)) return false;
    // Record ownership before any backup/candidate copy. Recovery can then
    // preserve a partial copy after a crash without guessing which app owns it.
    if (!disk.WriteNew(JournalPending, Encode(transaction), error) || !disk.Commit(error) || !disk.Move(JournalPending, Journal, error) || !disk.Commit(error)) return false;
    if ((!backed_up && !disk.CopyNew(Target, backup, error)) || !Matching(disk, backup, installed, error) ||
        !disk.CopyNew(Download, Candidate, error) || !Matching(disk, Candidate, transaction.after, error)) return false;
    // Cancellation belongs to fetch. Once the transaction is durable, finish
    // or recover it; abandoning the two renames would strand the target path.
    const bool replaced = Matching(disk, Target, installed, error) && disk.Move(Target, Rollback, error) &&
        disk.Move(Candidate, Target, error) && disk.Commit(error) && Matching(disk, Target, transaction.after, error);
    if (!replaced) {
        const auto cause = error;
        std::string recovery;
        const bool restored = Recover(disk, recovery);
        error = cause + (restored ? " Se ha conservado o recuperado un NRO verificado." : " La recuperación sigue pendiente: " + recovery);
        return false;
    }
    if (!Finish(disk, transaction, error)) return false;
    return disk.Remove(Download, error) && disk.Commit(error);
}

#ifndef NMSHOP_UPDATE_HOST_TEST
namespace {
class NativeStorage final : public Storage {
    fs::FsNativeSd m_disk;
    bool ResultOk(Result result, std::string& error) {
        if (R_SUCCEEDED(result)) return true;
        char code[16]; std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(result));
        error = "Error de microSD (0x" + std::string(code) + "). Se conservan los respaldos.";
        return false;
    }
public:
    bool Prepare(std::string& error) {
        return ResultOk(m_disk.GetFsOpenResult(), error) && ResultOk(m_disk.CreateDirectoryRecursively("/config/NinMagicShop/update"), error);
    }
    bool Probe(const std::string& path, Entry& entry, std::string& error) override {
        FsDirEntryType type{};
        const auto rc = m_disk.GetEntryType(path, &type);
        if (rc == FsError_PathNotFound) { entry = Entry::Missing; return true; }
        if (!ResultOk(rc, error)) return false;
        entry = type == FsDirEntryType_File ? Entry::File : Entry::Other;
        return true;
    }
    bool ReadText(const std::string& path, size_t maximum, std::string& text, std::string& error) override {
        fs::File file; s64 bytes{}; u64 read{};
        if (!ResultOk(m_disk.OpenFile(path, FsOpenMode_Read, &file), error) || !ResultOk(file.GetSize(&bytes), error)) return false;
        if (bytes < 0 || static_cast<uint64_t>(bytes) > maximum) return Fail(error, "El archivo de actualización supera su límite.");
        text.resize(bytes);
        return ResultOk(file.Read(0, text.data(), bytes, FsReadOption_None, &read), error) && (read == static_cast<uint64_t>(bytes) || Fail(error, "Lectura incompleta de microSD."));
    }
    bool WriteNew(const std::string& path, std::string_view text, std::string& error) override {
        if (!ResultOk(m_disk.CreateFile(path, text.size()), error)) return false;
        fs::File file;
        return ResultOk(m_disk.OpenFile(path, FsOpenMode_Write, &file), error) && ResultOk(file.Write(0, text.data(), text.size(), FsWriteOption_Flush), error);
    }
    bool CopyNew(const std::string& source, const std::string& destination, std::string& error) override {
        fs::File from, to; s64 bytes{};
        if (!ResultOk(m_disk.OpenFile(source, FsOpenMode_Read, &from), error) || !ResultOk(from.GetSize(&bytes), error)) return false;
        if (bytes <= 0 || static_cast<uint64_t>(bytes) > MaximumNroBytes) return Fail(error, "El NRO de origen supera el límite de 64 MiB.");
        if (!ResultOk(m_disk.CreateFile(destination, bytes), error) || !ResultOk(m_disk.OpenFile(destination, FsOpenMode_Write, &to), error)) return false;
        std::array<unsigned char, 65536> block{};
        for (s64 offset = 0; offset < bytes;) {
            const auto count = std::min<s64>(block.size(), bytes - offset); u64 read{};
            if (!ResultOk(from.Read(offset, block.data(), count, FsReadOption_None, &read), error) || read != static_cast<uint64_t>(count) || !ResultOk(to.Write(offset, block.data(), count, FsWriteOption_Flush), error)) return false;
            offset += count;
        }
        return true;
    }
    bool Move(const std::string& source, const std::string& destination, std::string& error) override { return ResultOk(m_disk.RenameFile(source, destination), error); }
    bool Remove(const std::string& path, std::string& error) override { return ResultOk(m_disk.DeleteFile(path), error); }
    bool Commit(std::string& error) override { return ResultOk(m_disk.Commit(), error); }
    bool Inspect(const std::string& path, Identity& identity, std::string& error) override {
        fs::File file; s64 bytes{};
        if (!ResultOk(m_disk.OpenFile(path, FsOpenMode_Read, &file), error) || !ResultOk(file.GetSize(&bytes), error)) return false;
        if (bytes < 0x4080 + 56 || static_cast<uint64_t>(bytes) > MaximumNroBytes) return Fail(error, "El NRO debe ocupar entre 16 KiB y 64 MiB.");
        const auto read_exact = [&](uint64_t offset, void* output, uint64_t size) {
            u64 read{};
            return offset <= static_cast<uint64_t>(bytes) && size <= static_cast<uint64_t>(bytes) - offset &&
                ResultOk(file.Read(offset, output, size, FsReadOption_None, &read), error) && read == size;
        };
        NroData header{}; NroAssetHeader assets{};
        if (!read_exact(0, &header, sizeof(header)) || header.header.magic != NROHEADER_MAGIC || header.header.size < sizeof(header) ||
            !read_exact(header.header.size, &assets, sizeof(assets)) || assets.magic != NROASSETHEADER_MAGIC || assets.version != NROASSETHEADER_VERSION) return Fail(error, "Las cabeceras del NRO descargado no son válidas.");
        uint64_t last{};
        for (const auto& segment : header.header.segments) {
            if (!segment.size || segment.file_off < last || static_cast<uint64_t>(segment.file_off) + segment.size > header.header.size) return Fail(error, "Los segmentos del NRO no son válidos.");
            last = static_cast<uint64_t>(segment.file_off) + segment.size;
        }
        if (header.header.segments[0].file_off != 0) return Fail(error, "El primer segmento NRO no comienza en cero.");
        last = sizeof(assets);
        for (const auto& section : {assets.icon, assets.nacp, assets.romfs}) {
            const auto available = static_cast<uint64_t>(bytes) - header.header.size;
            if (!section.size || section.offset < last || section.offset > available || section.size > available - section.offset) return Fail(error, "Los recursos del NRO se solapan o están incompletos.");
            last = section.offset + section.size;
        }
        NacpStruct control{};
        if (assets.nacp.size != sizeof(control) || !read_exact(static_cast<uint64_t>(header.header.size) + assets.nacp.offset, &control, sizeof(control)) || !nro_is_native(path)) return Fail(error, "El NRO no es una aplicación nativa válida para esta consola.");
        const auto entries = NacpLanguageEntries(control);
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& item = entries[i];
            if (!std::memchr(item.name, 0, sizeof(item.name)) || !std::memchr(item.author, 0, sizeof(item.author))) return Fail(error, "La identidad NACP no termina correctamente.");
            if ((!i || item.name[0] || item.author[0]) && !IsShopIdentity(item.name, item.author, "0.1.0")) return Fail(error, "El ejecutable no corresponde exactamente a NinMagic Shop de NinMagic.");
        }
        if (!std::memchr(control.display_version, 0, sizeof(control.display_version)) || !IsShopIdentity(entries[0].name, entries[0].author, control.display_version)) return Fail(error, "La versión NACP de NinMagic Shop no es válida.");
        Sha256Context digest; sha256ContextCreate(&digest);
        std::array<unsigned char, 65536> block{};
        for (s64 offset = 0; offset < bytes;) {
            const auto count = std::min<s64>(block.size(), bytes - offset); u64 read{};
            if (!ResultOk(file.Read(offset, block.data(), count, FsReadOption_None, &read), error) || read != static_cast<uint64_t>(count)) return false;
            sha256ContextUpdate(&digest, block.data(), count); offset += count;
        }
        std::array<unsigned char, 32> hash{}; sha256ContextGetHash(&digest, hash.data());
        identity.sha256.clear(); constexpr char hex[] = "0123456789abcdef";
        for (auto byte : hash) { identity.sha256 += hex[byte >> 4]; identity.sha256 += hex[byte & 15]; }
        identity.bytes = bytes; identity.version = control.display_version;
        return true;
    }
};
}
bool Check(Manifest& release, std::string& error, TransferHooks hooks) {
    NativeStorage disk;
    if (!disk.Prepare(error)) return false;
    const std::string manifest_path = "/config/NinMagicShop/update/manifest.pending.json";
    Entry state{};
    if (!disk.Probe(manifest_path, state, error) || state == Entry::Other) return Fail(error, "La caché del manifiesto no es un archivo regular.");
    if (state == Entry::File && !disk.Remove(manifest_path, error)) return false;
    setUpdateTransferHooks(std::move(hooks));
    const bool downloaded = downloadHttpsFile(ManifestUrl, manifest_path, error);
    updateNetworkExit();
    if (!downloaded) return false;
    std::string json;
    const bool ok = disk.ReadText(manifest_path, MaximumManifestBytes, json, error) && ParseManifest(json, release, error);
    std::string ignored; disk.Remove(manifest_path, ignored);
    return ok;
}
bool RecoverInstalled(std::string& error) { NativeStorage disk; return disk.Prepare(error) && Recover(disk, error); }
bool Install(const Manifest& release, std::string& error, TransferHooks hooks) {
    NativeStorage disk;
    if (!disk.Prepare(error)) return false;
    const auto cancelled = hooks.cancelled;
    setUpdateTransferHooks(std::move(hooks));
    const bool ok = Apply(disk, release, [&](const std::string& destination, std::string& failure) {
        if (cancelled && cancelled()) return Fail(failure, "Actualización cancelada antes de sustituir la aplicación.");
        if (!downloadHttpsFile(release.download_url, destination, failure)) return false;
        return !(cancelled && cancelled()) || Fail(failure, "Actualización cancelada antes de sustituir la aplicación.");
    }, error);
    updateNetworkExit();
    return ok;
}
#endif
}
