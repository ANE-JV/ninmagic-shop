// Host harness for the real Yati Http reader. The Switch worker is replaced by
// a synchronous libcurl transfer; the runner serves a tiny in-memory file.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>
#include <curl/curl.h>
#include "ninmagic_http_policy.hpp"
#if defined(_WIN32) && !defined(__CYGWIN__)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

using s64 = std::int64_t;
using u64 = std::uint64_t;
using Result = int;
enum { Result_YatiHttpReadFailed = 1, Result_CurlFailedEasyInit, Result_TransferCancelled,
    Result_YatiHttpFileNotFound, Result_YatiHttpAccessDenied };
#define R_UNLESS(condition, failure) do { if (!(condition)) return (failure); } while (0)
#define R_TRY(value) do { const Result result = (value); if (result) return result; } while (0)
#define R_FAILED(value) ((value) != 0)
#define R_THROW(value) return (value)
#define R_SUCCEED() return 0
template<class F> struct ScopeExit { F function; ~ScopeExit() { function(); } };
#define UNIQUE_(a, b) a##b
#define UNIQUE(a, b) UNIQUE_(a, b)
#define ON_SCOPE_EXIT(...) ScopeExit UNIQUE(cleanup_, __LINE__){[&] { __VA_ARGS__; }}
struct UEvent {};
void ueventCreate(UEvent*, bool) {}
void ueventSignal(UEvent*) {}
int waiterForUEvent(UEvent*) { return 0; }
void waitSingle(int, u64) {}
u64 armGetSystemTick() { static u64 ticks{}; return ticks += 60'000'000'000ULL; }
u64 armTicksToNs(u64 ticks) { return ticks; }
void log_write(const char*, ...) {}
namespace ninmagic::tls {
bool ConfigureCurlTrust(CURL* curl) {
    return curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK;
}
}
namespace sphaira::curl { constexpr char USER_AGENT[] = "Sphaira/1.1.1 NinMagicShop/wire-test"; }
namespace sphaira::devoptab::common {
struct PushThreadData {
    PushThreadData(CURL* handle, std::stop_token) : curl{handle} {}
    CURL* curl;
    std::string body;
    std::size_t position{};
    CURLcode error{};
    long code{};
    Result CreateAndStart() {
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "127.0.0.1,localhost");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        error = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        return 0;
    }
    static size_t push_thread_callback(const char* data, size_t size, size_t count, void* opaque) {
        static_cast<PushThreadData*>(opaque)->body.append(data, size * count);
        return size * count;
    }
    size_t PullData(char* buffer, size_t length) {
        const auto count = std::min(length, body.size() - position);
        if (count) std::memcpy(buffer, body.data() + position, count);
        position += count;
        return count;
    }
};
}
// Actual Http/Base declarations, constructor, Start, Read and callbacks.
#include "production_yati_http.inc"

namespace response_buffer {
struct DataStruct {
    std::vector<unsigned char> data;
    size_t memory_limit{};
    s64 offset{};
};
constexpr size_t CHUNK_SIZE = 1024 * 1024;
bool g_running = true;
#ifdef Yield
#undef Yield
#endif
void Yield() {}
#include "production_memory_write.inc"

void Check(const std::string& origin) {
    DataStruct chunk{{}, 128, 0};
    char input[128]{};
    assert(WriteMemoryCallback(input, 1, 64, &chunk) == 64);
    assert(WriteMemoryCallback(input, 1, 65, &chunk) == 0);
    assert(WriteMemoryCallback(input, 1, 64, &chunk) == 64 && chunk.data.size() == 128);
    if (!(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_LIBZ)) return;
    const auto handle = curl_easy_init();
    assert(handle);
    ON_SCOPE_EXIT(curl_easy_cleanup(handle));
    chunk = {{}, 128, 0};
    const auto url = origin + "compressed-metadata";
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_NOPROXY, "127.0.0.1");
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "gzip");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &chunk);
    assert(curl_easy_perform(handle) == CURLE_WRITE_ERROR);
    assert(chunk.data.size() <= 128 && chunk.data.capacity() <= 128);
}
}

int main(int argc, char** argv) {
    if (argc != 2 || curl_global_init(CURL_GLOBAL_DEFAULT)) return 2;
    ON_SCOPE_EXIT(curl_global_cleanup());
    const std::string origin{argv[1]};
    response_buffer::Check(origin);
    using sphaira::yati::source::Http;
    Http package{origin + "wire/file", "wire-user", "wire-pass", {}, true};
    std::array<char, 3> data{};
    u64 count{};
    for (const auto& [endpoint, legacy] : {std::pair{"wire/modern", false}, {"wire/legacy", true}}) {
        Http negotiated{origin + endpoint, "wire-user", "wire-pass", {}, legacy};
        assert(negotiated.Read(data.data(), 0, data.size(), &count) == 0 && count == 3);
        assert(std::string_view(data.data(), count) == "012");
    }
    assert(package.Read(data.data(), 0, data.size(), &count) == 0 && count == 3);
    assert(std::string_view(data.data(), count) == "012");
    assert(package.Read(data.data(), 3, data.size(), &count) == 0 && count == 3);
    assert(std::string_view(data.data(), count) == "345");
    assert(package.Read(data.data(), 9, data.size(), &count) == 0 && count == 3);
    assert(std::string_view(data.data(), count) == "9ab");
    Http large{origin + "wire/large", "wire-user", "wire-pass", {}, true};
    assert(large.Read(data.data(), (s64{1} << 32) + 1, data.size(), &count) == 0 && count == 3);
    assert(std::string_view(data.data(), count) == "123");
    for (const auto& [path, expected] : {std::pair{"wire/dead-alias", Result_YatiHttpFileNotFound},
            {"wire/gone", Result_YatiHttpFileNotFound}, {"wire/denied", Result_YatiHttpAccessDenied},
            {"wire/wrong-range", Result_YatiHttpReadFailed}}) {
        Http rejected{origin + path, "wire-user", "wire-pass", {}, true};
        assert(rejected.Read(data.data(), 9, data.size(), &count) == expected);
        assert(count == 0);
    }
    Http unauthorized{origin + "wire/file", "wire-user", "incorrect-password", {}, true};
    assert(unauthorized.Read(data.data(), 0, data.size(), &count) == Result_YatiHttpAccessDenied && count == 0);
    std::printf("Yati Read/Start: ranges, 64-bit seek, HTTP 401/403/404/410 and bad range passed (libcurl %s)\n",
        curl_version_info(CURLVERSION_NOW)->version);
}
