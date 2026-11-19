// SPDX-License-Identifier: GPL-3.0-or-later
#include "shop_update_network.hpp"
#include "shop_update.hpp"
#include "ninmagic_tls.hpp"
#include "fs.hpp"
#include <curl/curl.h>
#include <limits>
#include <utility>

namespace sphaira::shop_update {
namespace {
thread_local TransferHooks transfer_hooks;
struct Sink {
    fs::File* file;
    uint64_t written{}, maximum{};
    bool too_large{};
};
size_t WriteBody(char* data, size_t size, size_t count, void* context) {
    auto& sink = *static_cast<Sink*>(context);
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    const auto bytes = size * count;
    if (sink.written > sink.maximum || bytes > sink.maximum - sink.written) { sink.too_large = true; return 0; }
    if (transfer_hooks.cancelled && transfer_hooks.cancelled()) return 0;
    if (R_FAILED(sink.file->Write(sink.written, data, bytes, FsWriteOption_Flush))) return 0;
    sink.written += bytes;
    return bytes;
}
int Progress(void*, curl_off_t total, curl_off_t current, curl_off_t, curl_off_t) {
    if (transfer_hooks.cancelled && transfer_hooks.cancelled()) return 1;
    if (transfer_hooks.progress) transfer_hooks.progress(current > 0 ? current : 0, total > 0 ? total : 0);
    return 0;
}
}
void setUpdateTransferHooks(TransferHooks hooks) { transfer_hooks = std::move(hooks); }
void updateNetworkExit() { transfer_hooks = {}; }
bool downloadHttpsFile(const std::string& url, const std::string& path, std::string& error) {
    error.clear();
    const bool metadata = path == "/config/NinMagicShop/update/manifest.pending.json" && url == ManifestUrl;
    const bool executable = path == Download && IsImmutableDownloadUrl(url);
    if (!metadata && !executable) { error = "La descarga no pertenece al canal oficial de NinMagic Shop."; return false; }
    fs::FsNativeSd disk;
    if (disk.FileExists(path) || disk.DirExists(path)) { error = "El archivo temporal de descarga ya existe; no se sobrescribirá."; return false; }
    if (R_FAILED(disk.CreateFile(path, 0))) { error = "No se pudo crear la descarga en la microSD."; return false; }
    bool success{};
    {
        fs::File file;
        if (R_FAILED(disk.OpenFile(path, FsOpenMode_Write | FsOpenMode_Append, &file))) { disk.DeleteFile(path); error = "No se pudo abrir la descarga en la microSD."; return false; }
        auto handle = curl_easy_init();
        if (!handle) { error = "No se pudo abrir una conexión HTTPS."; }
        else {
            const uint64_t maximum = metadata ? MaximumManifestBytes : MaximumNroBytes;
            Sink sink{&file, 0, maximum, false};
            char curl_error[CURL_ERROR_SIZE]{};
            const auto set = [handle](CURLoption option, auto value) { return curl_easy_setopt(handle, option, value) == CURLE_OK; };
            const bool configured = ninmagic::tls::ConfigureCurlTrust(handle) &&
                set(CURLOPT_URL, url.c_str()) && set(CURLOPT_USERAGENT, "NinMagic-Shop/0.1.0") &&
                set(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS)) && set(CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS)) &&
                set(CURLOPT_FOLLOWLOCATION, 1L) && set(CURLOPT_MAXREDIRS, 5L) && set(CURLOPT_FAILONERROR, 1L) &&
                set(CURLOPT_CONNECTTIMEOUT, 15L) && set(CURLOPT_TIMEOUT, metadata ? 45L : 360L) &&
                set(CURLOPT_LOW_SPEED_LIMIT, 512L) && set(CURLOPT_LOW_SPEED_TIME, 30L) && set(CURLOPT_NOSIGNAL, 1L) &&
                set(CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maximum)) && set(CURLOPT_WRITEFUNCTION, WriteBody) && set(CURLOPT_WRITEDATA, &sink) &&
                set(CURLOPT_NOPROGRESS, 0L) && set(CURLOPT_XFERINFOFUNCTION, Progress) && set(CURLOPT_ERRORBUFFER, curl_error);
            const auto result = configured ? curl_easy_perform(handle) : CURLE_FAILED_INIT;
            long status{};
            const bool response = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK;
            success = configured && result == CURLE_OK && response && status == 200 && sink.written && !sink.too_large;
            if (!success) {
                if (!configured) error = "No se pudo activar HTTPS con certificados verificados.";
                else if (transfer_hooks.cancelled && transfer_hooks.cancelled()) error = "Actualización cancelada antes de sustituir la aplicación.";
                else if (sink.too_large) error = "La descarga supera el tamaño permitido.";
                else if (result == CURLE_OK) error = "El servidor de actualizaciones respondió HTTP " + std::to_string(status) + ".";
                else error = "No se pudo descargar por HTTPS: " + std::string(curl_error[0] ? curl_error : curl_easy_strerror(result));
            }
            curl_easy_cleanup(handle);
        }
    }
    if (!success) disk.DeleteFile(path);
    else if (R_FAILED(disk.Commit())) { error = "La microSD no confirmó la descarga completa."; return false; }
    return success;
}
}
