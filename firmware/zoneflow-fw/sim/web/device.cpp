// SPDX-License-Identifier: Apache-2.0
// The ZoneFlow firmware as a WebAssembly module, for the browser demo.
//
// What runs here is the firmware's own code: the controller core (zf_core)
// on the simulated rev A output stage of the host tests, the display logic
// and LVGL view (zf_display), and the REST API with its authentication
// (zf_web: api, auth, routes, json_map). This file is the glue that the
// device has in web.cpp, app_main.cpp and zf_platform: it turns a request
// from the page into an ApiRequest, runs the event stream, records the
// history, and plays the hardware (valve currents, flow meter, faults).
//
// Not here, and so not shown by the demo: the ESP32 drivers, WiFi and BLE,
// TLS, MQTT, OTA, and real timing. See sim/web/README.md.
//
// The page calls the zf_* functions below. Strings go in and out as UTF-8;
// a returned pointer is valid until the next call of the same function.
#include <emscripten/emscripten.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cJSON.h"
#include "controls.hpp"
#include "sim.hpp"
#include "zf/history/history.hpp"
#include "zf_web/api.hpp"
#include "zf_web/auth.hpp"
#include "zf_web/json_map.hpp"

namespace {

using namespace zf;

// ---- the simulated installation ------------------------------------------------------------------

// What the demo's garden draws when a valve is open. Invented values of a
// plausible size; the firmware learns the currents and measures the flow.
constexpr std::array<int32_t, 9> kValveMa{255, 262, 238, 248, 240, 231, 250, 250, 180};
constexpr std::array<float, kMaxZones> kZoneLpm{14.0f, 16.0f, 4.5f, 7.0f, 5.5f, 3.0f, 8.0f, 8.0f};
// A shorted valve wire: far above the comparator's trip level (4.0 A peak,
// about 2.8 A rms; hardware design section 9).
constexpr int32_t kShortMa = 6000;
constexpr int32_t kTripMa = 2800;
// A failed-short output device: current with no valve asked for (rule 6).
constexpr int32_t kFailedTriacMa = 250;

constexpr const char *kDemoPassword = "zoneflow";
constexpr const char *kSetupCode = "K7Q2M9XW4R";
constexpr const char *kDeviceIp = "192.168.1.47";

class EntropyRandom final : public web::IRandom {
public:
    void fill(uint8_t *out, std::size_t len) override
    {
        while (len > 0) {
            const std::size_t n = len < 256 ? len : 256;
            if (getentropy(out, n) != 0) {
                std::abort();
            }
            out += n;
            len -= n;
        }
    }
};

struct Device;

// The platform facts the API asks for. On the device this is web.cpp's
// Platform, backed by zf_net, zf_ota and zf_mqtt.
class DemoPlatform final : public web::IPlatformPort {
public:
    explicit DemoPlatform(Device &d) : d_(d) {}
    web::FirmwareInfo firmware() const override
    {
        return {ZF_DEMO_VERSION, "zoneflow-fw", "browser simulator", __DATE__ " " __TIME__,
                "zoneflow-rev-a (simulated)"};
    }
    web::SystemInfo system() const override;
    net::NetStatus network() const override;
    std::string setupCode() const override { return kSetupCode; }
    std::vector<web::WifiNetwork> scan() override { return {{"Garden-2G", -58, true}}; }
    bool provisionWifi(std::string_view, std::string_view) override { return false; }
    ota::OtaStatus otaStatus() const override { return {}; }
    web::SlotInfo slots() const override { return {"ota_0", "ota_1", "valid", ""}; }
    // There is no flash to write and no network to download from.
    ota::OtaError startPull(std::string_view, bool) override { return ota::OtaError::Download; }
    std::string tlsFingerprint() const override { return ""; }
    void sleepMs(uint32_t ms) override;
    const HistoryLog *history() const override;

private:
    Device &d_;
};

struct Stream {
    int id = 0; // 0 = free
    std::string cookie;
    std::string authorization;
};

struct Device {
    test::UiHarness h;
    sim::Controls controls;
    test::MemoryStorage webStore; // NVS namespace "zf_web" on the device
    EntropyRandom rng;
    web::AuthService auth{webStore, rng};
    DemoPlatform platform{*this};
    std::unique_ptr<AppServices> services;
    std::unique_ptr<web::Api> api;
    HistoryLog history{h.storage};

