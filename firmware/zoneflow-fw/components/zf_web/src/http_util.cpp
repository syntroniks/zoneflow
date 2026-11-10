// SPDX-License-Identifier: Apache-2.0
#include "zf_web/http_util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace zf::web {
namespace {

bool iequals(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace

std::string_view hostWithoutPort(std::string_view host)
{
    host = trim(host);
    if (!host.empty() && host.front() == '[') {
        const std::size_t close = host.find(']');
        return close == std::string_view::npos ? host : host.substr(0, close + 1);
    }
    const std::size_t colon = host.rfind(':');
    if (colon != std::string_view::npos && host.find(':') == colon) {
        return host.substr(0, colon);
    }
    return host;
}

bool isIpLiteral(std::string_view host)
{
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        return true;
    }
    int dots = 0;
    int digits = 0;
    for (char c : host) {
        if (c == '.') {
            if (digits == 0) {
                return false;
            }
            ++dots;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 3) {
                return false;
            }
        } else {
            return false;
        }
    }
    return dots == 3 && digits > 0;
}

bool hostAllowed(std::string_view hostHeader, std::string_view deviceHostname)
{
    const std::string_view h = hostWithoutPort(hostHeader);
    if (h.empty()) {
        return true;
    }
    if (isIpLiteral(h)) {
        return true;
    }
    std::string_view name = h;
    if (!name.empty() && name.back() == '.') {
        name.remove_suffix(1);
    }
    if (iequals(name, deviceHostname)) {
        return true;
    }
    constexpr std::string_view kLocal = ".local";
    return name.size() == deviceHostname.size() + kLocal.size() &&
           iequals(name.substr(0, deviceHostname.size()), deviceHostname) &&
           iequals(name.substr(deviceHostname.size()), kLocal);
}

bool originMatchesHost(std::string_view origin, std::string_view hostHeader)
{
    origin = trim(origin);
    if (origin.empty()) {
        return true;
    }
    const std::size_t scheme = origin.find("://");
    if (scheme == std::string_view::npos) {
        return false;
    }
    std::string_view rest = origin.substr(scheme + 3);
    if (const std::size_t slash = rest.find('/'); slash != std::string_view::npos) {
        rest = rest.substr(0, slash);
    }
    // Compare host and port as sent. Browsers omit default ports in both.
    return iequals(rest, trim(hostHeader));
}

std::string_view cookieValue(std::string_view header, std::string_view name)
{
    while (!header.empty()) {
        const std::size_t semi = header.find(';');
        std::string_view part = trim(header.substr(0, semi));
        const std::size_t eq = part.find('=');
        if (eq != std::string_view::npos && trim(part.substr(0, eq)) == name) {
            std::string_view v = trim(part.substr(eq + 1));
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                v = v.substr(1, v.size() - 2);
            }
            return v;
        }
        if (semi == std::string_view::npos) {
            break;
        }
        header.remove_prefix(semi + 1);
    }
    return {};
}

std::string_view bearerToken(std::string_view authorization)
{
    authorization = trim(authorization);
    constexpr std::string_view kBearer = "Bearer ";
    if (authorization.size() <= kBearer.size() || !iequals(authorization.substr(0, kBearer.size()), kBearer)) {
        return {};
    }
    return trim(authorization.substr(kBearer.size()));
}

bool mediaTypeIs(std::string_view contentType, std::string_view type)
{
    const std::size_t semi = contentType.find(';');
    return iequals(trim(contentType.substr(0, semi)), type);
}

bool urlDecode(std::string_view in, std::string &out)
{
    out.clear();
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%') {
            if (i + 2 >= in.size() || !std::isxdigit(static_cast<unsigned char>(in[i + 1])) ||
                !std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
                return false;
            }
            const auto hex = [](char h) {
                return h <= '9' ? h - '0' : (std::tolower(static_cast<unsigned char>(h)) - 'a' + 10);
            };
            const char decoded = static_cast<char>((hex(in[i + 1]) << 4) | hex(in[i + 2]));
            if (decoded == '\0') {
                return false;
            }
            out.push_back(decoded);
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return true;
}

