// SPDX-License-Identifier: Apache-2.0
// Pure logic of the network manager: status record, reconnect backoff,
// provisioning state machine, device names, setup code, QR payload and the
// captive-portal DNS answer. No ESP-IDF headers; built by test/host.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "zf/fixed_string.hpp"
#include "zf/types.hpp"

namespace zf::net {

enum class NetState : uint8_t {
    Off,          // WiFi not started
    Provisioning, // provisioning service running (BLE or SoftAP)
    Connecting,   // station connecting or waiting for DHCP
    Connected,    // station has an IPv4 address
    WaitingRetry, // disconnected; next attempt after the backoff delay
};

enum class ProvTransport : uint8_t { None, Ble, SoftAp };

const char *toString(NetState s);
const char *toString(ProvTransport t);
// "ble" / "softap"; anything else gives None.
ProvTransport transportFromString(std::string_view s);

struct NetStatus {
    NetState state = NetState::Off;
    ProvTransport provisioning = ProvTransport::None; // active provisioning transport
    bool provisioned = false;                         // station credentials stored
    FixedString<32> ssid;
    FixedString<17> bssid; // "aa:bb:cc:dd:ee:ff" of the current AP
    uint8_t channel = 0;
    int8_t rssi = 0;
    FixedString<15> ip;
    FixedString<15> netmask;
    FixedString<15> gateway;
    FixedString<31> hostname;         // "zoneflow-ab12"
    FixedString<17> mac;              // station MAC
    uint32_t reconnectAttempt = 0;    // since the last successful connection
    uint32_t retryInMs = 0;           // WaitingRetry only
    uint8_t lastDisconnectReason = 0; // wifi_err_reason_t value
    uint32_t disconnects = 0;         // since boot
    MonoMs connectedSinceMs = 0;
    bool mdnsRunning = false;
};

// Single writer (the net task), any number of readers. Same pattern as
// zf::StateStore: the mutex is held for one struct copy.
class NetStatusStore {
public:
    void publish(const NetStatus &s)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = s;
        sequence_.fetch_add(1, std::memory_order_release);
    }
    NetStatus snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }
    uint32_t sequence() const { return sequence_.load(std::memory_order_acquire); }

private:
    mutable std::mutex mutex_;
    NetStatus status_{};
    std::atomic<uint32_t> sequence_{0};
};

// ---- reconnect backoff -------------------------------------------------------

// Exponential backoff with jitter: initial * 2^n, capped, then scaled by a
// random factor in [100 - jitter, 100 + jitter] percent so that many devices
// that lost the same AP do not retry in step.
class Backoff {
public:
    Backoff(uint32_t initialMs = 1000, uint32_t maxMs = 5u * 60u * 1000u, uint8_t jitterPercent = 20)
        : initialMs_(initialMs), maxMs_(maxMs), jitter_(jitterPercent > 100 ? 100 : jitterPercent)
    {
    }
    // Delay before the next attempt. `random` is any uniformly random value.
    uint32_t next(uint32_t random);
    void reset() { attempt_ = 0; }
    uint32_t attempts() const { return attempt_; }

private:
    uint32_t initialMs_;
    uint32_t maxMs_;
    uint8_t jitter_;
    uint32_t attempt_ = 0;
};

// Station reconnect policy. The driver never locks to a BSSID or channel:
// every attempt is a full scan that picks the strongest AP of the SSID, so
// mesh nodes and channel changes are followed.
class StationPolicy {
public:
    explicit StationPolicy(Backoff backoff = Backoff{}) : backoff_(backoff) {}

    // Returns true: connect now.
    bool start();
    // Disconnected (or connect failed): schedule the next attempt.
    void onDisconnected(MonoMs now, uint32_t random);
    void onConnected();
    // True when a scheduled attempt is due; the caller then connects.
    bool due(MonoMs now);
    bool waiting() const { return waiting_; }
    uint32_t retryInMs(MonoMs now) const
    {
        return waiting_ && retryAt_ > now ? static_cast<uint32_t>(retryAt_ - now) : 0;
    }
    uint32_t attempts() const { return backoff_.attempts(); }

private:
    Backoff backoff_;
    bool waiting_ = false;
    MonoMs retryAt_ = 0;
};

