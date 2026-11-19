// Exercises the real production option function and request-handle policy.
// The runner extracts SetCommonCurlOptions into production_curl_options.inc.
#include <curl/curl.h>
#include "utils/curl_request.hpp"
#include "utils/curl_diagnostic.hpp"
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#if defined(_WIN32) && !defined(__CYGWIN__)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

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
void PrivacyChecks() {
    const auto safe=diagnostic::SanitizeUrl("https://hidden-user:hidden-password@shop.example:8443/private-folder?token=hidden-query#hidden-fragment");
    assert(safe.origin=="https://shop.example:8443" && safe.endpoint_kind=="other");
    const auto login=diagnostic::SanitizeUrl("https://shop.example/login?next=hidden-query");
    assert(login.endpoint_kind=="login");
    diagnostic::Trace trace;
    std::string outgoing="GET /private-folder?token=hidden-query HTTP/1.1\r\nAuthorization: Basic hidden-authorization\r\nCookie: hidden-cookie\r\nX-hidden-name: hidden-value\r\n\r\n";
    diagnostic::Debug(nullptr,CURLINFO_HEADER_OUT,outgoing.data(),outgoing.size(),&trace);
    std::string incoming="HTTP/1.1 302 Found\r\nLocation: https://hidden-user:hidden-password@shop.example/login?token=hidden-query\r\nSet-Cookie: hidden-session\r\n\r\n";
    diagnostic::Debug(nullptr,CURLINFO_HEADER_IN,incoming.data(),incoming.size(),&trace);
    std::string verbose="hidden-verbose";
    diagnostic::Debug(nullptr,CURLINFO_TEXT,verbose.data(),verbose.size(),&trace);
    assert(trace.request_methods==std::vector<std::string>{"GET"});
    assert(trace.request_header_names.size()==1 && trace.unknown_request_headers==1);
    assert(trace.response_codes==std::vector<long>{302});
    for(const auto& names:trace.request_header_names)for(const auto& name:names)assert(name.find("hidden")==name.npos);
    for(const auto& name:trace.response_header_names)assert(name.find("hidden")==name.npos);
    assert(trace.curl_version.empty() && trace.effective_origin.empty());
}
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
    if (legacy && !isolated) {
        // Negative control: the old 0.1.1 policy reset only the easy handle.
        curl_easy_setopt(handle, CURLOPT_FRESH_CONNECT, 0L);
        curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 0L);
    }
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
    long status{}, new_connections{};
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(handle, CURLINFO_NUM_CONNECTS, &new_connections);
    const bool index = capture.body.find("<table>") != std::string::npos;
    std::printf("{\"case\":\"%s\",\"curl_version\":\"%s\",\"http\":%ld,\"code\":%d,\"new_connections\":%ld,\"shop_index\":%s,\"header_names\":[", name, curl_version_info(CURLVERSION_NOW)->version, status, int(result), new_connections, index ? "true" : "false");
    bool first=true;
    for (const auto& name : capture.headers) { std::printf("%s\"%s\"", first ? "" : ",", name.c_str()); first=false; }
    std::puts("]}");
    if (result != CURLE_OK || status != 200) return false;
    if (legacy && isolated) return index && new_connections == 1 && capture.headers == std::set<std::string>{"host","accept","accept-encoding","authorization"};
    return true;
}
int main(int argc, char** argv) {
    if (argc != 2 || curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return 2;
    PrivacyChecks();
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
