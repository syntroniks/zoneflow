// SPDX-License-Identifier: Apache-2.0
// Connects the display's network port (zf_display/ports.hpp) to zf_net.
// It lives in main because zf_display and zf_net do not depend on each
// other.
//
// Compiled and linked; never run on hardware.
#pragma once

#include <cstdio>

#include "zf_display/ports.hpp"
#include "zf_net/net.hpp"

namespace zf {

class DisplayNetworkPort final : public display::INetworkPort {
public:
    // The display calls this on every redraw: one mutex-guarded struct copy.
    display::NetworkStatus status() const override
    {
        const net::NetStatus n = net::status();
        display::NetworkStatus out;
        switch (n.state) {
        case net::NetState::Off:
            out.link = display::WifiLink::NotProvisioned;
            break;
        case net::NetState::Provisioning:
            out.link = display::WifiLink::Provisioning;
            break;
        case net::NetState::Connecting:
            out.link = display::WifiLink::Connecting;
            break;
        case net::NetState::Connected:
            out.link = display::WifiLink::Connected;
            break;
        case net::NetState::WaitingRetry:
            out.link = display::WifiLink::Disconnected;
            break;
        }
        out.ssid.assign(n.ssid.view());
        out.ip.assign(n.ip.view());
        out.mac.assign(n.mac.view());
        out.hostname.assign(n.hostname.view());
        out.rssiDbm = n.rssi;
        if (n.state == net::NetState::Provisioning && n.provisioning == net::ProvTransport::SoftAp) {
            out.setupNetwork.assign(net::setupInfo().serviceName);
        }
        return out;
    }

    // zf_net starts BLE provisioning by itself on a device with no
    // credentials, which is when the setup wizard asks. On a provisioned
    // device it starts with a restart, which the display asks for first
    // (restartIntoProvisioning). This returns the QR payload while BLE
    // provisioning runs, and false otherwise.
    bool startProvisioning(char *out, std::size_t outSize) override
    {
        if (outSize > 0) {
            out[0] = '\0';
        }
        const net::NetStatus n = net::status();
        if (n.state != net::NetState::Provisioning || n.provisioning != net::ProvTransport::Ble) {
            return false;
        }
        const net::SetupInfo info = net::setupInfo();
        if (info.qrPayload.empty() || info.qrPayload.size() >= outSize) {
            return false;
        }
        std::snprintf(out, outSize, "%s", info.qrPayload.c_str());
        return true;
    }

    // Provisioning ends by itself (success, or zf_net's own timeout).
    void stopProvisioning() override {}

    // Setup, Network, Set up WiFi, after "Restart into WiFi setup?" was
    // answered Restart. zf_net stores the request and restarts; the boot
    // after it provisions over BLE (or SoftAP in a build without BT).
    bool restartIntoProvisioning() override { return net::requestProvisioning(net::ProvTransport::Ble); }

    // Setup, Network, Show setup code, after a 3 s hold of the encoder
    // switch. The code is in NVS because the display needs it (firmware
    // security 4.1).
    bool setupCode(char *out, std::size_t outSize) override
    {
        if (outSize > 0) {
            out[0] = '\0';
        }
        const net::SetupInfo info = net::setupInfo();
        if (info.setupCode.empty() || info.setupCode.size() >= outSize) {
            return false;
        }
        std::snprintf(out, outSize, "%s", info.setupCode.c_str());
        return true;
    }

    // zf_net stops watering, erases NVS except the device identity and
    // restarts.
    bool factoryReset() override { return net::factoryReset(); }
};

} // namespace zf