bool queryParam(std::string_view query, std::string_view key, std::string &value)
{
    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view part = query.substr(0, amp);
        const std::size_t eq = part.find('=');
        if (part.substr(0, eq) == key) {
            if (eq == std::string_view::npos) {
                value.clear();
                return true;
            }
            return urlDecode(part.substr(eq + 1), value);
        }
        if (amp == std::string_view::npos) {
            break;
        }
        query.remove_prefix(amp + 1);
    }
    return false;
}

bool queryFlag(std::string_view query, std::string_view key)
{
    std::string v;
    if (!queryParam(query, key, v)) {
        return false;
    }
    return v.empty() || v == "1" || v == "true" || v == "yes";
}

// ---- static files -----------------------------------------------------------------

bool acceptsGzip(std::string_view acceptEncoding)
{
    while (!acceptEncoding.empty()) {
        const std::size_t comma = acceptEncoding.find(',');
        std::string_view part = trim(acceptEncoding.substr(0, comma));
        const std::size_t semi = part.find(';');
        const std::string_view coding = trim(part.substr(0, semi));
        if (iequals(coding, "gzip")) {
            // "gzip;q=0" refuses it.
            const std::string_view params = semi == std::string_view::npos ? std::string_view{} : part.substr(semi + 1);
            return trim(params) != "q=0" && trim(params) != "q=0.0";
        }
        if (comma == std::string_view::npos) {
            break;
        }
        acceptEncoding.remove_prefix(comma + 1);
    }
    return false;
}

const char *mimeType(std::string_view path)
{
    static constexpr std::array<std::pair<std::string_view, const char *>, 16> kTypes = {{
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".mjs", "text/javascript; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".json", "application/json"},
        {".webmanifest", "application/manifest+json"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".ico", "image/x-icon"},
        {".webp", "image/webp"},
        {".woff2", "font/woff2"},
        {".woff", "font/woff"},
        {".txt", "text/plain; charset=utf-8"},
        {".map", "application/json"},
    }};
    for (const auto &[ext, type] : kTypes) {
        if (endsWith(path, ext)) {
            return type;
        }
    }
    return "application/octet-stream";
}

StaticTarget resolveStatic(std::string_view uriPath, bool clientAcceptsGzip,
                           const std::function<bool(const std::string &)> &exists)
{
    StaticTarget t;
    if (const std::size_t q = uriPath.find_first_of("?#"); q != std::string_view::npos) {
        uriPath = uriPath.substr(0, q);
    }
    std::string path;
    if (!urlDecode(uriPath, path) || path.empty() || path.front() != '/' || path.find("..") != std::string::npos ||
        path.find('\\') != std::string::npos || path.find("//") != std::string::npos) {
        t.kind = StaticTarget::Kind::BadPath;
        return t;
    }
    if (path.back() == '/') {
        path += "index.html";
    }
    auto tryFile = [&](const std::string &p) {
        if (clientAcceptsGzip && exists(p + ".gz")) {
            t.path = p + ".gz";
            t.gzip = true;
        } else if (exists(p)) {
            t.path = p;
            t.gzip = false;
        } else {
            return false;
        }
        t.kind = StaticTarget::Kind::File;
        t.contentType = mimeType(p);
        t.cacheControl = p.rfind("/assets/", 0) == 0 ? "public, max-age=31536000, immutable" : "no-cache";
        return true;
    };
    if (tryFile(path)) {
        return t;
    }
    const std::size_t lastSlash = path.rfind('/');
    const bool hasExtension = path.find('.', lastSlash) != std::string::npos;
    if (!hasExtension && tryFile("/index.html")) {
        return t;
    }
    t.kind = StaticTarget::Kind::NotFound;
    return t;
}

} // namespace zf::web