    // Injected conditions.
    std::array<bool, kMaxZones> wireCut{};
    std::array<bool, kMaxZones> wireShort{};
    bool failedTriac = false;
    float leakLpm = 0.0f;

    // Flow meter.
    double flowPulses = 0.0;
    float flowLpm = 0.0f;
    float pulsesPerLitre = 450.0f;
    uint32_t configRevision = 0;

    // Events on their way to the history and to the event stream.
    std::deque<Event> historyQueue;
    std::deque<Event> streamQueue;
    std::deque<Event> pageQueue; // for the page's own account of what happens
    std::array<Stream, 3> streams{};
    int nextStreamId = 1;
    uint32_t messageId = 0;
    uint32_t stateSeq = 0;
    double lastState = 0;
    double lastSend = 0;
    double lastAuthCheck = 0;

    uint32_t sleepDebtMs = 0;
    MonoMs bootMono = 0;
    uint64_t steps = 0;

    bool anyStream() const
    {
        for (const Stream &s : streams) {
            if (s.id != 0) {
                return true;
            }
        }
        return false;
    }

    void applyFaults()
    {
        test::FakeBoard &b = *h.board;
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            b.valveMa[z] = wireShort[z] ? kShortMa : wireCut[z] ? 0 : kValveMa[z];
        }
        b.valveMa[8] = kValveMa[8];
        b.shortedOutputMa = failedTriac ? kFailedTriacMa : 0;
    }

    // Water moves through a zone when its triac conducts, the relay feeds
    // the valve common, 24 VAC is there and the wire is whole.
    bool waterOn(std::size_t z) const
    {
        const test::FakeBoard &b = *h.board;
        return b.acPresent && b.relayClosed() && b.valveDriven(z) && !wireCut[z] && !wireShort[z];
    }

    void step()
    {
        controls.sample(h);
        h.step();
        ++steps;

        test::FakeBoard &b = *h.board;
        // The hardware comparator: removes gate drive and latches, with no
        // firmware involved.
        if (b.currentMa() >= kTripMa) {
            b.trip();
        }
        h.sensors.sample.overCurrentTripped = b.latched;

        if (const uint32_t rev = h.config.revision(); rev != configRevision) {
            configRevision = rev;
            pulsesPerLitre = h.config.get().flow.pulsesPerLitre;
        }
        float lpm = leakLpm;
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            lpm += waterOn(z) ? kZoneLpm[z] : 0.0f;
        }
        flowLpm = lpm;
        flowPulses += static_cast<double>(lpm) / 60.0 * 0.05 * static_cast<double>(pulsesPerLitre);
        h.sensors.sample.flowPulses = static_cast<uint32_t>(static_cast<uint64_t>(flowPulses));

        // The history recorder task (zf_platform/history_recorder.cpp).
        while (!historyQueue.empty()) {
            const Event &e = historyQueue.front();
            if (const auto r = historyFromEvent(e)) {
                history.append(*r, h.clock.mono, e.type == EventType::FaultRaised);
            }
            historyQueue.pop_front();
        }
        if (history.flushDue(h.clock.mono)) {
            history.flush(h.clock.mono);
        }
    }

    static void onEvent(const Event &e, void *ctx)
    {
        auto *self = static_cast<Device *>(ctx);
        if (e.type != EventType::ZoneChanged && e.type != EventType::MasterChanged &&
            e.type != EventType::QueueChanged && e.type != EventType::CommandRejected) {
            self->historyQueue.push_back(e);
        }
        if (self->anyStream() && self->streamQueue.size() < 64) {
            self->streamQueue.push_back(e);
        }
        if (self->pageQueue.size() < 256) {
            self->pageQueue.push_back(e);
        }
    }
};

