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

} // namespace sphaira::curl
