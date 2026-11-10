// SPDX-License-Identifier: Apache-2.0
// Route matching, header helpers and static-file resolution.
#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>
#include <string>

#include "cJSON.h"
#include "zf_web/http_util.hpp"
#include "zf_web/json_map.hpp"
#include "zf_web/routes.hpp"

using namespace zf::web;

TEST(Routes, MatchWithParams)
{
    RouteMatch m = matchRoute("POST", "/api/v1/zones/3/run");
    ASSERT_NE(m.route, nullptr);
    EXPECT_EQ(m.route->id, RouteId::ZoneRun);
    ASSERT_EQ(m.paramCount, 1u);
    EXPECT_EQ(m.params[0], "3");

    m = matchRoute("GET", "/api/v1/programs/12/");
    ASSERT_NE(m.route, nullptr);
    EXPECT_EQ(m.route->id, RouteId::ProgramGet);
    EXPECT_EQ(m.params[0], "12");

    m = matchRoute("GET", "/api/v1/zones//run");
    EXPECT_EQ(m.route, nullptr);
    EXPECT_FALSE(m.pathKnown);
    m = matchRoute("GET", "/api/v2/status");
    EXPECT_EQ(m.route, nullptr);
    m = matchRoute("GET", "/api/v1/status/extra");
    EXPECT_EQ(m.route, nullptr);
    m = matchRoute("PUT", "/api/v1/status");
    EXPECT_EQ(m.route, nullptr);
    EXPECT_TRUE(m.pathKnown);
}

TEST(Routes, TableIsConsistent)
{
    std::set<std::string> seen;
    std::set<int> ids;
    for (const Route &r : routes()) {
        EXPECT_TRUE(seen.insert(std::string(r.method) + " " + std::string(r.pattern)).second) << r.pattern;
        EXPECT_TRUE(ids.insert(static_cast<int>(r.id)).second) << r.pattern;
        // Every write is protected except login/setup and provisioning.
        if (r.method != "GET" && r.access == Access::Public) {
            EXPECT_TRUE(r.id == RouteId::AuthLogin || r.id == RouteId::AuthSetup) << r.pattern;
        }
        if (r.method == "GET") {
            EXPECT_NE(r.access, Access::Admin) << r.pattern;
            EXPECT_EQ(r.maxBody, 0u) << r.pattern;
        }
        if (r.body == Body::Json) {
            EXPECT_GT(r.maxBody, 0u) << r.pattern;
        }
    }
}