web::SystemInfo DemoPlatform::system() const
{
    web::SystemInfo s;
    s.uptimeMs = d_.h.clock.mono - d_.bootMono;
    s.resetReason = "poweron";
    return s;
}

net::NetStatus DemoPlatform::network() const
{
    const display::NetworkStatus n = d_.h.net.status();
    net::NetStatus s;
    s.provisioned = n.link != display::WifiLink::NotProvisioned;
    s.hostname.assign("zoneflow-2b3c");
    s.mac.assign("84:F7:03:1A:2B:3C");
    if (n.link == display::WifiLink::Connected) {
        s.state = net::NetState::Connected;
        s.ssid.assign(n.ssid.c_str());
        s.bssid.assign("3c:84:6a:10:20:30");
        s.channel = 6;
        s.rssi = n.rssiDbm;
        s.ip.assign(n.ip.c_str());
        s.netmask.assign("255.255.255.0");
        s.gateway.assign("192.168.1.1");
        s.mdnsRunning = true;
        s.connectedSinceMs = d_.bootMono;
    } else {
        s.state = n.link == display::WifiLink::Disconnected ? net::NetState::WaitingRetry : net::NetState::Off;
    }
    return s;
}

// The API task sleeps while it waits for the control task to accept a run.
// Here there is one thread, so sleeping means: let the control loop run.
void DemoPlatform::sleepMs(uint32_t ms)
{
    d_.sleepDebtMs += ms;
    while (d_.sleepDebtMs >= 50) {
        d_.sleepDebtMs -= 50;
        d_.step();
    }
}

const HistoryLog *DemoPlatform::history() const { return &d_.history; }

std::unique_ptr<Device> gDevice;
std::unique_ptr<sim::Screen> gScreen;
std::array<uint8_t, sim::kWidth * sim::kHeight * 4> gRgba{};
uint32_t gShownSerial = 0;
bool gDrawn = false;

// ---- small JSON helpers ----------------------------------------------------------------------------

const char *text(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

double number(const cJSON *o, const char *key, double fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}

bool flag(const cJSON *o, const char *key, bool fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : fallback;
}

std::string toHex(const std::string &bytes)
{
    static const char *d = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out.push_back(d[b >> 4]);
        out.push_back(d[b & 15]);
    }
    return out;
}

int nibble(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

std::string fromHex(const char *hex)
{
    std::string out;
    for (std::size_t i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return {};
        }
        out.push_back(static_cast<char>(hi << 4 | lo));
    }
    return out;
}

void restoreStore(test::MemoryStorage &store, const cJSON *object)
{
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, object)
    {
        if (cJSON_IsString(item) && item->string != nullptr) {
            store.data[item->string] = fromHex(item->valuestring);
        }
    }
}

cJSON *dumpStore(const test::MemoryStorage &store)
{
    cJSON *o = cJSON_CreateObject();
    for (const auto &[key, value] : store.data) {
        cJSON_AddStringToObject(o, key.c_str(), toHex(value).c_str());
    }
    return o;
}

const char *keep(std::string &slot, cJSON *json)
{
    web::JsonPtr holder(json);
    slot = web::printJson(json);
    return slot.c_str();
}

// "id: n\nevent: state\ndata: {...}\n\n", as web.cpp's stateMessage().
std::string stateMessage(Device &d, uint32_t id)
{
    const SystemState s = d.h.state.snapshot();
    const auto cfg = std::make_unique<Config>(d.h.config.get());
    web::JsonPtr o(web::stateToJson(s, *cfg));
    cJSON_AddItemToObject(o.get(), "network", web::netStatusToJson(d.platform.network()));
    cJSON_AddItemToObject(o.get(), "ota", web::otaStatusToJson(d.platform.otaStatus()));
    return web::sseMessage("state", id, web::printJson(o.get()));
}

} // namespace

