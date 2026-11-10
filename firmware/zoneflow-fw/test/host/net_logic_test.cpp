// SPDX-License-Identifier: Apache-2.0
// Network manager logic: reconnect backoff, provisioning state machine,
// names, setup code, QR payload and the captive-portal DNS answer.
#include <gtest/gtest.h>

#include <vector>

#include "cJSON.h"
#include "zf_net/net_logic.hpp"

using namespace zf;
using namespace zf::net;

TEST(Backoff, DoublesToCapWithoutJitter)
{
    Backoff b(1000, 60000, 0);
    std::vector<uint32_t> got;
    for (int i = 0; i < 9; ++i) {
        got.push_back(b.next(0));
    }
    EXPECT_EQ(got, (std::vector<uint32_t>{1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000, 60000}));
    EXPECT_EQ(b.attempts(), 9u);
    b.reset();
    EXPECT_EQ(b.next(0), 1000u);
}

TEST(Backoff, JitterStaysWithinBounds)
{
    Backoff b(1000, 300000, 20);
    for (uint32_t r = 0; r < 200; ++r) {
        b.reset();
        const uint32_t d = b.next(r * 7919u);
        EXPECT_GE(d, 800u);
        EXPECT_LE(d, 1200u);
    }
    Backoff big(1000, 300000, 20);
    for (int i = 0; i < 40; ++i) {
        const uint32_t d = big.next(0xFFFFFFFFu);
        EXPECT_LE(d, 360000u); // cap + 20 %
    }
}

TEST(StationPolicy, RetriesAfterBackoffAndResetsOnConnect)
{
    StationPolicy p(Backoff(1000, 300000, 0));
    EXPECT_TRUE(p.start());
    p.onDisconnected(10000, 0);
    EXPECT_TRUE(p.waiting());
    EXPECT_EQ(p.retryInMs(10500), 500u);
    EXPECT_FALSE(p.due(10999));
    EXPECT_TRUE(p.due(11000));
    EXPECT_FALSE(p.due(11001)); // fires once
    p.onDisconnected(11000, 0);
    EXPECT_FALSE(p.due(12999));
    EXPECT_TRUE(p.due(13000)); // 2 s the second time
    EXPECT_EQ(p.attempts(), 2u);
    p.onConnected();
    EXPECT_EQ(p.attempts(), 0u);
    p.onDisconnected(20000, 0);
    EXPECT_TRUE(p.due(21000)); // back to 1 s
}

TEST(Provisioning, BootPlan)
{
    BootPlan p = ProvisioningFsm::plan(false, ProvTransport::None, true);
    EXPECT_TRUE(p.provision);
    EXPECT_EQ(p.transport, ProvTransport::Ble);
    p = ProvisioningFsm::plan(true, ProvTransport::None, true);
    EXPECT_FALSE(p.provision);
    p = ProvisioningFsm::plan(true, ProvTransport::SoftAp, true);
    EXPECT_TRUE(p.provision);
    EXPECT_EQ(p.transport, ProvTransport::SoftAp);
    p = ProvisioningFsm::plan(false, ProvTransport::None, false);
    EXPECT_EQ(p.transport, ProvTransport::SoftAp);
    p = ProvisioningFsm::plan(true, ProvTransport::Ble, false);
    EXPECT_EQ(p.transport, ProvTransport::SoftAp);
}

TEST(Provisioning, UnprovisionedBleFallsBackToSoftApOnce)
{
    ProvisioningFsm f;
    f.begin(ProvTransport::Ble, false, 0);
    EXPECT_EQ(f.tick(10 * 60 * 1000 - 1), ProvAction::None);
    EXPECT_EQ(f.tick(10 * 60 * 1000), ProvAction::SwitchToSoftAp);
    EXPECT_EQ(f.transport(), ProvTransport::SoftAp);
    // SoftAP then stays up: an unprovisioned device has nothing else to do.
    EXPECT_EQ(f.tick(48ull * 3600 * 1000), ProvAction::None);
    EXPECT_TRUE(f.active());
}

TEST(Provisioning, OpenSessionHoldsTheTransport)
{
    ProvisioningFsm f;
    f.begin(ProvTransport::Ble, false, 0);
    f.onSessionStarted();
    EXPECT_EQ(f.tick(20 * 60 * 1000), ProvAction::None);
    f.onSessionEnded();
    EXPECT_EQ(f.tick(20 * 60 * 1000), ProvAction::SwitchToSoftAp);
}

