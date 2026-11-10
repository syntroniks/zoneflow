// SPDX-License-Identifier: Apache-2.0
// Narrow interfaces the display needs from outside zf::AppServices.
//
// AppServices (zf/app/services.hpp) covers zone state, the run queue,
// sensors, faults, configuration and the clock. It does not yet cover the
// items below. Each one has a stub (stubs.hpp) so the display builds and runs
// without the owning module. The owner replaces the stub with the real thing
// and passes it to zf::display::start().
//
//  INetworkPort   zf_net       WiFi link state and BLE provisioning payload
//  IZoneDiagPort  zf_core      per-zone learned current (CurrentMonitor::observedMa)
//  ISystemPort    main / zf_ota  version strings, restart
//  ISettingsStore zf_display   the display's own settings (NVS on the device)
//
// No ESP-IDF or LVGL types here: the simulator and the host tests use the
// same header.
#pragma once

#include <cstddef>
#include <cstdint>

#include "zf/fixed_string.hpp"
#include "zf/types.hpp"

namespace zf::display {

// ---- network (owner: zf_net) --------------------------------------------------

enum class WifiLink : uint8_t {
    Unavailable,    // no network module in this build
    NotProvisioned, // no credentials stored
    Provisioning,   // BLE provisioning service is advertising
    Connecting,
    Connected,
    Disconnected, // credentials stored, link down, retrying
};

struct NetworkStatus {
    WifiLink link = WifiLink::Unavailable;
    FixedString<32> ssid;
    FixedString<15> ip; // dotted IPv4
    FixedString<17> mac;
    FixedString<31> hostname;
    int8_t rssiDbm = 0; // valid when Connected
};

class INetworkPort {
public:
    virtual ~INetworkPort() = default;
    // Any task. Must not block: return a cached copy.
    virtual NetworkStatus status() const = 0;
    // Makes sure BLE provisioning (ESP-IDF unified provisioning, Security 2)
    // is running and writes the text for the QR code that the Espressif
    // provisioning app scans, for example
    //   {"ver":"v1","name":"ZoneFlow-AB12","username":"wifiprov","pop":"abcd1234","transport":"ble"}
    // Returns false if provisioning is not running and cannot be started
    // from here; out is then empty.
    virtual bool startProvisioning(char *out, std::size_t outSize) = 0;
    // The user left the QR screen. May be a no-op.
    virtual void stopProvisioning() = 0;
    // Factory reset, asked for and confirmed on the display.
    // Returns true if the network module takes the whole job: it stops
    // watering, erases all stored data and restarts the device by itself
    // (zf_net::factoryReset does). Returns false if there is nothing of that
    // kind; the display then erases the configuration and its own settings
    // and restarts.
    virtual bool factoryReset() = 0;
};

// ---- zone diagnostics (owner: zf_core) -----------------------------------------

class IZoneDiagPort {
public:
    virtual ~IZoneDiagPort() = default;
    // Mean current seen with only this zone on, in mA. 0 = never observed.
    // The core has it as CurrentMonitor::observedMa(); SystemState does not
    // publish it yet.
    virtual uint16_t learnedCurrentMa(ZoneIndex zone) const = 0;
};

// ---- system (owner: main, zf_ota) ------------------------------------------------

class ISystemPort {
public:
    virtual ~ISystemPort() = default;
    virtual const char *firmwareVersion() const = 0;
    virtual const char *idfVersion() const = 0;
    virtual const char *boardName() const = 0;
    virtual const char *graphicsVersion() const = 0; // LVGL
    // Restarts the device. On the device this does not return.
    virtual void restart() = 0;
};

// ---- the display's own settings ----------------------------------------------------

struct UiSettings {
    static constexpr uint32_t kVersion = 1;
    uint8_t brightnessPercent = 80; // 10..100
    uint16_t dimAfterSec = 30;      // 0 = never dim
    uint16_t offAfterSec = 300;     // 0 = never off; counted from the last input
    bool wizardDone = false;
    uint8_t lastManualZone = 0;
    uint16_t lastManualMinutes = 10;
};

class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;
    // Returns false if nothing is stored; out keeps its defaults.
    virtual bool load(UiSettings &out) = 0;
    virtual bool save(const UiSettings &settings) = 0;
    virtual bool erase() = 0;
};

struct Ports {
    INetworkPort &net;
    IZoneDiagPort &diag;
    ISystemPort &system;
    ISettingsStore &settings;
};

} // namespace zf::display
