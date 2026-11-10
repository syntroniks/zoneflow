// SPDX-License-Identifier: Apache-2.0
// Test doubles for the web, network and OTA modules.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "fakes.hpp"

#include "zf/events/command.hpp"
#include "zf_ota/ota_logic.hpp"
#include "zf_web/api.hpp"
#include "zf_web/auth.hpp"

namespace zf::test {

// Deterministic "random" bytes: a counter. Good enough to tell values apart.
class SeqRandom final : public web::IRandom {
public:
    uint8_t next = 1;
    void fill(uint8_t *out, std::size_t len) override
    {
        for (std::size_t i = 0; i < len; ++i) {
            out[i] = next++;
        }
    }
};

class RecordingSink final : public ICommandSink {
public:
    std::vector<Command> commands;
    bool full = false;
    bool post(const Command &cmd) override
    {
        if (full) {
            return false;
        }
        commands.push_back(cmd);
        return true;
    }
    int count(CommandType t) const
    {
        int n = 0;
        for (const Command &c : commands) {
            n += c.type == t ? 1 : 0;
        }
        return n;
    }
};

class FakePlatform final : public web::IPlatformPort {
public:
    net::NetStatus net;
    std::string code = "ABCDEFGHJK";
    ota::OtaStatus ota;
    ota::OtaError pullResult = ota::OtaError::None;
    std::string pulledUrl;
    bool pulledForce = false;
    std::vector<std::pair<std::string, std::string>> provisioned;
    bool acceptProvision = true;

    FakePlatform()
    {
        net.hostname.assign("zoneflow-ab12");
        net.state = net::NetState::Connected;
        net.provisioned = true;
        net.ip.assign("192.168.1.30");
    }

    web::FirmwareInfo firmware() const override
    {
        return {"1.2.3", "zoneflow-fw", "v5.5.6", "Nov 10 2026 12:00:00", "zoneflow-rev-a"};
    }
    web::SystemInfo system() const override
    {
        web::SystemInfo s;
        s.uptimeMs = 5000;
        s.freeHeap = 100000;
        s.resetReason = "poweron";
        return s;
    }
    net::NetStatus network() const override { return net; }
    std::string setupCode() const override { return code; }
    std::vector<web::WifiNetwork> scan() override { return {{"home", -50, true}, {"cafe", -80, false}}; }
    bool provisionWifi(std::string_view ssid, std::string_view password) override
    {
        provisioned.emplace_back(ssid, password);
        return acceptProvision;
    }
    ota::OtaStatus otaStatus() const override { return ota; }
    web::SlotInfo slots() const override { return {"ota_0", "ota_1", "valid", "1.2.2"}; }
    ota::OtaError startPull(std::string_view url, bool force) override
    {
        pulledUrl = url;
        pulledForce = force;
        return pullResult;
    }
    std::string tlsFingerprint() const override { return ""; }

    // MQTT module stand-in.
    mqtt::MqttStatus mqtt;
    std::string mqttPassword;
    std::string mqttCa;
    bool mqttStoreFails = false;
    int mqttSecretWrites = 0;
    mqtt::MqttStatus mqttStatus() const override { return mqtt; }
    const HistoryLog *historyLog = nullptr;
    const HistoryLog *history() const override { return historyLog; }
    web::MqttSecretsInfo mqttSecrets() const override { return {!mqttPassword.empty(), !mqttCa.empty()}; }
    bool setMqttSecrets(std::optional<std::string_view> password, std::optional<std::string_view> caPem) override
    {
        if (mqttStoreFails) {
            return false;
        }
        ++mqttSecretWrites;
        if (password) {
            mqttPassword = std::string(*password);
        }
        if (caPem) {
            mqttCa = std::string(*caPem);
        }
        return true;
    }

    // sleepMs() advances this clock and then runs onSleep, which plays the
    // control task (publish a snapshot, a CommandRejected event, ...).
    FakeClock *clock = nullptr;
    std::function<void()> onSleep;
    uint32_t sleptMs = 0;
    void sleepMs(uint32_t ms) override
    {
        sleptMs += ms;
        if (clock != nullptr) {
            clock->advanceMs(ms);
        }
        if (onSleep) {
            onSleep();
        }
    }
};

} // namespace zf::test