// Security review F3. A session held the window open without limit, and the
// glue counted any BLE connection as a session, before Security 2. Someone
// in radio range without the setup code could keep a re-provisioning device
// off its network (and an unprovisioned one stuck on BLE, with the only BLE
// connection taken) for as long as they stayed connected. The glue now
// counts a session only after the Security 2 handshake; and even a real
// session holds the window for at most heldWindowMaxMs.
TEST(Provisioning, AnOpenSessionHoldsTheWindowOnlyUpToTheLimit)
{
    const MonoMs limit = ProvisioningTimeouts{}.heldWindowMaxMs;
    EXPECT_EQ(limit, 30ull * 60ull * 1000ull);
    ProvisioningFsm re;
    re.begin(ProvTransport::Ble, true, 1000);
    re.onSessionStarted();
    EXPECT_EQ(re.tick(1000 + limit - 1), ProvAction::None);
    EXPECT_EQ(re.tick(1000 + limit), ProvAction::AbandonToStation);

    ProvisioningFsm fresh;
    fresh.begin(ProvTransport::Ble, false, 0);
    fresh.onSessionStarted();
    EXPECT_EQ(fresh.tick(limit - 1), ProvAction::None);
    EXPECT_EQ(fresh.tick(limit), ProvAction::SwitchToSoftAp);
    // The SoftAP of an unprovisioned device still stays up.
    fresh.onSessionStarted();
    EXPECT_EQ(fresh.tick(10 * limit), ProvAction::None);
    EXPECT_TRUE(fresh.active());
}

TEST(Provisioning, ReprovisionGivesUpAndKeepsOldNetwork)
{
    ProvisioningFsm f;
    f.begin(ProvTransport::Ble, true, 1000);
    EXPECT_EQ(f.tick(1000 + 15 * 60 * 1000 - 1), ProvAction::None);
    EXPECT_EQ(f.tick(1000 + 15 * 60 * 1000), ProvAction::AbandonToStation);
    EXPECT_FALSE(f.active());
    EXPECT_EQ(f.tick(1000 + 30 * 60 * 1000), ProvAction::None);
}

TEST(Provisioning, FailuresAllowRetryAndSuccessFinishes)
{
    ProvisioningFsm f;
    f.begin(ProvTransport::SoftAp, false, 0);
    f.onSessionStarted();
    EXPECT_EQ(f.onCredentialsFailed(), ProvAction::RetryAfterFailure);
    EXPECT_EQ(f.onCredentialsFailed(), ProvAction::RetryAfterFailure);
    EXPECT_EQ(f.failures(), 2u);
    EXPECT_TRUE(f.active());
    EXPECT_EQ(f.onSuccess(), ProvAction::Finish);
    EXPECT_FALSE(f.active());
    EXPECT_EQ(f.onSuccess(), ProvAction::None);
    EXPECT_EQ(f.onCredentialsFailed(), ProvAction::None);
}

TEST(Names, HostnameAndServiceName)
{
    const std::array<uint8_t, 6> mac = {0x24, 0x0a, 0xc4, 0x12, 0xAB, 0x0C};
    EXPECT_EQ(makeHostname(mac), "zoneflow-ab0c");
    EXPECT_EQ(makeServiceName(mac), "ZoneFlow-AB0C");
}

TEST(SetupCode, AlphabetAndLength)
{
    EXPECT_EQ(kSetupCodeAlphabet.size(), 32u);
    for (char c : std::string_view("01IO")) {
        EXPECT_EQ(kSetupCodeAlphabet.find(c), std::string_view::npos) << c;
    }
    uint8_t rnd[10];
    for (int i = 0; i < 10; ++i) {
        rnd[i] = static_cast<uint8_t>(i * 37 + 255);
    }
    const std::string code = makeSetupCode(rnd, sizeof rnd);
    EXPECT_EQ(code.size(), kSetupCodeLen);
    EXPECT_TRUE(validSetupCode(code));
    EXPECT_FALSE(validSetupCode("ABCDEFGHJ"));
    EXPECT_FALSE(validSetupCode("ABCDEFGHJ0"));
    EXPECT_FALSE(validSetupCode("abcdefghjk"));
    // Usable as a WPA2 passphrase (8..63 printable).
    EXPECT_GE(code.size(), 8u);
}

