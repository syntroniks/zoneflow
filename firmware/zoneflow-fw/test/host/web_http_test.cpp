// SPDX-License-Identifier: Apache-2.0
// Route matching, header helpers and static-file resolution.
#include <gtest/gtest.h>

#include <set>

#include "zf_web/http_util.hpp"
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

TEST(StaticFiles, EmptyPartitionFindsNothing)
{
    auto none = [](const std::string &) { return false; };
    EXPECT_EQ(resolveStatic("/", true, none).kind, StaticTarget::Kind::NotFound);
    EXPECT_EQ(resolveStatic("/zones", true, none).kind, StaticTarget::Kind::NotFound);
}
