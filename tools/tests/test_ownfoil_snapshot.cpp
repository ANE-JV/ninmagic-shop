// A real reconnect discards every in-memory artwork object. The disk format,
// network state machine, profile scope and validation below are production
// code; this transport makes body downloads observable without signing in.
#include "utils/ownfoil_profile.hpp"
#include "utils/ownfoil_snapshot.hpp"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
using namespace sphaira::ownfoil;
using u8 = unsigned char;
using u32 = unsigned;
using s64 = std::int64_t;
namespace curl {
struct Header {
    std::unordered_map<std::string,std::string> m_map;
    auto Find(const char* key) const { return m_map.find(key); }
};
enum { Flag_KeepErrorBody=1, Flag_NoRedirect=2, Flag_ConnectionDiagnostic=4, Flag_NoBody=8 };
struct ApiResult { bool success{true}; long code{200}; Header header; std::vector<u8> data; };
struct Url { std::string value; };
struct Fields { std::string value; };
struct UserPass { std::string user, pass; };
struct PreemptiveAuth { bool on; };
struct Flags { u32 value; };
struct MemoryLimit { std::size_t bytes; };
using OnProgress = std::function<bool(s64,s64,s64,s64)>;
using StopToken = std::stop_token;
struct Server {
    bool denied{}, private_metadata{true}, html{}, fail_head{};
    std::string etag{"version-1"};
    std::string body = R"({"games":[{"app_id":"0100000000000000","name":"Demo","app_type":"BASE","iconUrl":"https://images.test/cover.jpg","owned":true}]})";
    unsigned bodies{}, heads{}, logins{};
} server;
struct Api {
    template<class... Options> static ApiResult ToMemory(Url, Fields, Header, Flags, Options...) {
        ++server.logins;
        ApiResult result;
        result.code = 302;
        result.header.m_map.emplace("Set-Cookie", server.denied ? "session=denied" : "session=secret-marker");
        return result;
    }
    template<class... Options> static ApiResult ToMemory(Url, Header headers, UserPass, PreemptiveAuth, Flags flags, Options...) {
        ApiResult result;
        const bool head = flags.value & Flag_NoBody;
        if (head) ++server.heads;
        if (server.denied) { result.code = 403; return result; }
        if (server.private_metadata && headers.m_map["Cookie"] != "session=secret-marker") { result.code = 302; return result; }
        if (head && server.fail_head) { result.code = 405; return result; }
        result.header.m_map["Content-Type"] = server.html ? "text/html" : "application/json";
        result.header.m_map["ETag"] = server.etag;
        if (!head) {
            ++server.bodies;
            const auto content = server.html ? "<html>login</html>" : server.body;
            result.data.assign(content.begin(), content.end());
        }
        return result;
    }
};
}
namespace fs {
struct FsNativeSd {
    int CreateDirectoryRecursively(const std::string& path) {
        std::filesystem::create_directories(path); return 0;
    }
};
}
#define R_SUCCEEDED(value) ((value) == 0)
struct ConnectionDiagnostic {
    void Event(std::string) const {}
    void Response(const char*,const std::string&,const curl::ApiResult&) const {}
};
struct ArtworkSnapshot { std::vector<index::Artwork> records; index::ArtworkLookup by_identity; };
struct ArtworkCache { std::string root,metadata_url,user,pass; std::shared_ptr<const ArtworkSnapshot> records; } g_artwork_cache;
std::mutex g_artwork_mutex;
std::string CatalogDirectory;
std::atomic<std::uint64_t> g_catalog_generation{}, g_catalog_stage{};
snapshot::Digest Digest(std::string_view bytes) {
    // Console build injects libnx SHA-256. Only deterministic equality of
    // distinct input samples is needed for this host fixture.
    snapshot::Digest value{};
    std::uint64_t h = 14695981039346656037ULL;
    for (auto c : bytes) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
    std::memcpy(value.data(), &h, sizeof(h));
    return value;
}
bool IsSafeAddress(const std::string& url) { return url.starts_with("https://"); }
bool IsSameOrigin(const std::string& root,const std::string& url) {
    return url.starts_with(root);
}
std::string ResolveUrl(const std::string& root,const std::string& path) {
    if (path.empty() || path.find("://") != std::string::npos) return path;
    return root + path;
}
#include "production_snapshot.inc"