TEST(QrPayload, MatchesEspressifFormat)
{
    const std::string p = qrPayload("ZoneFlow-AB0C", "ABCDEFGHJK", ProvTransport::Ble);
    EXPECT_EQ(p, R"({"ver":"v1","name":"ZoneFlow-AB0C","username":"wifiprov","pop":"ABCDEFGHJK","transport":"ble"})");
    cJSON *o = cJSON_Parse(qrPayload("a\"b", "X", ProvTransport::SoftAp).c_str());
    ASSERT_NE(o, nullptr);
    EXPECT_STREQ(cJSON_GetObjectItem(o, "name")->valuestring, "a\"b");
    EXPECT_STREQ(cJSON_GetObjectItem(o, "transport")->valuestring, "softap");
    cJSON_Delete(o);
}

namespace {

std::vector<uint8_t> query(const char *name, uint16_t qtype, uint16_t id = 0x1234)
{
    std::vector<uint8_t> q = {
        static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id), 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    std::string_view n(name);
    while (!n.empty()) {
        const std::size_t dot = n.find('.');
        const std::string_view label = n.substr(0, dot);
        q.push_back(static_cast<uint8_t>(label.size()));
        q.insert(q.end(), label.begin(), label.end());
        n = dot == std::string_view::npos ? std::string_view{} : n.substr(dot + 1);
    }
    q.push_back(0);
    q.push_back(static_cast<uint8_t>(qtype >> 8));
    q.push_back(static_cast<uint8_t>(qtype));
    q.push_back(0);
    q.push_back(1);
    return q;
}

} // namespace

TEST(CaptiveDns, AnswersARecordsWithOwnAddress)
{
    const auto q = query("connectivitycheck.gstatic.com", 1);
    uint8_t out[512];
    const std::size_t n = captiveDnsAnswer(q.data(), q.size(), 0xC0A80401, out, sizeof out);
    ASSERT_EQ(n, q.size() + 16);
    EXPECT_EQ(out[0], 0x12);
    EXPECT_EQ(out[1], 0x34);
    EXPECT_EQ(out[2] & 0x80, 0x80); // response
    EXPECT_EQ(out[2] & 0x01, 0x01); // RD copied
    EXPECT_EQ(out[3] & 0x0F, 0);    // NOERROR
    EXPECT_EQ(out[7], 1);           // one answer
    EXPECT_EQ(out[n - 4], 192);
    EXPECT_EQ(out[n - 3], 168);
    EXPECT_EQ(out[n - 2], 4);
    EXPECT_EQ(out[n - 1], 1);
}

TEST(CaptiveDns, OtherTypesGetEmptyAnswer)
{
    const auto q = query("example.com", 28); // AAAA
    uint8_t out[512];
    const std::size_t n = captiveDnsAnswer(q.data(), q.size(), 0xC0A80401, out, sizeof out);
    ASSERT_EQ(n, q.size());
    EXPECT_EQ(out[7], 0);
}

TEST(CaptiveDns, DropsMalformedAndResponses)
{
    uint8_t out[512];
    auto q = query("a.b", 1);
    q[2] |= 0x80; // a response
    EXPECT_EQ(captiveDnsAnswer(q.data(), q.size(), 1, out, sizeof out), 0u);
    q = query("a.b", 1);
    q[12] = 60; // label runs past the end
    EXPECT_EQ(captiveDnsAnswer(q.data(), q.size(), 1, out, sizeof out), 0u);
    q = query("a.b", 1);
    q[12] = 0xC0; // compression pointer in a question
    EXPECT_EQ(captiveDnsAnswer(q.data(), q.size(), 1, out, sizeof out), 0u);
    q = query("a.b", 1);
    EXPECT_EQ(captiveDnsAnswer(q.data(), 10, 1, out, sizeof out), 0u);
    EXPECT_EQ(captiveDnsAnswer(q.data(), q.size(), 1, out, q.size()), 0u); // no room for the answer
    q = query("a.b", 1);
    q[5] = 2; // two questions
    EXPECT_EQ(captiveDnsAnswer(q.data(), q.size(), 1, out, sizeof out), 0u);
}

TEST(NetStatusStore, SequenceAdvances)
{
    NetStatusStore s;
    const uint32_t before = s.sequence();
    NetStatus n;
    n.state = NetState::Connected;
    s.publish(n);
    EXPECT_EQ(s.sequence(), before + 1);
    EXPECT_EQ(s.snapshot().state, NetState::Connected);
}