extern "C" {

// Starts the device, or restarts it. options (JSON):
//   utc        seconds; the clock at power-up
//   tz         POSIX TZ string, used when nothing is stored
//   storage    what zf_storage() returned before the restart, or absent for
//              a device that has never been set up
//   fresh      true: out of the box (first-boot wizard, no admin password,
//              default configuration). Otherwise the demo garden.
// Returns 1 when the device runs.
EMSCRIPTEN_KEEPALIVE int zf_boot(const char *optionsJson)
{
    web::JsonPtr options(cJSON_Parse(optionsJson != nullptr ? optionsJson : "{}"));
    if (!options) {
        return 0;
    }
    if (!gScreen) {
        gScreen = std::make_unique<sim::Screen>();
    }
    setHistoryLog(nullptr);
    gDevice.reset();
    gDevice = std::make_unique<Device>();
    Device &d = *gDevice;
    test::UiHarness &h = d.h;

    const cJSON *storage = cJSON_GetObjectItemCaseSensitive(options.get(), "storage");
    const bool stored = cJSON_IsObject(storage);
    const bool fresh = flag(options.get(), "fresh", false);
    bool wizardDone = !fresh;
    Config cfg = fresh ? Config::defaults() : test::demoConfig();
    if (const char *tz = text(options.get(), "tz"); tz[0] != '\0') {
        cfg.timezone.assign(tz);
    }
    if (stored) {
        restoreStore(h.storage, cJSON_GetObjectItemCaseSensitive(storage, "nvs"));
        restoreStore(d.webStore, cJSON_GetObjectItemCaseSensitive(storage, "web"));
        wizardDone = flag(storage, "wizard_done", wizardDone);
        // An erased or damaged configuration loads as the defaults, as on
        // the device.
        h.config.load();
        cfg = h.config.get();
    }

    h.useRevABoard();
    d.applyFaults();
    h.net.resetsDevice = true; // a factory reset from the display erases everything
    h.clock.utc = static_cast<UtcSeconds>(number(options.get(), "utc", 0));
    h.system.graphics = std::to_string(LVGL_VERSION_MAJOR) + "." + std::to_string(LVGL_VERSION_MINOR) + "." +
                        std::to_string(LVGL_VERSION_PATCH);
    h.net.connect("Garden-2G", kDeviceIp);
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        h.diag.learned[z] = z < cfg.zoneCount ? static_cast<uint16_t>(kValveMa[z]) : 0;
    }

    d.history.load();
    setHistoryLog(&d.history);
    {
        HistoryRecord boot;
        boot.utc = h.clock.utc;
        boot.kind = HistoryKind::Boot;
        boot.a = static_cast<uint8_t>(stored ? HistoryResetReason::Software : HistoryResetReason::PowerOn);
        d.history.append(boot, h.clock.mono, true);
    }
    h.start(cfg, wizardDone, true);
    h.time.onNetworkSync();
    d.bootMono = h.clock.mono;
    d.configRevision = 0;
    h.bus.subscribe(&Device::onEvent, &d);

    d.auth.load();
    if (!stored && !fresh) {
        // What provisioning does on the device: the first password.
        d.auth.setInitialPassword(kDemoPassword);
        // The demo garden is a controller in use: the web interface's
        // first-run steps (time zone, zones) are behind it.
        web::WebSettings settings;
        settings.setupComplete = true;
        d.webStore.write(web::Api::kSettingsKey, settings.encode());
    }
    d.services = std::make_unique<AppServices>(AppServices{h.bus, h.state, h.commands, h.config, h.time});
    d.api = std::make_unique<web::Api>(*d.services, d.auth, d.platform, d.webStore, h.clock);
    d.api->begin();
    d.stateSeq = h.state.sequence();
    gDrawn = false;
    return 1;
}