// ---- provisioning -------------------------------------------------------------

struct ProvisioningTimeouts {
    // Unprovisioned device: BLE alone for this long, then the SoftAP takes
    // over (BLE memory is released when the BLE scheme stops, so the switch
    // is one-way until the next boot).
    MonoMs bleWindowMs = 10ull * 60ull * 1000ull;
    // Re-provisioning a device that already has credentials: give up after
    // this long and go back to the stored network.
    MonoMs reprovisionWindowMs = 15ull * 60ull * 1000ull;
    // A connected phone or browser holds the window open, but not for longer
    // than this from the start of the window (security review F3). Does not
    // end the SoftAP of an unprovisioned device, which has nothing else to do.
    MonoMs heldWindowMaxMs = 30ull * 60ull * 1000ull;
};

enum class ProvAction : uint8_t {
    None,
    SwitchToSoftAp,    // stop BLE provisioning, start SoftAP provisioning
    AbandonToStation,  // stop provisioning, clear the request, use stored credentials
    RetryAfterFailure, // reset the provisioning state machine so the user can try again
    Finish,            // credentials work: stop provisioning, clear the request
};

const char *toString(ProvAction a);

struct BootPlan {
    bool provision = false;
    ProvTransport transport = ProvTransport::None;
};

class ProvisioningFsm {
public:
    explicit ProvisioningFsm(ProvisioningTimeouts t = {}) : timeouts_(t) {}

    // What to do at boot. `requested` is the transport stored by a
    // re-provision request (None when there is none).
    static BootPlan plan(bool provisioned, ProvTransport requested, bool bleAvailable);

    void begin(ProvTransport transport, bool provisioned, MonoMs now);
    // A phone or browser opened a session: timeouts no longer switch modes.
    void onSessionStarted() { session_ = true; }
    void onSessionEnded() { session_ = false; }
    ProvAction onCredentialsFailed();
    ProvAction onSuccess();
    ProvAction tick(MonoMs now);

    bool active() const { return active_; }
    ProvTransport transport() const { return transport_; }
    uint32_t failures() const { return failures_; }

private:
    ProvisioningTimeouts timeouts_;
    ProvTransport transport_ = ProvTransport::None;
    bool provisioned_ = false;
    bool active_ = false;
    bool session_ = false;
    MonoMs since_ = 0;
    uint32_t failures_ = 0;
};

// ---- names, setup code, QR payload ---------------------------------------------

// "zoneflow-ab12" from the last two bytes of the station MAC.
std::string makeHostname(const std::array<uint8_t, 6> &mac);
// "ZoneFlow-AB12": BLE device name and SoftAP SSID.
std::string makeServiceName(const std::array<uint8_t, 6> &mac);

// Setup code: 10 characters from a 32-symbol alphabet without 0/O/1/I
// (50 bits). Used as the Security 2 password (PoP), the SoftAP WPA2
// passphrase and the first-access proof for setting the admin password.
inline constexpr std::size_t kSetupCodeLen = 10;
inline constexpr std::string_view kSetupCodeAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
// `random` must hold at least 10 bytes; each contributes 5 bits.
std::string makeSetupCode(const uint8_t *random, std::size_t len);
bool validSetupCode(std::string_view code);

// Security 2 user name. The Espressif provisioning apps read it from the QR
// payload; "wifiprov" is the name their documentation uses.
inline constexpr std::string_view kProvUsername = "wifiprov";

// QR payload in the format the Espressif provisioning apps scan
// (ESP-IDF example wifi_prov_mgr): {"ver":"v1","name":...,"username":...,
// "pop":...,"transport":"ble"|"softap"}.
std::string qrPayload(std::string_view serviceName, std::string_view setupCode, ProvTransport transport);

// ---- captive portal DNS -------------------------------------------------------

// Builds the answer to a DNS query: every A (or ANY) question gets `ipv4`
// (host byte order), other types get an empty NOERROR answer. Returns the
// response length, or 0 to drop the packet (not a standard query, malformed,
// or `outCap` too small).
std::size_t captiveDnsAnswer(const uint8_t *query, std::size_t len, uint32_t ipv4, uint8_t *out, std::size_t outCap);

} // namespace zf::net
