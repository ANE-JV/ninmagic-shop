#pragma once

#include <curl/curl.h>
#include <initializer_list>

namespace sphaira::curl {

// curl_easy_reset() resets options but retains internal request/connection state.
// A handle that sent TE can keep its generated TE/Connection lines. Legacy
// Ownfoil treats those lines as an unknown client, so it needs a clean handle.
// ConfigureCurlTrust and the shared DNS/connections/TLS cache still apply below.
class RequestHandle final {
public:
    RequestHandle(CURL* pooled, bool legacy)
    : m_handle{legacy ? curl_easy_init() : pooled}, m_owned{legacy} {}
    ~RequestHandle() { if (m_owned && m_handle) curl_easy_cleanup(m_handle); }
    RequestHandle(const RequestHandle&) = delete;
    RequestHandle& operator=(const RequestHandle&) = delete;
    auto Get() const -> CURL* { return m_handle; }

private:
    CURL* m_handle{};
    bool m_owned{};
};

inline auto AddLegacyHeaders(curl_slist*& list) -> bool {
    for (const auto header : {"User-Agent:", "TE:", "Connection:"}) {
        const auto appended = curl_slist_append(list, header);
        if (!appended) return false;
        list = appended;
    }
    return true;
}

// In libcurl 7.69 the generated TE header belongs to connectdata, so a new
// easy handle can still inherit it through CURL_LOCK_DATA_CONNECT. Start a
// real TCP connection for legacy and do not put that connection back in the
// shared cache. DNS and TLS-session reuse remain available and TLS is verified.
inline auto ConfigureLegacyConnection(CURL* handle) -> bool {
    return curl_easy_setopt(handle, CURLOPT_FRESH_CONNECT, 1L) == CURLE_OK
        && curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 1L) == CURLE_OK;
}

} // namespace sphaira::curl