// Runs the device for `ticks` control periods of 50 ms.
EMSCRIPTEN_KEEPALIVE void zf_step(int ticks)
{
    if (!gDevice) {
        return;
    }
    for (int i = 0; i < ticks; ++i) {
        gDevice->step();
    }
}

// The display, 320x240 RGBA. Returns null when the picture has not changed
// since the last call.
EMSCRIPTEN_KEEPALIVE const uint8_t *zf_frame()
{
    if (!gDevice || !gScreen) {
        return nullptr;
    }
    test::UiHarness &h = gDevice->h;
    if (gDrawn && h.ui->frameSerial() == gShownSerial) {
        return nullptr;
    }
    gDrawn = true;
    gShownSerial = h.ui->frameSerial();
    sim::Screen::tickMs = static_cast<uint32_t>(h.clock.mono);
    gScreen->render(h.frame());
    const uint16_t *src = gScreen->pixels();
    for (std::size_t i = 0; i < static_cast<std::size_t>(sim::kWidth * sim::kHeight); ++i) {
        const uint16_t p = src[i];
        const unsigned r5 = (p >> 11) & 0x1F;
        const unsigned g6 = (p >> 5) & 0x3F;
        const unsigned b5 = p & 0x1F;
        gRgba[i * 4] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        gRgba[i * 4 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        gRgba[i * 4 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
        gRgba[i * 4 + 3] = 0xFF;
    }
    return gRgba.data();
}

// Controls. detents: clockwise positive. key: 0 encoder push, 1 Back, 2 Manual/Home.
EMSCRIPTEN_KEEPALIVE void zf_turn(int detents)
{
    if (gDevice) {
        gDevice->controls.encoderCount += sim::Controls::kDetent * detents;
    }
}

EMSCRIPTEN_KEEPALIVE void zf_key(int key, int down)
{
    if (!gDevice) {
        return;
    }
    sim::Controls &c = gDevice->controls;
    (key == 0 ? c.selectDown : key == 1 ? c.backDown : c.homeDown) = down != 0;
}

// Conditions outside the controller. what: "rain", "ac", "wifi", "leak"
// (value: litres per minute x 10), "triac" (an output device failed short),
// "cut" and "short" (the wire of zone `index`, 0-based).
EMSCRIPTEN_KEEPALIVE void zf_set(const char *what, int index, int value)
{
    if (!gDevice) {
        return;
    }
    Device &d = *gDevice;
    const std::string_view w = what;
    const bool on = value != 0;
    const bool zoneOk = index >= 0 && index < static_cast<int>(kMaxZones);
    if (w == "rain") {
        d.h.sensors.sample.rainContactClosed = on;
    } else if (w == "ac") {
        d.h.board->acPresent = on;
    } else if (w == "wifi") {
        if (on) {
            d.h.net.connect("Garden-2G", kDeviceIp);
        } else {
            d.h.net.net.link = display::WifiLink::Disconnected;
            for (Stream &s : d.streams) {
                s = Stream{};
            }
        }
    } else if (w == "leak") {
        d.leakLpm = static_cast<float>(value) / 10.0f;
    } else if (w == "triac") {
        d.failedTriac = on;
    } else if (w == "cut" && zoneOk) {
        d.wireCut[static_cast<std::size_t>(index)] = on;
    } else if (w == "short" && zoneOk) {
        d.wireShort[static_cast<std::size_t>(index)] = on;
    }
    d.applyFaults();
}

// What a person standing at the controller and in the garden would see.
EMSCRIPTEN_KEEPALIVE const char *zf_world()
{
    static std::string out;
    if (!gDevice) {
        return "{}";
    }
    Device &d = *gDevice;
    test::UiHarness &h = d.h;
    const test::FakeBoard &b = *h.board;
    const auto cfg = std::make_unique<Config>(h.config.get());
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "utc", static_cast<double>(h.clock.utc));
    cJSON_AddNumberToObject(o, "mono", static_cast<double>(h.clock.mono));
    cJSON_AddNumberToObject(o, "backlight", h.ui->backlightPercent());
    cJSON_AddNumberToObject(o, "brightness", h.ui->settings().brightnessPercent);
    cJSON_AddBoolToObject(o, "ac", b.acPresent);
    cJSON_AddBoolToObject(o, "rain", h.sensors.sample.rainContactClosed);
    cJSON_AddBoolToObject(o, "wifi", h.net.net.link == display::WifiLink::Connected);
    cJSON_AddBoolToObject(o, "relay", b.relayClosed());
    cJSON_AddBoolToObject(o, "drive", b.driveEnabled());
    cJSON_AddBoolToObject(o, "tripped", b.latched);
    cJSON_AddBoolToObject(o, "triac", d.failedTriac);
    cJSON_AddNumberToObject(o, "current_ma", b.currentMa());
    cJSON_AddNumberToObject(o, "flow_lpm", static_cast<double>(std::round(d.flowLpm * 10.0f) / 10.0f));
    cJSON_AddNumberToObject(o, "leak_lpm", static_cast<double>(d.leakLpm));
    cJSON_AddNumberToObject(o, "litres",
                            std::floor(d.flowPulses / static_cast<double>(d.pulsesPerLitre) * 10.0) / 10.0);
    cJSON_AddNumberToObject(o, "zone_count", cfg->zoneCount);
    cJSON_AddBoolToObject(o, "master_enabled", cfg->master.enabled);
    cJSON_AddBoolToObject(o, "master", b.valveDriven(8));
    cJSON_AddBoolToObject(o, "password_set", d.auth.passwordSet());
    cJSON *zones = cJSON_AddArrayToObject(o, "zones");
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        cJSON *zone = cJSON_CreateObject();
        cJSON_AddStringToObject(zone, "name", cfg->zones[z].name.c_str());
        cJSON_AddBoolToObject(zone, "enabled", z < cfg->zoneCount && cfg->zones[z].enabled);
        cJSON_AddBoolToObject(zone, "led", b.valveDriven(z)); // the output LED sits in the opto drive path
        cJSON_AddBoolToObject(zone, "water", d.waterOn(z));
        cJSON_AddBoolToObject(zone, "cut", d.wireCut[z]);
        cJSON_AddBoolToObject(zone, "short", d.wireShort[z]);
        cJSON_AddNumberToObject(zone, "lpm", static_cast<double>(kZoneLpm[z]));
        cJSON_AddItemToArray(zones, zone);
    }
    cJSON *programs = cJSON_AddObjectToObject(o, "programs");
    for (std::size_t i = 0; i < cfg->schedule.programCount; ++i) {
        const Program &p = cfg->schedule.programs[i];
        cJSON_AddStringToObject(programs, std::to_string(p.id).c_str(), p.name.c_str());
    }
    // The firmware asked for a restart, or for a factory reset.
    cJSON_AddStringToObject(o, "restart", h.net.forgets > 0 ? "factory" : h.system.restarts > 0 ? "reboot" : "");
    return keep(out, o);
}