TEST(HttpUtil, HostAllowed)
{
    EXPECT_TRUE(hostAllowed("", "zoneflow-ab12"));
    EXPECT_TRUE(hostAllowed("192.168.4.1", "zoneflow-ab12"));
    EXPECT_TRUE(hostAllowed("10.0.0.7:8080", "zoneflow-ab12"));
    EXPECT_TRUE(hostAllowed("[fe80::1]:80", "zoneflow-ab12"));
    EXPECT_TRUE(hostAllowed("zoneflow-ab12", "zoneflow-ab12"));
    EXPECT_TRUE(hostAllowed("ZONEFLOW-AB12.LOCAL.", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("zoneflow-ab12.evil.com", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("evil.com", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("1.2.3.4.evil.com", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("999.1.1", "zoneflow-ab12"));
}

TEST(HttpUtil, OriginMatchesHost)
{
    EXPECT_TRUE(originMatchesHost("", "x"));
    EXPECT_TRUE(originMatchesHost("http://192.168.1.30", "192.168.1.30"));
    EXPECT_TRUE(originMatchesHost("https://zoneflow-ab12.local:8443", "zoneflow-ab12.local:8443"));
    EXPECT_FALSE(originMatchesHost("http://192.168.1.30:81", "192.168.1.30"));
    EXPECT_FALSE(originMatchesHost("null", "192.168.1.30"));
    EXPECT_FALSE(originMatchesHost("http://evil.example", "192.168.1.30"));
}

TEST(HttpUtil, CookiesAndBearer)
{
    EXPECT_EQ(cookieValue("a=1; zf_session=abc; b=2", "zf_session"), "abc");
    EXPECT_EQ(cookieValue("zf_session=\"q\"", "zf_session"), "q");
    EXPECT_EQ(cookieValue("xzf_session=abc", "zf_session"), "");
    EXPECT_EQ(cookieValue("", "zf_session"), "");
    EXPECT_EQ(bearerToken("Bearer tok"), "tok");
    EXPECT_EQ(bearerToken("bearer  tok "), "tok");
    EXPECT_EQ(bearerToken("Basic dXNlcg=="), "");
    EXPECT_EQ(bearerToken("Bearer "), "");
}

TEST(HttpUtil, MediaTypeAndQuery)
{
    EXPECT_TRUE(mediaTypeIs("application/json; charset=utf-8", "application/json"));
    EXPECT_TRUE(mediaTypeIs("Application/JSON", "application/json"));
    EXPECT_FALSE(mediaTypeIs("text/plain", "application/json"));
    EXPECT_FALSE(mediaTypeIs("", "application/json"));
    std::string v;
    EXPECT_TRUE(queryParam("a=1&name=Front%20lawn+x&force", "name", v));
    EXPECT_EQ(v, "Front lawn x");
    EXPECT_TRUE(queryFlag("a=1&force", "force"));
    EXPECT_TRUE(queryFlag("force=true", "force"));
    EXPECT_FALSE(queryFlag("force=0", "force"));
    EXPECT_FALSE(queryFlag("", "force"));
    EXPECT_FALSE(urlDecode("%zz", v));
    EXPECT_FALSE(urlDecode("%00", v));
    EXPECT_FALSE(urlDecode("%4", v));
}

TEST(HttpUtil, AcceptsGzip)
{
    EXPECT_TRUE(acceptsGzip("gzip, deflate, br"));
    EXPECT_TRUE(acceptsGzip("br;q=1.0, GZIP;q=0.5"));
    EXPECT_FALSE(acceptsGzip("gzip;q=0"));
    EXPECT_FALSE(acceptsGzip("deflate"));
    EXPECT_FALSE(acceptsGzip(""));
}

TEST(StaticFiles, ResolveRules)
{
    const std::set<std::string> files = {"/index.html",         "/index.html.gz", "/assets/app-1a2b.js.gz",
                                         "/assets/app-1a2b.js", "/favicon.ico",   "/manifest.webmanifest"};
    auto exists = [&](const std::string &p) { return files.count(p) > 0; };

    StaticTarget t = resolveStatic("/", true, exists);
    EXPECT_EQ(t.kind, StaticTarget::Kind::File);
    EXPECT_EQ(t.path, "/index.html.gz");
    EXPECT_TRUE(t.gzip);
    EXPECT_STREQ(t.contentType, "text/html; charset=utf-8");
    EXPECT_STREQ(t.cacheControl, "no-cache");

    t = resolveStatic("/", false, exists);
    EXPECT_EQ(t.path, "/index.html");
    EXPECT_FALSE(t.gzip);

    t = resolveStatic("/assets/app-1a2b.js?v=1", true, exists);
    EXPECT_EQ(t.path, "/assets/app-1a2b.js.gz");
    EXPECT_STREQ(t.contentType, "text/javascript; charset=utf-8");
    EXPECT_STREQ(t.cacheControl, "public, max-age=31536000, immutable");

    t = resolveStatic("/favicon.ico", true, exists);
    EXPECT_EQ(t.path, "/favicon.ico");
    EXPECT_STREQ(t.contentType, "image/x-icon");

    // Client-side route: no extension, no file.
    t = resolveStatic("/zones/3", true, exists);
    EXPECT_EQ(t.kind, StaticTarget::Kind::File);
    EXPECT_EQ(t.path, "/index.html.gz");

    EXPECT_EQ(resolveStatic("/missing.png", true, exists).kind, StaticTarget::Kind::NotFound);
    EXPECT_EQ(resolveStatic("/../secret", true, exists).kind, StaticTarget::Kind::BadPath);
    EXPECT_EQ(resolveStatic("/%2e%2e/secret", true, exists).kind, StaticTarget::Kind::BadPath);
    EXPECT_EQ(resolveStatic("//etc", true, exists).kind, StaticTarget::Kind::BadPath);
    EXPECT_EQ(resolveStatic("relative", true, exists).kind, StaticTarget::Kind::BadPath);
}

// Security review: encoded and double-encoded traversal, NUL, backslash and
// overlong UTF-8. Decoding happens once; whatever survives must not contain
// "..", a backslash or an empty segment.
TEST(StaticFiles, HostileEncodingsNeverEscapeTheMount)
{
    const std::set<std::string> files = {"/index.html", "/assets/app.js"};
    auto exists = [&](const std::string &p) { return files.count(p) > 0; };
    for (const char *bad : {"/assets/%2E%2E/%2e%2e/secret", "/..%2fsecret", "/%2e%2e%5csecret", "/a%5c..%5cb",
                            "/index.html%00.png", "/assets//app.js", "/%2F%2Fetc", "/x/%2e%2e"}) {
        EXPECT_EQ(resolveStatic(bad, true, exists).kind, StaticTarget::Kind::BadPath) << bad;
    }
    for (const char *odd : {"/%252e%252e/secret", "/%c0%ae%c0%ae/secret", "/assets/app.js%3f.gz", "/.%2e.x"}) {
        const StaticTarget t = resolveStatic(odd, true, exists);
        EXPECT_EQ(t.path.find(".."), std::string::npos) << odd;
        EXPECT_EQ(t.path.find('\\'), std::string::npos) << odd;
    }
    // A precompressed file is served only as itself, never as "<name>.gz"
    // with a wrong Content-Encoding.
    const std::set<std::string> gz = {"/assets/app.js.gz"};
    auto gzExists = [&](const std::string &p) { return gz.count(p) > 0; };
    const StaticTarget direct = resolveStatic("/assets/app.js.gz", true, gzExists);
    EXPECT_EQ(direct.path, "/assets/app.js.gz");
    EXPECT_FALSE(direct.gzip);
    EXPECT_STREQ(direct.contentType, "application/octet-stream");
    EXPECT_EQ(resolveStatic("/assets/app.js", false, gzExists).kind, StaticTarget::Kind::NotFound);
}

TEST(HttpUtil, HostCheckEdgeCases)
{
    EXPECT_FALSE(hostAllowed("zoneflow-ab12.local.evil.com", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("zoneflow-ab12.localhost", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("xzoneflow-ab12.local", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("192.168.1.30.nip.io", "zoneflow-ab12"));
    EXPECT_FALSE(hostAllowed("1.2.3.4.", "zoneflow-ab12")); // trailing dot: a DNS name, not an address
    EXPECT_FALSE(hostAllowed(".local", "zoneflow-ab12"));
    EXPECT_FALSE(originMatchesHost("http://192.168.1.30.evil.com", "192.168.1.30"));
    EXPECT_FALSE(originMatchesHost("http://192.168.1.30@evil.com", "192.168.1.30"));
}

TEST(StaticFiles, EmptyPartitionFindsNothing)
{
    auto none = [](const std::string &) { return false; };
    EXPECT_EQ(resolveStatic("/", true, none).kind, StaticTarget::Kind::NotFound);
    EXPECT_EQ(resolveStatic("/zones", true, none).kind, StaticTarget::Kind::NotFound);
}

// ---- Security review: the JSON depth pre-check -------------------------------------

namespace {

// Nesting depth of a parsed document: what cJSON recursed through.
int treeDepth(const cJSON *n)
{
    int deepest = 0;
    for (const cJSON *c = n->child; c != nullptr; c = c->next) {
        deepest = std::max(deepest, treeDepth(c));
    }
    return (cJSON_IsArray(n) || cJSON_IsObject(n)) ? deepest + 1 : deepest;
}

// Random JSON-like text: nested arrays and objects, and strings that hold
// brackets, quotes and backslashes, some escaped correctly and some not.
void randomValue(std::mt19937 &rng, int depth, std::string &out)
{
    static constexpr std::string_view kInString = "[]{}\"\\a";
    switch (depth > 6 ? 3 : rng() % 5) {
    case 0:
        out += '[';
        for (unsigned i = rng() % 3; i > 0; --i) {
            randomValue(rng, depth + 1, out);
            out += i > 1 ? "," : "";
        }
        out += ']';
        break;
    case 1:
        out += '{';
        for (unsigned i = rng() % 3; i > 0; --i) {
            out += "\"k\":";
            randomValue(rng, depth + 1, out);
            out += i > 1 ? "," : "";
        }
        out += '}';
        break;
    case 2:
        out += '"';
        for (unsigned i = rng() % 6; i > 0; --i) {
            const char c = kInString[rng() % kInString.size()];
            if ((c == '"' || c == '\\') && rng() % 4 != 0) {
                out += '\\';
            }
            out += c;
        }
        out += '"';
        break;
    default:
        out += std::to_string(rng() % 100);
        break;
    }
}

} // namespace

// cJSON recurses once per array or object it parses, and its own limit (1000
// levels at about 64 bytes each) is far beyond the 12 KB HTTP task stack, so
// the pre-check must hold for every input, including malformed ones. 200 000
// random documents, then each with one random byte changed, inserted or
// removed: whenever the pre-check accepts and cJSON parses, the parsed tree
// is no deeper than the limit. (Where cJSON fails, it has recursed only
// through brackets it consumed, which the pre-check counted the same way.)
TEST(JsonDepth, PreCheckBoundsWhatCJsonParses)
{
    std::mt19937 rng(20261110);
    constexpr int kLimit = 4;
    constexpr std::string_view kMutations = "[]{}\"\\,:a1 ";
    int parsedDeep = 0;
    for (int i = 0; i < 200000; ++i) {
        std::string text;
        randomValue(rng, 0, text);
        if (i % 2 == 1 && !text.empty()) {
            const std::size_t at = rng() % text.size();
            const char c = kMutations[rng() % kMutations.size()];
            switch (rng() % 3) {
            case 0:
                text[at] = c;
                break;
            case 1:
                text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), c);
                break;
            default:
                text.erase(at, 1);
                break;
            }
        }
        JsonPtr doc(cJSON_ParseWithLength(text.data(), text.size()));
        if (!doc) {
            continue;
        }
        const int depth = treeDepth(doc.get());
        parsedDeep += depth > kLimit ? 1 : 0;
        if (jsonDepthOk(text, kLimit)) {
            ASSERT_LE(depth, kLimit) << text;
        }
    }
    EXPECT_GT(parsedDeep, 500); // the generator does reach past the limit
}

// Security review F7 (low). Closing brackets with nothing open drove the
// counter negative, so the opening brackets after them were not counted:
// "]]]...]" followed by 20 levels passed the check. cJSON 1.7.19 stops at
// the first value and so never parses those levels, but the check must not
// depend on that. A closer at depth 0 now fails the check (it is not JSON).
TEST(JsonDepth, UnbalancedClosersCannotHideNesting)
{
    const std::string deep = std::string(20, '[') + std::string(20, ']');
    EXPECT_FALSE(jsonDepthOk(deep));
    EXPECT_FALSE(jsonDepthOk(std::string(30, ']') + deep));
    EXPECT_FALSE(jsonDepthOk("[1]" + std::string(30, ']') + deep));
    EXPECT_TRUE(jsonDepthOk(R"({"a":"[[[[[[[[[[[[[[[[[[[[[["})")); // brackets in strings do not count
    EXPECT_TRUE(jsonDepthOk(R"({"a":"\\\"]]]]"})"));               // escaped quote keeps the string open
}
