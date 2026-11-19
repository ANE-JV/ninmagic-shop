#include "utils/ownfoil_diagnostic.hpp"
#include <cassert>
#include <iostream>
#include <string>

int main() {
    namespace diag = sphaira::ownfoil::diagnostic;
    const std::string marker_user = "fixture-private-account";
    const std::string marker_secret = "fixture-private-value";
    const auto url = "https://" + marker_user + ':' + marker_secret + "@example.invalid/login?secret=" + marker_secret;
    assert(std::string(diag::Endpoint(url)) == "login");
    assert(std::string(diag::Scheme(url)) == "https");
    assert(std::string(diag::Endpoint("https://example.invalid/?q=fixture")) == "root");
    assert(std::string(diag::Endpoint("https://example.invalid/proxy/api/graphql?query=fixture")) == "api");
    assert(std::string(diag::Endpoint("https://example.invalid/another/path")) == "other");
    const auto login = "<html><form><input type=password value='" + marker_secret + "'></form></html>";
    auto shape = diag::DescribeBody(login);
    assert(shape.forms == 1 && shape.anchors == 0 && std::string(shape.kind) == "html_login");
    const auto report = diag::FormatBody(shape);
    assert(report.find(marker_user) == std::string::npos && report.find(marker_secret) == std::string::npos);
    shape = diag::DescribeBody("<html><table class='listing'><A\nHREF='package.nsp'>file</A></table></html>");
    assert(shape.tables == 1 && shape.anchors == 1 && std::string(shape.kind) == "html_directory");
    shape = diag::DescribeBody("<formidable><article><aside></aside></article></formidable>");
    assert(shape.forms == 0 && shape.anchors == 0);
    shape = diag::DescribeBody(std::string(70000, 'x'));
    assert(shape.bytes == 70000 && shape.inspected == 65536);
    shape = diag::DescribeBody("{\"files\":[]}");
    assert(std::string(shape.kind) == "json");
    const auto names = diag::DescribeHeaders({"Authorization", "Cookie", "Set-Cookie", "Accept", "TE", "Connection", "X-" + marker_user, "Authorization: " + marker_secret});
    assert(names.known == "accept,authorization,connection,cookie,set-cookie,te" && names.unknown == 2);
    assert(names.known.find(marker_user) == std::string::npos && names.known.find(marker_secret) == std::string::npos);
    std::cout << "Ownfoil diagnostic shape and privacy fixtures passed\n";
}