int main(int argc,char** argv) {
    assert(!snapshot::SafeUrl("//user:password@images.test/cover.png"));
    assert(!snapshot::SafeUrl("https://images.test/cover.png?signature=secret"));
    CatalogDirectory = (std::filesystem::temp_directory_path() /
        ("ninmagic-snapshot-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    Config profile;
    profile.uid = "profile-A"; profile.user = "alice-marker"; profile.pass = "secret-password-marker";
    const std::string root = "https://catalog.test/";
    std::string listing = "authenticated-root-v1";
    std::shared_ptr<const ArtworkSnapshot> view;
    const auto open = [&](const Config& config) {
        g_artwork_cache = {}; view.reset(); // Simulate a new process on each call.
        return FetchWebArtwork(root, config, {}, true, listing, view);
    };
    assert(open(profile) && view->records.size() == 1 && curl::server.bodies == 1);
    const auto disk = CatalogSnapshotPath(root,profile);
    assert(std::filesystem::exists(disk));
    {
        std::ifstream file(disk,std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(file),{}};
        for (const auto& private_value : {profile.user,profile.pass,std::string("session=secret-marker")})
            assert(bytes.find(private_value) == std::string::npos);
    }
    assert(open(profile) && view->records[0].name == "Demo" && curl::server.bodies == 1);
    assert(curl::server.heads == 1 && curl::server.logins == 2);
    curl::server.denied = true; assert(!open(profile) && !view); curl::server.denied = false;
    curl::server.html = true; assert(!open(profile) && !view); curl::server.html = false;
    auto before = curl::server.bodies;
    auto other = profile; other.uid = "profile-B";
    assert(CatalogSnapshotPath(root,other) != disk && open(other) && curl::server.bodies == before + 1);
    other = profile; other.pass = "replacement-password";
    assert(CatalogSnapshotPath(root,other) != disk && open(other) && curl::server.bodies == before + 2);
    listing += "-changed"; assert(open(profile) && curl::server.bodies == before + 3);
    curl::server.etag = "version-2"; assert(open(profile) && curl::server.bodies == before + 4);
    std::filesystem::remove(disk); assert(open(profile) && curl::server.bodies == before + 5);
    {
        std::fstream broken(disk,std::ios::binary|std::ios::in|std::ios::out);
        broken.seekp(-1,std::ios::end); broken.put('X');
    }
    assert(open(profile) && curl::server.bodies == before + 6);
    snapshot::Header header{};
    assert(!snapshot::Inspect(disk,Digest(listing),std::time(nullptr)+snapshot::Lifetime+2,header));
    curl::server.fail_head = true; assert(open(profile) && curl::server.bodies == before + 7); curl::server.fail_head = false;
    std::filesystem::remove(disk);
    auto with_token = curl::server.body;
    with_token.insert(with_token.find("cover.jpg")+9,"?signed=private");
    curl::server.body = with_token;
    assert(open(profile) && std::filesystem::exists(disk) && view->records.front().icon.find("?signed") != std::string::npos);
    assert(open(profile) && view->records.front().icon.empty());
    {
        std::ifstream file(disk,std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(file),{}};
        assert(bytes.find("signed=private") == std::string::npos);
    }
    std::filesystem::remove(disk);
    if (argc == 2) {
        std::ifstream input(argv[1],std::ios::binary);
        curl::server.body.assign(std::istreambuf_iterator<char>(input),{});
        const auto started = std::chrono::steady_clock::now();
        assert(open(profile));
        const auto count = view->records.size();
        const auto downloads = curl::server.bodies;
        const auto saved_at = std::chrono::steady_clock::now();
        assert(open(profile) && view->records.size() == count && curl::server.bodies == downloads);
        const auto elapsed_ms = [](auto start, auto end) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
        };
        std::cout << "Private metadata snapshot: records=" << count << " snapshot_bytes=" << std::filesystem::file_size(disk)
            << " metadata_body_bytes_skipped=" << curl::server.body.size() << " host_cold_ms=" << elapsed_ms(started,saved_at)
            << " host_warm_ms=" << elapsed_ms(saved_at,std::chrono::steady_clock::now()) << "\n";
    }
    std::filesystem::remove_all(CatalogDirectory);
    std::cout << "Second launch reused validated metadata, account/refresh/corruption/login cases passed\n";
}

