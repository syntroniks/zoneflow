// SPDX-License-Identifier: Apache-2.0
// Keeps docs/firmware/openapi.json and the firmware route table in step:
// every documented operation is registered, every registered route is
// documented, and the documented security, body type, body cap and response
// type match the table.
#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "cJSON.h"
#include "zf_web/routes.hpp"

using namespace zf::web;

namespace {

struct Doc {
    cJSON *root = nullptr;
    Doc()
    {
        std::ifstream in(ZF_OPENAPI_PATH);
        std::stringstream ss;
        ss << in.rdbuf();
        root = cJSON_Parse(ss.str().c_str());
    }
    ~Doc() { cJSON_Delete(root); }
};

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char &c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

const cJSON *operation(const cJSON *paths, const Route &r)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(paths, std::string(r.pattern).c_str());
    return cJSON_GetObjectItemCaseSensitive(item, lower(r.method).c_str());
}

// "anonymous" = security list contains {}; "none" = empty list.
struct SecurityShape {
    bool present = false;
    bool empty = false;
    bool anonymous = false;
    bool bearer = false;
    bool cookie = false;
};

SecurityShape security(const cJSON *op)
{
    SecurityShape s;
    const cJSON *sec = cJSON_GetObjectItemCaseSensitive(op, "security");
    if (!cJSON_IsArray(sec)) {
        return s;
    }
    s.present = true;
    s.empty = cJSON_GetArraySize(sec) == 0;
    const cJSON *req = nullptr;
    cJSON_ArrayForEach(req, sec)
    {
        if (cJSON_GetArraySize(req) == 0) {
            s.anonymous = true;
        }
        s.bearer = s.bearer || cJSON_HasObjectItem(req, "bearerAuth");
        s.cookie = s.cookie || cJSON_HasObjectItem(req, "cookieAuth");
    }
    return s;
}

} // namespace

TEST(OpenApi, DocumentParsesAndIsVersion31)
{
    Doc doc;
    ASSERT_NE(doc.root, nullptr) << "cannot read or parse " << ZF_OPENAPI_PATH;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(doc.root, "openapi");
    ASSERT_TRUE(cJSON_IsString(v));
    EXPECT_EQ(std::string(v->valuestring).rfind("3.1", 0), 0u);
    const cJSON *servers = cJSON_GetObjectItemCaseSensitive(doc.root, "servers");
    ASSERT_TRUE(cJSON_IsArray(servers));
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetArrayItem(servers, 0), "url")->valuestring, "/api/v1");
}

TEST(OpenApi, EveryRouteIsDocumentedWithMatchingContract)
{
    Doc doc;
    ASSERT_NE(doc.root, nullptr);
    const cJSON *paths = cJSON_GetObjectItemCaseSensitive(doc.root, "paths");
    for (const Route &r : routes()) {
        SCOPED_TRACE(std::string(r.method) + " " + std::string(r.pattern));
        const cJSON *op = operation(paths, r);
        ASSERT_NE(op, nullptr) << "route not in openapi.json";

        const SecurityShape s = security(op);
        ASSERT_TRUE(s.present) << "operation must state its security";
        switch (r.access) {
        case Access::Public:
        case Access::Provisioning:
            EXPECT_TRUE(s.empty);
            break;
        case Access::Read:
            EXPECT_TRUE(s.anonymous && s.bearer && s.cookie);
            break;
        case Access::Admin:
            EXPECT_TRUE(!s.anonymous && !s.empty && s.bearer && s.cookie);
            break;
        }

        const cJSON *body = cJSON_GetObjectItemCaseSensitive(op, "requestBody");
        const cJSON *content = cJSON_GetObjectItemCaseSensitive(body, "content");
        switch (r.body) {
        case Body::None:
            EXPECT_EQ(body, nullptr);
            break;
        case Body::Json:
            ASSERT_NE(content, nullptr);
            EXPECT_TRUE(cJSON_HasObjectItem(content, "application/json"));
            break;
        case Body::Binary:
            ASSERT_NE(content, nullptr);
            EXPECT_TRUE(cJSON_HasObjectItem(content, "application/octet-stream"));
            break;
        }
        if (r.body != Body::None) {
            const cJSON *cap = cJSON_GetObjectItemCaseSensitive(body, "x-max-body-bytes");
            ASSERT_TRUE(cJSON_IsNumber(cap)) << "requestBody needs x-max-body-bytes";
            EXPECT_EQ(static_cast<uint32_t>(cap->valuedouble), r.maxBody);
        }

        const cJSON *ok = nullptr;
        const cJSON *responses = cJSON_GetObjectItemCaseSensitive(op, "responses");
        ASSERT_TRUE(cJSON_IsObject(responses));
        cJSON_ArrayForEach(ok, responses)
        {
            if (ok->string[0] == '2') {
                break;
            }
        }
        ASSERT_NE(ok, nullptr) << "no 2xx response";
        const cJSON *okContent = cJSON_GetObjectItemCaseSensitive(ok, "content");
        if (r.response == Response::EventStream) {
            EXPECT_TRUE(cJSON_HasObjectItem(okContent, "text/event-stream"));
        } else {
            EXPECT_TRUE(cJSON_HasObjectItem(okContent, "application/json"));
        }
        EXPECT_TRUE(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(op, "operationId")));
    }
}

TEST(OpenApi, EveryDocumentedOperationIsRegistered)
{
    Doc doc;
    ASSERT_NE(doc.root, nullptr);
    const cJSON *paths = cJSON_GetObjectItemCaseSensitive(doc.root, "paths");
    const std::set<std::string> methods = {"get", "put", "post", "delete", "patch", "head", "options", "trace"};
    std::set<std::string> operationIds;
    std::size_t count = 0;
    const cJSON *path = nullptr;
    cJSON_ArrayForEach(path, paths)
    {
        const cJSON *op = nullptr;
        cJSON_ArrayForEach(op, path)
        {
            if (methods.count(op->string) == 0) {
                continue; // "parameters", "summary"
            }
            ++count;
            std::string method = op->string;
            for (char &c : method) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            const RouteMatch m = matchRoute(method, std::string(kApiPrefix) + path->string);
            ASSERT_NE(m.route, nullptr) << method << " " << path->string << " is documented but not registered";
            EXPECT_EQ(m.route->pattern, std::string_view(path->string));
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(op, "operationId");
            if (cJSON_IsString(id)) {
                EXPECT_TRUE(operationIds.insert(id->valuestring).second) << "duplicate operationId " << id->valuestring;
            }
        }
    }
    EXPECT_EQ(count, routes().size());
}