// What happened since the last call, for the page's log: the firmware's
// events (the JSON of the event stream) and the state of the output stage.
// This does not go through the simulated network, so it works with WiFi off.
EMSCRIPTEN_KEEPALIVE const char *zf_narrate()
{
    static std::string out;
    if (!gDevice) {
        return "{}";
    }
    Device &d = *gDevice;
    cJSON *o = cJSON_CreateObject();
    cJSON *events = cJSON_AddArrayToObject(o, "events");
    while (!d.pageQueue.empty()) {
        cJSON_AddItemToArray(events, web::eventToJson(d.pageQueue.front()));
        d.pageQueue.pop_front();
    }
    const SystemState s = d.h.state.snapshot();
    cJSON_AddItemToObject(o, "hardware", web::hardwareToJson(s.hardware));
    cJSON_AddStringToObject(o, "queue", toString(s.queue.state));
    return keep(out, o);
}

// Everything the device keeps in flash, to carry over a restart or a reload
// of the page. serial changes when there is something new to save.
EMSCRIPTEN_KEEPALIVE int zf_storage_serial()
{
    return gDevice ? gDevice->h.storage.writes + gDevice->webStore.writes + gDevice->h.settings.saves : 0;
}

EMSCRIPTEN_KEEPALIVE const char *zf_storage()
{
    static std::string out;
    if (!gDevice) {
        return "{}";
    }
    Device &d = *gDevice;
    d.history.flush(d.h.clock.mono);
    display::UiSettings ui;
    d.h.settings.load(ui);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "nvs", dumpStore(d.h.storage));
    cJSON_AddItemToObject(o, "web", dumpStore(d.webStore));
    cJSON_AddBoolToObject(o, "wizard_done", ui.wizardDone);
    return keep(out, o);
}

