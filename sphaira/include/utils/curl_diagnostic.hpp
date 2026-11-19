#pragma once
#include <curl/curl.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace sphaira::curl::diagnostic {
struct Trace {
    bool enabled{}, debug_enabled{}, legacy{}, user_present{}, password_present{};
    std::string curl_version, requested_origin, effective_origin;
    std::string requested_endpoint_kind, effective_endpoint_kind, peer_ip;
    long transport_code{}, http_version{}, redirect_count{}, new_connections{};
    std::vector<std::vector<std::string>> request_header_names;
    std::vector<std::string> request_methods, response_header_names;
    std::vector<long> response_codes;
    unsigned unknown_request_headers{}, unknown_response_headers{};
};
struct SafeUrl { std::string origin, endpoint_kind{"other"}; };
inline auto Lower(std::string_view text) -> std::string {
    std::string out; out.reserve(text.size());
    for (const auto c : text) out += c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a'-'A')) : c;
    return out;
}
inline auto SanitizeUrl(const char* input) -> SafeUrl {
    SafeUrl out;
    if (!input) return out;
    auto parsed=curl_url(); if (!parsed) return out;
    if (curl_url_set(parsed,CURLUPART_URL,input,CURLU_DEFAULT_SCHEME)==CURLUE_OK) {
        char *scheme{},*host{},*port{},*path{};
        curl_url_get(parsed,CURLUPART_SCHEME,&scheme,0);
        curl_url_get(parsed,CURLUPART_HOST,&host,0);
        curl_url_get(parsed,CURLUPART_PORT,&port,CURLU_DEFAULT_PORT);
        curl_url_get(parsed,CURLUPART_PATH,&path,0);
        const auto protocol=scheme?Lower(scheme):std::string{};
        if ((protocol=="http"||protocol=="https")&&host&&port) out.origin=protocol+"://"+host+":"+port;
        auto route=path?Lower(path):std::string{};
        while(route.size()>1&&route.back()=='/') route.pop_back();
        if(route.empty()||route=="/") out.endpoint_kind="root";
        else if(route.ends_with("/login")||route.ends_with("/signin")) out.endpoint_kind="login";
        else if(route.ends_with("/api/graphql")) out.endpoint_kind="graphql";
        else if(route.ends_with("/api/titles")) out.endpoint_kind="titles";
        else if(route.ends_with("/juegos")) out.endpoint_kind="web_catalog";
        curl_free(scheme);curl_free(host);curl_free(port);curl_free(path);
    }
    curl_url_cleanup(parsed); return out;
}
inline auto SafeHeader(std::string_view raw) -> std::string {
    if(raw.empty()||raw.size()>64) return {};
    const auto name=Lower(raw);
    // Unknown names are counted, never persisted: a hostile header name can
    // itself contain private data. Header values are never copied here.
    constexpr std::string_view known[]={"host","accept","accept-encoding","authorization","cookie","connection","te",
        "user-agent","proxy-connection","proxy-authorization","content-length","content-type","content-encoding",
        "transfer-encoding","range","if-none-match","if-modified-since","expect","origin","referer","cache-control",
        "pragma","upgrade","server","date","location","set-cookie","etag","last-modified","vary","allow",
        "www-authenticate","strict-transport-security","x-frame-options","x-content-type-options","content-security-policy"};
    return std::find(std::begin(known),std::end(known),name)!=std::end(known)?name:std::string{};
}
inline void AddName(std::vector<std::string>& names,const std::string& name) {
    if(names.size()<40&&std::find(names.begin(),names.end(),name)==names.end()) names.push_back(name);
}
inline int Debug(CURL*,curl_infotype type,char* data,size_t size,void* opaque) {
    if(type!=CURLINFO_HEADER_OUT&&type!=CURLINFO_HEADER_IN) return 0;
    auto& trace=*static_cast<Trace*>(opaque);
    const bool outgoing=type==CURLINFO_HEADER_OUT;
    if(outgoing&&trace.request_header_names.size()<16) trace.request_header_names.emplace_back();
    const std::string_view block{data,size};
    for(size_t start{};start<block.size();) {
        auto end=block.find('\n',start);if(end==block.npos)end=block.size();
        const auto line=block.substr(start,end-start);start=end+1;
        if(!outgoing&&line.starts_with("HTTP/")) {
            const auto p=line.find(' ');
            if(p!=line.npos&&p+3<line.size()&&line[p+1]>='0'&&line[p+1]<='9'&&line[p+2]>='0'&&line[p+2]<='9'&&line[p+3]>='0'&&line[p+3]<='9'&&trace.response_codes.size()<24)
                trace.response_codes.push_back((line[p+1]-'0')*100+(line[p+2]-'0')*10+line[p+3]-'0');
            continue;
        }
        if(outgoing) {
            const auto space=line.find(' ');
            const auto method=line.substr(0,space);
            if((method=="GET"||method=="HEAD"||method=="POST"||method=="OPTIONS"||method=="PUT"||method=="DELETE")&&trace.request_methods.size()<16) {
                trace.request_methods.emplace_back(method);continue;
            }
        }
        const auto colon=line.find(':');if(colon==line.npos||!colon)continue;
        const auto name=SafeHeader(line.substr(0,colon));
        if(name.empty()) {if(outgoing)++trace.unknown_request_headers;else ++trace.unknown_response_headers;continue;}
        if(outgoing&&!trace.request_header_names.empty()) AddName(trace.request_header_names.back(),name);
        else if(!outgoing) AddName(trace.response_header_names,name);
    }
    return 0;
}
class Scope final {
public:
    Scope(CURL* handle,bool enabled,bool legacy,const std::string& url,bool user,bool password):m_handle{handle} {
        if(!enabled||!handle)return;
        m_trace.enabled=true;m_trace.legacy=legacy;m_trace.user_present=user;m_trace.password_present=password;
        m_trace.curl_version=::curl_version();
        const auto safe=SanitizeUrl(url.c_str());m_trace.requested_origin=safe.origin;m_trace.requested_endpoint_kind=safe.endpoint_kind;
        // Never enable verbose unless the consuming callback and its data were
        // installed successfully. No fallback fprintf of Basic Auth is allowed.
        if(curl_easy_setopt(handle,CURLOPT_DEBUGFUNCTION,Debug)==CURLE_OK&&curl_easy_setopt(handle,CURLOPT_DEBUGDATA,&m_trace)==CURLE_OK)
            m_trace.debug_enabled=curl_easy_setopt(handle,CURLOPT_VERBOSE,1L)==CURLE_OK;
        if(!m_trace.debug_enabled)curl_easy_setopt(handle,CURLOPT_VERBOSE,0L);
    }
    ~Scope() {
        if(m_trace.enabled) {
            curl_easy_setopt(m_handle,CURLOPT_VERBOSE,0L);
            curl_easy_setopt(m_handle,CURLOPT_DEBUGFUNCTION,nullptr);
            curl_easy_setopt(m_handle,CURLOPT_DEBUGDATA,nullptr);
        }
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    auto Finish(CURLcode code) -> Trace {
        if(!m_trace.enabled)return {};
        m_trace.transport_code=code;
        curl_easy_getinfo(m_handle,CURLINFO_HTTP_VERSION,&m_trace.http_version);
        curl_easy_getinfo(m_handle,CURLINFO_REDIRECT_COUNT,&m_trace.redirect_count);
        curl_easy_getinfo(m_handle,CURLINFO_NUM_CONNECTS,&m_trace.new_connections);
        char *url{},*ip{};curl_easy_getinfo(m_handle,CURLINFO_EFFECTIVE_URL,&url);curl_easy_getinfo(m_handle,CURLINFO_PRIMARY_IP,&ip);
        const auto safe=SanitizeUrl(url);m_trace.effective_origin=safe.origin;m_trace.effective_endpoint_kind=safe.endpoint_kind;
        if(ip) {
            const std::string_view text{ip};
            if(text.size()<=64&&text.find_first_not_of("0123456789abcdefABCDEF:.[]%")==text.npos)m_trace.peer_ip=text;
        }
        return m_trace;
    }
private:
    CURL* m_handle{};Trace m_trace{};
};
} // namespace sphaira::curl::diagnostic
