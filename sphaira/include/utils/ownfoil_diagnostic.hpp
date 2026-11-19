// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// Safe summaries only: no bodies, URLs, account fields or header values.
namespace sphaira::ownfoil::diagnostic {
inline auto Lower(std::string_view value) -> std::string {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
inline auto Endpoint(std::string_view url) -> const char* {
    const auto scheme = url.find("://");
    const auto start = scheme == std::string_view::npos ? 0 : scheme + 3;
    const auto slash = url.find('/', start);
    if (slash == std::string_view::npos) return "root";
    const auto path = Lower(url.substr(slash, url.find_first_of("?#", slash) - slash));
    if (path.empty() || path == "/") return "root";
    if (path == "/login" || path.ends_with("/login") || path.ends_with("/login/")) return "login";
    if (path == "/api" || path.find("/api/") != std::string::npos) return "api";
    return "other";
}
inline auto Scheme(std::string_view url) -> const char* {
    const auto end = url.find("://");
    if (end == std::string_view::npos) return "unspecified";
    const auto scheme = Lower(url.substr(0, end));
    return scheme == "https" ? "https" : scheme == "http" ? "http" : "other";
}
inline auto EndpointFromTransport(std::string_view kind) -> const char* {
    if (kind == "root" || kind == "login") return kind == "root" ? "root" : "login";
    return kind == "api" || kind == "graphql" || kind == "titles" ? "api" : "other";
}
struct BodyShape {
    std::size_t bytes{}, inspected{}, forms{}, anchors{}, tables{};
    const char* kind{"empty"};
};
inline auto CountTag(const std::string& lower, std::string_view tag) -> std::size_t {
    std::size_t count{}, position{};
    while ((position = lower.find(tag, position)) != std::string::npos) {
        const auto after = position + tag.size();
        if (after < lower.size() && (std::isspace(static_cast<unsigned char>(lower[after])) || lower[after] == '>' || lower[after] == '/')) ++count;
        position = after;
    }
    return count;
}
inline auto DescribeBody(std::string_view body) -> BodyShape {
    BodyShape shape;
    shape.bytes = body.size();
    shape.inspected = std::min<std::size_t>(body.size(), 65536);
    if (body.empty()) return shape;
    if (body.size() >= 2 && static_cast<unsigned char>(body[0]) == 0x1f && static_cast<unsigned char>(body[1]) == 0x8b) { shape.kind = "gzip"; return shape; }
    const auto lower = Lower(body.substr(0, shape.inspected));
    shape.forms = CountTag(lower, "<form");
    shape.anchors = CountTag(lower, "<a");
    shape.tables = CountTag(lower, "<table");
    if (shape.forms && lower.find("password") != std::string::npos) shape.kind = "html_login";
    else if (shape.forms) shape.kind = "html_form";
    else if (lower.find("00 - error") != std::string::npos) shape.kind = "html_error_list";
    else if (shape.tables || shape.anchors) shape.kind = "html_directory";
    else if (lower.find("<html") != std::string::npos || lower.find("<!doctype html") != std::string::npos) shape.kind = "html_other";
    else {
        const auto begin = lower.find_first_not_of(" \r\n\t");
        shape.kind = begin != std::string::npos && (lower[begin] == '{' || lower[begin] == '[') ? "json" : "other";
    }
    return shape;
}
struct HeaderNames { std::string known; std::size_t unknown{}; };
inline auto DescribeHeaders(const std::vector<std::string>& names) -> HeaderNames {
    constexpr const char* allowed[] = {
        "host", "accept", "accept-encoding", "authorization", "user-agent", "te", "connection", "cookie", "range", "referer", "expect", "pragma",
        "content-type", "content-length", "content-encoding", "transfer-encoding", "location", "set-cookie", "www-authenticate", "proxy-connection",
        "cache-control", "if-none-match", "if-modified-since", "etag", "last-modified", "date", "server", "vary", "allow", "expires",
        "strict-transport-security", "x-frame-options", "x-content-type-options", "x-xss-protection", "content-security-policy", "cf-ray", "cf-cache-status"
    };
    HeaderNames out;
    std::vector<std::string> known;
    for (const auto& input : names) {
        const auto name = Lower(input);
        const auto found = std::find_if(std::begin(allowed), std::end(allowed), [&name](const auto* candidate) { return name == candidate; });
        if (found == std::end(allowed)) ++out.unknown;
        else if (std::find(known.begin(), known.end(), name) == known.end()) known.push_back(name);
    }
    std::sort(known.begin(), known.end());
    for (const auto& name : known) { if (!out.known.empty()) out.known += ','; out.known += name; }
    if (out.known.empty()) out.known = "none";
    return out;
}
inline auto FormatBody(const BodyShape& shape) -> std::string {
    return "body=" + std::string(shape.kind) + " bytes=" + std::to_string(shape.bytes) + " inspected=" + std::to_string(shape.inspected)
        + " forms=" + std::to_string(shape.forms) + " anchors=" + std::to_string(shape.anchors) + " tables=" + std::to_string(shape.tables);
}
} // namespace sphaira::ownfoil::diagnostic