// One HTTP request to /api/v1. request (JSON): method, path, query, body,
// content_type, cookie, authorization, csrf, origin, host. The answer
// (JSON): status, body, content_type, headers [[name, value]], action
// ("", "reboot", "factory_reset", "reprovision").
EMSCRIPTEN_KEEPALIVE const char *zf_request(const char *requestJson)
{
    static std::string out;
    if (!gDevice) {
        return "{\"status\":503}";
    }
    Device &d = *gDevice;
    web::JsonPtr in(cJSON_Parse(requestJson));
    if (!in) {
        return "{\"status\":400}";
    }
    const std::string body = text(in.get(), "body");
    web::ApiRequest req;
    req.method = text(in.get(), "method");
    req.path = text(in.get(), "path");
    req.query = text(in.get(), "query");
    req.body = body;
    req.contentLength = body.size();
    req.host = text(in.get(), "host");
    req.origin = text(in.get(), "origin");
    req.cookie = text(in.get(), "cookie");
    req.authorization = text(in.get(), "authorization");
    req.csrf = text(in.get(), "csrf");
    req.contentType = text(in.get(), "content_type");
    req.peer = "192.168.1.50";

    web::ApiResponse res;
    const web::Admission adm = d.api->admit(req);
    if (adm.ok && adm.match.route->body == web::Body::Binary) {
        res = web::Api::error(503, "not_in_simulator",
                              "The browser simulator has no flash memory to write, so uploads are switched off.");
    } else if (adm.ok && adm.match.route->response == web::Response::EventStream) {
        res = web::Api::error(400, "use_stream", "open the stream with zf_stream_open");
    } else {
        res = d.api->handle(req);
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "status", res.status);
    cJSON_AddStringToObject(o, "body", res.body.c_str());
    cJSON_AddStringToObject(o, "content_type", res.contentType.c_str());
    cJSON *headers = cJSON_AddArrayToObject(o, "headers");
    for (const auto &[name, value] : res.headers) {
        cJSON *pair = cJSON_CreateArray();
        cJSON_AddItemToArray(pair, cJSON_CreateString(name.c_str()));
        cJSON_AddItemToArray(pair, cJSON_CreateString(value.c_str()));
        cJSON_AddItemToArray(headers, pair);
    }
    const char *action = res.action == web::PostAction::Reboot         ? "reboot"
                         : res.action == web::PostAction::FactoryReset ? "factory_reset"
                         : res.action == web::PostAction::Reprovision  ? "reprovision"
                                                                       : "";
    cJSON_AddStringToObject(o, "action", action);
    return keep(out, o);
}

