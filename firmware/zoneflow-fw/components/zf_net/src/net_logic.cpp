// SPDX-License-Identifier: Apache-2.0
#include "zf_net/net_logic.hpp"

#include <algorithm>
#include <cstring>

namespace zf::net {

const char *toString(NetState s)
{
    switch (s) {
    case NetState::Off:
        return "off";
    case NetState::Provisioning:
        return "provisioning";
    case NetState::Connecting:
        return "connecting";
    case NetState::Connected:
        return "connected";
    case NetState::WaitingRetry:
        return "waiting_retry";
    }
    return "?";
}

const char *toString(ProvTransport t)
{
    switch (t) {
    case ProvTransport::None:
        return "none";
    case ProvTransport::Ble:
        return "ble";
    case ProvTransport::SoftAp:
        return "softap";
    }
    return "?";
}

ProvTransport transportFromString(std::string_view s)
{
    if (s == "ble") {
        return ProvTransport::Ble;
    }
    if (s == "softap") {
        return ProvTransport::SoftAp;
    }
    return ProvTransport::None;
}

const char *toString(ProvAction a)
{
    switch (a) {
    case ProvAction::None:
        return "none";
    case ProvAction::SwitchToSoftAp:
        return "switch_to_softap";
    case ProvAction::AbandonToStation:
        return "abandon_to_station";
    case ProvAction::RetryAfterFailure:
        return "retry_after_failure";
    case ProvAction::Finish:
        return "finish";
    }
    return "?";
}

// ---- backoff ------------------------------------------------------------------

uint32_t Backoff::next(uint32_t random)
{
    const uint32_t shift = std::min<uint32_t>(attempt_, 24);
    const uint64_t base = std::min<uint64_t>(uint64_t{initialMs_} << shift, maxMs_);
    if (attempt_ < UINT32_MAX) {
        ++attempt_;
    }
    if (jitter_ == 0) {
        return static_cast<uint32_t>(base);
    }
    const uint32_t span = 2u * jitter_ + 1u;
    const uint32_t percent = 100u - jitter_ + random % span; // 100-j .. 100+j
    return static_cast<uint32_t>(base * percent / 100u);
}

bool StationPolicy::start()
{
    backoff_.reset();
    waiting_ = false;
    return true;
}

void StationPolicy::onDisconnected(MonoMs now, uint32_t random)
{
    retryAt_ = now + backoff_.next(random);
    waiting_ = true;
}

void StationPolicy::onConnected()
{
    backoff_.reset();
    waiting_ = false;
}

bool StationPolicy::due(MonoMs now)
{
    if (waiting_ && now >= retryAt_) {
        waiting_ = false;
        return true;
    }
    return false;
}

// ---- provisioning ---------------------------------------------------------------

BootPlan ProvisioningFsm::plan(bool provisioned, ProvTransport requested, bool bleAvailable)
{
    BootPlan p;
    if (requested == ProvTransport::Ble && !bleAvailable) {
        requested = ProvTransport::SoftAp;
    }
    if (requested != ProvTransport::None) {
        p.provision = true;
        p.transport = requested;
    } else if (!provisioned) {
        p.provision = true;
        p.transport = bleAvailable ? ProvTransport::Ble : ProvTransport::SoftAp;
    }
    return p;
}

void ProvisioningFsm::begin(ProvTransport transport, bool provisioned, MonoMs now)
{
    transport_ = transport;
    provisioned_ = provisioned;
    active_ = transport != ProvTransport::None;
    session_ = false;
    since_ = now;
    failures_ = 0;
}

ProvAction ProvisioningFsm::onCredentialsFailed()
{
    if (!active_) {
        return ProvAction::None;
    }
    ++failures_;
    session_ = false;
    return ProvAction::RetryAfterFailure;
}

ProvAction ProvisioningFsm::onSuccess()
{
    if (!active_) {
        return ProvAction::None;
    }
    active_ = false;
    provisioned_ = true;
    transport_ = ProvTransport::None;
    return ProvAction::Finish;
}

ProvAction ProvisioningFsm::tick(MonoMs now)
{
    if (!active_) {
        return ProvAction::None;
    }
    const MonoMs elapsed = now - since_;
    if (session_ && elapsed < timeouts_.heldWindowMaxMs) {
        return ProvAction::None;
    }
    if (provisioned_) {
        if (elapsed >= timeouts_.reprovisionWindowMs) {
            active_ = false;
            transport_ = ProvTransport::None;
            return ProvAction::AbandonToStation;
        }
        return ProvAction::None;
    }
    if (transport_ == ProvTransport::Ble && elapsed >= timeouts_.bleWindowMs) {
        transport_ = ProvTransport::SoftAp;
        since_ = now;
        return ProvAction::SwitchToSoftAp;
    }
    return ProvAction::None;
}

// ---- names ---------------------------------------------------------------------

namespace {

std::string hex2(uint8_t b, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    return {digits[b >> 4], digits[b & 0x0f]};
}

void appendJsonString(std::string &out, std::string_view s)
{
    out.push_back('"');
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            static constexpr char kHex[] = "0123456789abcdef";
            out += "\\u00";
            out.push_back(kHex[(c >> 4) & 0x0f]);
            out.push_back(kHex[c & 0x0f]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

} // namespace

std::string makeHostname(const std::array<uint8_t, 6> &mac)
{
    return "zoneflow-" + hex2(mac[4], false) + hex2(mac[5], false);
}

std::string makeServiceName(const std::array<uint8_t, 6> &mac)
{
    return "ZoneFlow-" + hex2(mac[4], true) + hex2(mac[5], true);
}

std::string makeSetupCode(const uint8_t *random, std::size_t len)
{
    std::string code;
    for (std::size_t i = 0; i < kSetupCodeLen && i < len; ++i) {
        code.push_back(kSetupCodeAlphabet[random[i] & 0x1f]);
    }
    return code;
}

bool validSetupCode(std::string_view code)
{
    return code.size() == kSetupCodeLen && std::all_of(code.begin(), code.end(), [](char c) {
               return kSetupCodeAlphabet.find(c) != std::string_view::npos;
           });
}

std::string qrPayload(std::string_view serviceName, std::string_view setupCode, ProvTransport transport)
{
    std::string out = "{\"ver\":\"v1\",\"name\":";
    appendJsonString(out, serviceName);
    out += ",\"username\":";
    appendJsonString(out, kProvUsername);
    out += ",\"pop\":";
    appendJsonString(out, setupCode);
    out += ",\"transport\":";
    appendJsonString(out, transport == ProvTransport::SoftAp ? "softap" : "ble");
    out += "}";
    return out;
}

// ---- captive DNS -------------------------------------------------------------------

std::size_t captiveDnsAnswer(const uint8_t *q, std::size_t len, uint32_t ipv4, uint8_t *out, std::size_t outCap)
{
    constexpr std::size_t kHeader = 12;
    if (len < kHeader + 5 || len > 512) {
        return 0;
    }
    const bool isResponse = (q[2] & 0x80) != 0;
    const unsigned opcode = (q[2] >> 3) & 0x0f;
    const unsigned qdcount = (unsigned{q[4]} << 8) | q[5];
    if (isResponse || opcode != 0 || qdcount != 1) {
        return 0;
    }
    // Walk the QNAME labels (no compression in a question).
    std::size_t p = kHeader;
    while (true) {
        if (p >= len) {
            return 0;
        }
        const uint8_t l = q[p];
        if (l == 0) {
            ++p;
            break;
        }
        if ((l & 0xC0) != 0 || l > 63) {
            return 0;
        }
        p += 1 + l;
        if (p - kHeader > 255) {
            return 0;
        }
    }
    if (p + 4 > len) {
        return 0;
    }
    const unsigned qtype = (unsigned{q[p]} << 8) | q[p + 1];
    const unsigned qclass = (unsigned{q[p + 2]} << 8) | q[p + 3];
    const std::size_t questionEnd = p + 4;
    const bool answerA = (qtype == 1 || qtype == 255) && qclass == 1;
    const std::size_t total = questionEnd + (answerA ? 16 : 0);
    if (total > outCap) {
        return 0;
    }
    std::memcpy(out, q, questionEnd);
    out[2] = static_cast<uint8_t>(0x80 | (q[2] & 0x01)); // QR, opcode 0, keep RD
    out[3] = 0x80;                                       // RA, RCODE 0
    out[6] = 0;
    out[7] = answerA ? 1 : 0; // ANCOUNT
    out[8] = out[9] = out[10] = out[11] = 0;
    if (answerA) {
        uint8_t *a = out + questionEnd;
        const uint8_t answer[16] = {0xC0,
                                    0x0C, // name: pointer to the question
                                    0x00,
                                    0x01, // type A
                                    0x00,
                                    0x01, // class IN
                                    0x00,
                                    0x00,
                                    0x00,
                                    0x3C, // TTL 60 s
                                    0x00,
                                    0x04,
                                    static_cast<uint8_t>(ipv4 >> 24),
                                    static_cast<uint8_t>(ipv4 >> 16),
                                    static_cast<uint8_t>(ipv4 >> 8),
                                    static_cast<uint8_t>(ipv4)};
        std::memcpy(a, answer, sizeof answer);
    }
    return total;
}

} // namespace zf::net
