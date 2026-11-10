// SPDX-License-Identifier: Apache-2.0
// Stand-ins for the ports in ports.hpp. The firmware uses them until the
// owning module provides the real implementation; the simulator and the
// host tests use them too.
#pragma once

#include "zf_display/ports.hpp"

namespace zf::display {

// No network module: the display shows no WiFi icon and says WiFi setup is
// not in this build.
class StubNetworkPort final : public INetworkPort {
public:
    NetworkStatus status() const override { return {}; }
    bool startProvisioning(char *out, std::size_t outSize) override
    {
        if (out != nullptr && outSize > 0) {
            out[0] = '\0';
        }
        return false;
    }
    void stopProvisioning() override {}
    bool factoryReset() override { return false; }
};

// The core does not publish learned currents yet: "Not measured".
class StubZoneDiagPort final : public IZoneDiagPort {
public:
    uint16_t learnedCurrentMa(ZoneIndex /*zone*/) const override { return 0; }
};

// Settings that live only until power-off.
class MemorySettingsStore final : public ISettingsStore {
public:
    bool load(UiSettings &out) override
    {
        if (!stored_) {
            return false;
        }
        out = settings_;
        return true;
    }
    bool save(const UiSettings &settings) override
    {
        settings_ = settings;
        stored_ = true;
        ++saves;
        return true;
    }
    bool erase() override
    {
        stored_ = false;
        settings_ = UiSettings{};
        return true;
    }
    int saves = 0;

private:
    bool stored_ = false;
    UiSettings settings_{};
};

} // namespace zf::display