// GET /api/v1/events. request as for zf_request. The answer (JSON): status;
// on 200 also id (for zf_stream_close) and first, the text the device sends
// at once; otherwise body, the error document.
EMSCRIPTEN_KEEPALIVE const char *zf_stream_open(const char *requestJson)
{
    static std::string out;
    if (!gDevice) {
        return "{\"status\":503}";
    }
    Device &d = *gDevice;
    web::JsonPtr in(cJSON_Parse(requestJson));
    if (!in) {
        return "{\"status\":400}";
    }
    web::ApiRequest req;
    req.method = "GET";
    req.path = "/api/v1/events";
    req.host = text(in.get(), "host");
    req.cookie = text(in.get(), "cookie");
    req.authorization = text(in.get(), "authorization");
    req.peer = "192.168.1.50";
    const web::Admission adm = d.api->admit(req);
    cJSON *o = cJSON_CreateObject();
    Stream *slot = nullptr;
    for (Stream &s : d.streams) {
        if (s.id == 0 && slot == nullptr) {
            slot = &s;
        }
    }
    if (!adm.ok) {
        cJSON_AddNumberToObject(o, "status", adm.error.status);
        cJSON_AddStringToObject(o, "body", adm.error.body.c_str());
    } else if (slot == nullptr) {
        const web::ApiResponse busy = web::Api::error(503, "too_many_streams", "at most 3 event streams");
        cJSON_AddNumberToObject(o, "status", busy.status);
        cJSON_AddStringToObject(o, "body", busy.body.c_str());
    } else {
        *slot = Stream{d.nextStreamId++, std::string(req.cookie), std::string(req.authorization)};
        cJSON_AddNumberToObject(o, "status", 200);
        cJSON_AddNumberToObject(o, "id", slot->id);
        cJSON_AddStringToObject(o, "first", ("retry: 3000\n\n" + stateMessage(d, 0)).c_str());
    }
    return keep(out, o);
}

EMSCRIPTEN_KEEPALIVE void zf_stream_close(int id)
{
    if (!gDevice) {
        return;
    }
    for (Stream &s : gDevice->streams) {
        if (s.id == id) {
            s = Stream{};
        }
    }
}

// The stream task of web.cpp, run by the page a few times a second. nowMs
// is the page's clock: the pacing of the stream is in real time, as the
// browser of a real device would see it. The answer (JSON): text, to send
// to every open stream; closed, ids of streams the device has ended.
EMSCRIPTEN_KEEPALIVE const char *zf_stream_poll(double nowMs)
{
    static std::string out;
    if (!gDevice) {
        return "{}";
    }
    Device &d = *gDevice;
    std::string send;
    cJSON *o = cJSON_CreateObject();
    cJSON *closed = cJSON_AddArrayToObject(o, "closed");
    if (!d.anyStream()) {
        d.stateSeq = d.h.state.sequence();
        d.streamQueue.clear();
    } else {
        if (nowMs - d.lastAuthCheck >= 1000) {
            d.lastAuthCheck = nowMs;
            for (Stream &s : d.streams) {
                if (s.id != 0 && !d.api->streamMayContinue(s.cookie, s.authorization)) {
                    cJSON_AddItemToArray(closed, cJSON_CreateNumber(s.id));
                    s = Stream{};
                }
            }
        }
        while (!d.streamQueue.empty()) {
            web::JsonPtr e(web::eventToJson(d.streamQueue.front()));
            d.streamQueue.pop_front();
            send += web::sseMessage("event", ++d.messageId, web::printJson(e.get()));
            d.lastSend = nowMs;
        }
        const uint32_t seq = d.h.state.sequence();
        if ((seq != d.stateSeq && nowMs - d.lastState >= 500) || nowMs - d.lastState >= 30000) {
            d.stateSeq = seq;
            d.lastState = nowMs;
            d.lastSend = nowMs;
            send += stateMessage(d, ++d.messageId);
        }
        if (nowMs - d.lastSend >= 15000) {
            send += ": keep-alive\n\n";
            d.lastSend = nowMs;
        }
    }
    cJSON_AddStringToObject(o, "text", send.c_str());
    return keep(out, o);
}

} // extern "C"

// The module has no main loop of its own: the page drives it.
int main() { return 0; }
