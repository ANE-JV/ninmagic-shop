// Exercises the real production option function and request-handle policy.
// The runner extracts SetCommonCurlOptions into production_curl_options.inc.
#include <curl/curl.h>
#include "utils/curl_request.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <strings.h>

void log_write(const char*, ...) {}
namespace ninmagic::tls {
bool ConfigureCurlTrust(CURL* handle) {
    return curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK;
}
}
namespace sphaira::curl {
constexpr char USER_AGENT[] = "Sphaira/1.1.1 NinMagicShop/wire-test";
enum { Flag_NoBody=1<<1, Flag_KeepErrorBody=1<<2, Flag_LegacyOwnfoil=1<<3, Flag_NoRedirect=1<<4 };
struct UserPass { std::string m_user, m_pass; };
struct Api {
    std::string url, method, empty;
    UserPass credentials{"wire-user", "wire-pass"};
    unsigned flags{};
    auto GetFlags() const { return flags; }
    auto& GetUrl() const { return url; }
    auto& GetCustomRequest() const { return method; }
    auto& GetUserPass() const { return credentials; }
    bool GetPreemptiveAuth() const { return true; }
    auto& GetBearer() const { return empty; }
    auto& GetPubKey() const { return empty; }
    auto& GetPrivKey() const { return empty; }
    int GetPort() const { return 0; }
};
int ProgressCallbackFunc(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return 0; }
CURLSH* g_curl_share{};
#define CURL_EASY_SETOPT_LOG(handle, option, value) do { \
    if (curl_easy_setopt(handle, option, value) != CURLE_OK) std::abort(); \
} while (0)
#include "production_curl_options.inc"
}
using namespace sphaira::curl;
struct Capture { std::string body; std::set<std::string> headers; };
size_t Body(char* data, size_t size, size_t count, void* opaque) {
    const auto bytes = size * count;
    static_cast<Capture*>(opaque)->body.append(data, bytes);
    return bytes;
}
int Debug(CURL*, curl_infotype type, char* data, size_t size, void* opaque) {
    if (type != CURLINFO_HEADER_OUT) return 0;
    auto& capture = *static_cast<Capture*>(opaque);
    const std::string block{data, size};
    for (size_t start{}; start < block.size();) {
        auto end = block.find('\n', start);
        if (end == block.npos) end = block.size();
        const auto line = block.substr(start, end-start);
        const auto colon = line.find(':');
        if (colon != line.npos && colon) {
            auto key = line.substr(0, colon);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            capture.headers.insert(std::move(key));
        }
        start = end + 1;
    }
    return 0;
}
bool Run(CURL* pooled, const char* url, const char* name, bool legacy, bool isolated, const char* method="") {
    RequestHandle scoped{pooled, legacy && isolated};
    auto handle = scoped.Get();
    if (!handle) return false;
    Api request; request.url=url; request.method=method;
    request.flags=Flag_KeepErrorBody | (legacy ? Flag_LegacyOwnfoil : 0);
    curl_easy_reset(handle);
    if (!SetCommonCurlOptions(handle, request)) return false;
    Capture capture;
    curl_slist* headers{};
    if (legacy && !AddLegacyHeaders(headers)) return false;
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_NOPROXY, "127.0.0.1,localhost");
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, Body);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &capture);
    curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(handle, CURLOPT_DEBUGFUNCTION, Debug);
    curl_easy_setopt(handle, CURLOPT_DEBUGDATA, &capture);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);
    const auto result = curl_easy_perform(handle);
    curl_slist_free_all(headers);
    long status{};
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    const bool index = capture.body.find("<table>") != std::string::npos;
    std::printf("{\"case\":\"%s\",\"http\":%ld,\"code\":%d,\"shop_index\":%s,\"header_names\":[", name, status, int(result), index ? "true" : "false");
    bool first=true;
    for (const auto& name : capture.headers) { std::printf("%s\"%s\"", first ? "" : ",", name.c_str()); first=false; }
    std::puts("]}");
    if (result != CURLE_OK || status != 200) return false;
    if (legacy && isolated) return index && capture.headers == std::set<std::string>{"host","accept","accept-encoding","authorization"};
    return true;
}
int main(int argc, char** argv) {
    if (argc != 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 2;
    g_curl_share=curl_share_init();
    for (auto type : {CURL_LOCK_DATA_COOKIE,CURL_LOCK_DATA_DNS,CURL_LOCK_DATA_SSL_SESSION,CURL_LOCK_DATA_CONNECT,CURL_LOCK_DATA_PSL}) curl_share_setopt(g_curl_share,CURLSHOPT_SHARE,type);
    auto pooled=curl_easy_init();
    bool ok=Run(pooled,argv[1],"fresh_legacy",true,true);
    ok=Run(pooled,argv[1],"native_options",false,false,"OPTIONS") && ok;
    ok=Run(pooled,argv[1],"native_get",false,false) && ok;
    ok=Run(pooled,argv[1],"unsafe_reset_control",true,false) && ok;
    ok=Run(pooled,argv[1],"legacy_after_native",true,true) && ok;
    ok=Run(pooled,argv[1],"legacy_repeat",true,true) && ok;
    curl_easy_cleanup(pooled);
    curl_share_cleanup(g_curl_share);
    curl_global_cleanup();
    return ok ? 0 : 1;
}
