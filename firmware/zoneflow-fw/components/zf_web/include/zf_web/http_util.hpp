// SPDX-License-Identifier: Apache-2.0
// HTTP helpers with no server dependency: header parsing, the Host and Origin
// checks, query strings and static-file resolution. Host-tested.
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace zf::web {

// "host:port" -> "host"; "[v6]:port" -> "[v6]".
std::string_view hostWithoutPort(std::string_view host);
bool isIpLiteral(std::string_view host);

// DNS-rebinding guard: the Host header must be an IP literal, the device
// hostname or "<hostname>.local" (case-insensitive). An absent Host header
// (HTTP/1.0) is accepted.
bool hostAllowed(std::string_view hostHeader, std::string_view deviceHostname);

// True when an Origin header is absent or names the same host and port as
// the Host header. "null" never matches.
bool originMatchesHost(std::string_view origin, std::string_view hostHeader);

// Value of cookie `name` in a Cookie header, empty if absent.
std::string_view cookieValue(std::string_view cookieHeader, std::string_view name);
// Token of "Authorization: Bearer <token>", empty otherwise.
std::string_view bearerToken(std::string_view authorization);
// Media type without parameters, lowercase compare: "application/json; charset=utf-8" -> true.
bool mediaTypeIs(std::string_view contentType, std::string_view type);

// URL-decoded value of `key` in a query string; nullopt-like empty + found flag.
bool queryParam(std::string_view query, std::string_view key, std::string &value);
// "1", "true", "yes" or a bare key count as true.
bool queryFlag(std::string_view query, std::string_view key);
// Decodes %XX and '+'. Returns false on a malformed escape or a NUL byte.
bool urlDecode(std::string_view in, std::string &out);

// ---- static files ---------------------------------------------------------------

struct StaticTarget {
    enum class Kind { File, NotFound, BadPath } kind = Kind::NotFound;
    std::string path;  // file to open, relative to the mount point ("/index.html", "/app.js.gz")
    bool gzip = false; // send Content-Encoding: gzip
    const char *contentType = "application/octet-stream";
    const char *cacheControl = "no-cache";
};

// Resolves a request path against the files in the assets partition.
//  - "/" serves /index.html.
//  - A precompressed "<file>.gz" is preferred when the client accepts gzip.
//  - A path with no extension that matches no file serves /index.html
//    (client-side routing).
//  - Files under /assets/ are cached for a year ("immutable"): the UI build
//    must put a content hash in those names. Everything else: no-cache.
StaticTarget resolveStatic(std::string_view uriPath, bool clientAcceptsGzip,
                           const std::function<bool(const std::string &)> &exists);

bool acceptsGzip(std::string_view acceptEncoding);
const char *mimeType(std::string_view path);

} // namespace zf::web
