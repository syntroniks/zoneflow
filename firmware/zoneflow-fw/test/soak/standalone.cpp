// SPDX-License-Identifier: Apache-2.0
// The standalone firmware in the soak simulation: the controller core, the
// output stage, the config store, persistence, the history log, the time
// service, the REST API and the MQTT command path, wired as app_main.cpp
// and the platform tasks wire them, on the world of world.hpp.
#include <cJSON.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <memory>
#include <queue>
#include <set>

#include "alloc_count.hpp"
#include "physical_oracle.hpp"
#include "scenario.hpp"
#include "soak.hpp"
#include "world.hpp"
#include "zf/app/controller.hpp"
#include "zf/app/persist.hpp"
#include "zf/app/services.hpp"
#include "zf/config/config_json.hpp"
#include "zf/history/history.hpp"
#include "zf/hw/reference_store.hpp"
#include "zf_mqtt/mqtt_logic.hpp"
#include "zf_web/api.hpp"
#include "zf_web/auth.hpp"
#include "zf_web/json_map.hpp"

namespace zf::soak {
namespace {

constexpr MonoMs kBootMonoMs = 300; // esp_timer at app_main
constexpr const char *kPassword = "soak password 1";
constexpr const char *kHost = "192.168.1.47";

// ---- the firmware's view of time -------------------------------------------------------------

class DevClock final : public IClock {
public:
    DevClock(World &w, uint64_t bootPhys) : w_(w), boot_(bootPhys) {}
    UtcSeconds nowUtc() const override { return w_.sys.now(); }
    MonoMs monoMs() const override { return w_.phys - boot_ + kBootMonoMs; }

private:
    World &w_;
    uint64_t boot_;
};

// A fixed queue of events, as EventQueueSubscriber copies them on the device.
class EventRing {
public:
    explicit EventRing(std::size_t cap) : buf_(cap) {}
    void push(const Event &e)
    {
        if (n_ == buf_.size()) {
            ++dropped_;
            return;
        }
        buf_[(head_ + n_) % buf_.size()] = e;
        ++n_;
    }
    bool pop(Event &e)
    {
        if (n_ == 0) {
            return false;
        }
        e = buf_[head_];
        head_ = (head_ + 1) % buf_.size();
        --n_;
        return true;
    }
    uint64_t dropped() const { return dropped_; }

private:
    std::vector<Event> buf_;
    std::size_t head_ = 0;
    std::size_t n_ = 0;
    uint64_t dropped_ = 0;
};

class FixedRandom final : public web::IRandom {
public:
    explicit FixedRandom(uint64_t seed) : rng_(seed) {}
    void fill(uint8_t *out, std::size_t len) override
    {
        for (std::size_t i = 0; i < len; ++i) {
            out[i] = static_cast<uint8_t>(rng_.next());
        }
    }

private:
    Rng rng_;
};

struct Device;

class SoakPlatform final : public web::IPlatformPort {
public:
    explicit SoakPlatform(Device &d) : d_(d) {}
    web::FirmwareInfo firmware() const override
    {
        return {"soak", "zoneflow-fw", "host", "", "zoneflow-rev-a (simulated)"};
    }
    web::SystemInfo system() const override { return {}; }
    net::NetStatus network() const override
    {
        net::NetStatus s;
        s.state = net::NetState::Connected;
        s.provisioned = true;
        s.ip.assign(kHost);
        s.hostname.assign("zoneflow-2b3c");
        return s;
    }
    std::string setupCode() const override { return "K7Q2M9XW4R"; }
    std::vector<web::WifiNetwork> scan() override { return {}; }
    bool provisionWifi(std::string_view, std::string_view) override { return false; }
    ota::OtaStatus otaStatus() const override { return {}; }
    web::SlotInfo slots() const override { return {"ota_0", "ota_1", "valid", ""}; }
    ota::OtaError startPull(std::string_view, bool) override { return ota::OtaError::Download; }
    std::string tlsFingerprint() const override { return ""; }
    void sleepMs(uint32_t ms) override;
    const HistoryLog *history() const override;

private:
    Device &d_;
};

class Runner;

class WorldSensors final : public ISensorHal {
public:
    explicit WorldSensors(Device &d) : d_(d) {}
    SensorSample read() override;

private:
    Device &d_;
};

// AC_ZC as the zone driver's zero-cross switching reads it (zf_platform's
// ZeroCrossInput): crossings every half mains period while 24 VAC is there.
class WorldZeroCross final : public IZeroCrossHal {
public:
    explicit WorldZeroCross(Device &d) : d_(d) {}
    ZeroCrossSample sample() const override;
    // The device busy-waits on the control task; simulated time does not
    // move inside a tick. Z1: never longer than half a period plus the
    // detector offset (zero_cross.hpp).
    void delayUs(uint32_t us) override;

private:
    Device &d_;
};

// One boot of the firmware.
struct Device {
    Device(Runner &r, World &w, uint64_t bootPhys);

    Runner &runner;
    World &w;
    uint64_t bootPhys;
    DevClock clock;
    OutputEnableGuard guard{OutputEnableMode::Toggle, 500};
    ObservedBus bus;
    hw::BlobReferenceStore reference;
    hw::OutputStage stage;
    WorldSensors sensors{*this};
    WorldZeroCross zeroCross{*this};
    EventBus events;
    StateStore state;
    CommandQueue commands;
    ConfigStore config;
    TimeService time;
    std::unique_ptr<Controller> ctrl;
    HistoryLog history;
    EventRing historyQueue{32}; // zf_history's queue
    EventRing oracleQueue{4096};
    FixedRandom random;
    web::AuthService auth;
    SoakPlatform platform{*this};
    std::unique_ptr<AppServices> services;
    std::unique_ptr<web::Api> api;
    std::string token;
    mqtt::RateLimiter limiter;
    mqtt::PendingEdits edits;
    // zf_mqtt's inbox: the broker callback queues, the MQTT task drains it
    // every kLoopMs (250 ms). Full: the message is dropped.
    struct Inbound {
        uint64_t seq = 0;
        uint64_t atPhys = 0;
        std::string suffix;
        std::string payload;
        bool retained = false;
    };
    static constexpr std::size_t kInboxDepth = 8; // zf_mqtt/src/mqtt.cpp
    std::deque<Inbound> inbox;
    uint64_t inboxSeq = 0;
    uint64_t inboxHandled = 0;
    uint32_t zcWaitTooLongUs = 0;
    uint64_t servicedPhys = 0;    // resumed after a pause of the service tasks (the harness skips them while hung)
    uint64_t lastServicePhys = 0; // the last pass
    MonoMs lastConfigWrite = 0;
    uint32_t pulseBase = 0;
    uint32_t windowSeq = 0;
    CurrentWindowStats lastWindow{};
    ConfigStore::LoadResult loaded;
    bool hung = false;
    uint32_t sleepDebtMs = 0;
    bool inApi = false;

    MonoMs mono() const { return clock.monoMs(); }
};

SensorSample WorldSensors::read()
{
    World &w = d_.w;
    SensorSample s;
    s.rainContactClosed = w.rainContact;
    s.spareContactClosed = w.spareContact;
    s.acPresent = w.transformer && w.volts > 0.4;
    s.flowPulses = static_cast<uint32_t>(static_cast<uint64_t>(w.flowPulses)) - d_.pulseBase;
    s.valveCurrentMa = -1;
    s.currentWindow = d_.lastWindow;
    s.currentSenseAvailable = w.adcOk;
    s.overCurrentTripped = w.board.latched;
    s.mainsMilliHz = w.zeroCrossBridged ? 100000 : w.mainsMilliHz;
    s.sensorSupplyOn = true;
    s.keepAlivePinPlain = w.kaPinPlain;
    s.dieTempKnown = true;
    s.dieTempC = w.dieTempC;
    return s;
}

const HistoryLog *SoakPlatform::history() const { return &d_.history; }

ZeroCrossSample WorldZeroCross::sample() const
{
    World &w = d_.w;
    ZeroCrossSample s;
    s.present = w.transformer && w.volts > 0.4 && !w.zeroCrossBridged && w.mainsMilliHz != 0;
    s.nowUs = static_cast<uint32_t>(d_.mono() * 1000u);
    s.intervalUs = w.mainsMilliHz != 0 ? static_cast<uint32_t>(500'000'000ull / w.mainsMilliHz) : 0;
    s.lastEdgeUs = s.intervalUs != 0 ? s.nowUs - s.nowUs % s.intervalUs : 0;
    return s;
}

void WorldZeroCross::delayUs(uint32_t us)
{
    World &w = d_.w;
    w.count("zc.waits");
    w.count("zc.wait_us", us);
    if (us > kZeroCrossMaxIntervalUs) {
        d_.zcWaitTooLongUs = us; // reported after the tick
    }
}

// ---- the schedule oracle (S8) ------------------------------------------------------------------
//
// Independent of the firmware's TimeZone and DayRule code: local time comes
// from glibc (localtime_r with the POSIX TZ string), day rules are written
// again here from the configuration documentation.

class LocalTime {
public:
    void use(const std::string &tz)
    {
        if (tz != tz_) {
            setenv("TZ", tz.c_str(), 1);
            tzset();
            tz_ = tz;
        }
    }
    // Local (day serial, minute of day).
    void at(UtcSeconds t, int64_t &day, int &minute) const
    {
        const time_t tt = static_cast<time_t>(t);
        struct tm tm{};
        localtime_r(&tt, &tm);
        day = daysFromCivil(tm.tm_year + 1900, static_cast<unsigned>(tm.tm_mon + 1), static_cast<unsigned>(tm.tm_mday));
        minute = tm.tm_hour * 60 + tm.tm_min;
    }
    int64_t key(UtcSeconds t) const
    {
        int64_t d = 0;
        int m = 0;
        at(t, d, m);
        return d * 1440 + m;
    }

private:
    std::string tz_;
};

bool dayMatches(const DayRule &r, int64_t day)
{
    const civil::Date date = civil::civilFromDays(day);
    switch (r.type) {
    case DayRuleType::Weekdays: {
        const auto wd = static_cast<unsigned>(((day % 7) + 7 + 4) % 7); // 1970-01-01 was a Thursday
        return (r.weekdayMask >> wd & 1u) != 0;
    }
    case DayRuleType::Interval: {
        if (r.intervalDays == 0) {
            return false;
        }
        const int64_t d = day - r.intervalAnchorDay;
        return ((d % r.intervalDays) + r.intervalDays) % r.intervalDays == 0;
    }
    case DayRuleType::OddDays:
        return date.day % 2 == 1;
    case DayRuleType::EvenDays:
        return date.day % 2 == 0;
    }
    return false;
}

std::string keyText(int64_t key);

struct Due {
    uint16_t pid = 0;
    int64_t key = 0;
    UtcSeconds at = 0;
    uint64_t foundPhys = 0;
    uint64_t deadlinePhys = 0;
    bool catchup = false;
    // The clock went back more than a day after this start was consumed: the
    // scheduler may consume it again on its replayed day (kLargeBackJumpSec).
    bool replay = false;
    // The clock was put back by less than a day over this start: the firmware
    // reports it missed when the clock passes it again (policy 8).
    bool replaySmall = false;
    // A valve opened for this start: a second run of it waters twice.
    bool watered = false;
    enum class St : uint8_t { Pending, Started, Skipped, Missed, Excluded } st = St::Pending;
};

// What a started program should do, for the liveness check.
struct Expected {
    uint16_t pid = 0;
    UtcSeconds at = 0;
    uint64_t deadlinePhys = 0;
    std::map<ZoneIndex, uint32_t> zones; // zone -> seconds
    std::set<ZoneIndex> done;
};

// ---- the run ---------------------------------------------------------------------------------------

class Runner {
public:
    explicit Runner(const Options &o);
    Result run();

    World w;
    Options opt;
    Result res;
    std::unique_ptr<Device> dev;
    PhysicalOracle phys;
    Scenario scenario;

    // Firmware state the oracles need across boots.
    enum class Down : uint8_t { No, Hung, Frozen, Off };
    Down down = Down::Off;
    uint64_t lastBeatPhys = 0;
    bool frozenPinHigh = false;
    uint64_t bootAtPhys = 0; // when the next boot happens while Off
    std::string pendingBootReason = "power_on";
    bool pendingPowerLoss = true;
    uint64_t quietSince = 0;
    uint32_t boots = 0;

    // S8
    LocalTime local;
    std::string tz = "UTC0";
    Config applied{};
    bool haveApplied = false;
    std::map<uint16_t, int64_t> progKey; // last consumed (local key), the truth across boots
    std::map<std::pair<uint16_t, int64_t>, std::size_t> dueIndex;
    std::deque<Due> dues;
    std::size_t dueBase = 0; // index of dues.front()
    bool cursorValid = false;
    UtcSeconds cursorUtc = 0;
    uint64_t tzChangedPhys = 0;
    bool tzChanged = false;
    std::set<uint16_t> storedKeysAtBoot; // programs with a key in the runtime record at boot
    std::set<uint16_t> keysBeforeEval;   // programs with a key before the first valid evaluation
    std::map<uint16_t, uint64_t> programChangedPhys;
    std::deque<Expected> expected;
    std::map<uint16_t, uint64_t> lastLiveStart;

    // Misc invariants.
    uint64_t allocInTick = 0;
    uint64_t maxLiveBytesDay2 = 0;
    uint64_t maxLiveBytesLate = 0;
    uint64_t nextHeapSample = 0;
    uint64_t lastApiStatusPhys = 0;
    bool bootChecked = true;
    uint64_t bootDisturbedPhys = 0;

    mutable FirmwareView lastView_{};
    std::map<uint16_t, UtcSeconds> progAt; // the instant of progKey
    std::map<uint16_t, int64_t> aheadKey;  // as Scheduler::aheadKeys_
    std::map<uint16_t, UtcSeconds> aheadAt;
    std::map<uint16_t, int64_t> lastStartedDue; // program -> key of its latest scheduled start
    UtcSeconds aheadCursor = 0;
    bool aheadValid = false;
    bool configSeenThisBoot = false;
    uint64_t smallBackJumpPhys = 0;
    bool runtimeDamagedAtBoot = false;
    uint64_t supplyBadPhys = 0; // VIN_MON last out of range (S9)
    std::map<std::pair<uint16_t, int64_t>, uint32_t> dueBoot;

    void violation(const char *id, const std::string &detail);
    void enumerate(const Program &p, UtcSeconds from, UtcSeconds to, bool catchup, bool noKey);
    UtcSeconds instantOf(int64_t key) const;
    Due *findDue(uint16_t pid, int64_t key);
    int64_t firmwareKey(uint16_t pid) const;

    // The scenario's actions.
    void setupScenario();
    void displayCommand();
    void restCall();
    void mqttRandom();
    void flood();
    void configEdit();
    void manualRun();
    void haltScript();
    void powerCut();
    void syncTime();
    bool wifi = true;
    bool armedRestartAfterStart = false;
    // World loop.
    void stepIsr();
    void controlTick();
    void serviceTasks();
    bool quiescent() const;
    bool idleNow() const;
    void advance(uint64_t ms, bool events);
    void sleep(uint32_t ms);

    // Firmware lifecycle.
    void boot();
    void shutdown(const std::string &why, bool powerLoss, bool tearRetained);
    void halt(bool freeze, bool pinHigh, uint64_t forMs);

    // Interfaces for the scenario.
    web::ApiResponse api(const std::string &method, const std::string &path, const std::string &body = {},
                         bool authed = true);
    void ensureSession();
    void mqttMessage(const std::string &suffix, const std::string &payload, bool retained = false);
    void mqttHandle(const Device::Inbound &in);
    void mqttDrain();
    bool up() const { return dev != nullptr && down == Down::No; }

    // Oracles.
    FirmwareView view() const;
    void afterTick();
    void onEvent(const Event &e);
    void scheduleTick();
    void addDue(const Program &p, int64_t key, UtcSeconds at, bool catchup);
    void configApplied();
    void checkDeadlines();
    void finish();
};

void SoakPlatform::sleepMs(uint32_t ms) { d_.runner.sleep(ms); }

Device::Device(Runner &r, World &world, uint64_t boot)
    : runner(r), w(world), bootPhys(boot), clock(world, boot), bus(world), reference(world.nvs),
      stage(bus, guard, clock, test::revALayout(), hw::OutputStageConfig{}, &reference), config(world.nvs),
      time(world.sys, world.rtc.answers ? static_cast<IRtc *>(&world.rtc) : nullptr), history(world.nvs),
      random(world.rng.next()), auth(world.webNvs, random, 2)
{
    pulseBase = static_cast<uint32_t>(static_cast<uint64_t>(world.flowPulses));
}

Runner::Runner(const Options &o) : w(o.seed, o.mode), opt(o), scenario(w, o)
{
    res.seed = o.seed;
    res.mode = o.mode;
    w.tracing = o.trace;
    phys.violation = [this](const char *id, const std::string &d) { violation(id, d); };
    w.onControlTaskWrite = [this](const char *key) {
        violation("S11-flash-in-tick", std::string("control task writes flash (") + key + ") with a valve energised");
    };
}

void Runner::violation(const char *id, const std::string &detail)
{
    HarnessScope harness;
    ++res.violationCount;
    w.count(std::string("violation.") + id);
    w.note(std::string("VIOLATION ") + id + ": " + detail);
    if (res.violations.size() < 20) {
        res.violations.push_back({id, w.phys, detail});
        if (res.violations.size() == 1) {
            res.firstEvents = w.traceTail;
        }
    }
}

// ---- lifecycle ------------------------------------------------------------------------------------

void Runner::boot()
{
    ++boots;
    w.count("boots");
    dev = std::make_unique<Device>(*this, w, w.phys);
    Device &d = *dev;
    d.bus.transientRate = scenario.busTransientRate;
    d.bus.violation = [this](const std::string &id, const std::string &det) { violation(id.c_str(), det); };
    d.bus.keepAliveStopped = [this] { return dev && dev->guard.request() == KeepAliveRequest::Off; };
    w.board.attach(d.guard);
    w.board.cpuHalted = false;
    d.loaded = d.config.load();
    if (d.loaded.status == ConfigStore::LoadStatus::InvalidDefaults) {
        w.count("boot.config_invalid");
    }
    d.time.boot();
    // Runtime record as stored: which programs have a key (S8, policy 5).
    storedKeysAtBoot.clear();
    // Either copy damaged beyond NVS's guarantee: the firmware's own check
    // rejects it (gap 9, closed), and when the newest copy may be the
    // damaged one the catch-up reports instead of running.
    runtimeDamagedAtBoot = w.nvs.damaged.count(ConfigStore::kRuntimeKey) != 0 || w.nvs.damaged.count(kRuntimeKeyB) != 0;
    {
        SchedulerPersist p;
        uint32_t seq = 0;
        const RecordLoad load = loadRuntime(w.nvs, p, seq);
        w.count(std::string("boot.runtime_") + toString(load));
        if (load == RecordLoad::Certain || load == RecordLoad::Uncertain) {
            for (std::size_t i = 0; i < p.count && i < p.fired.size(); ++i) {
                storedKeysAtBoot.insert(p.fired[i].programId);
                if (opt.trace) {
                    w.note("  stored key of program " + std::to_string(p.fired[i].programId) + ": " +
                           keyText(p.fired[i].key));
                }
            }
        } else {
            w.count("boot.runtime_missing");
        }
    }
    RetainedRecord rr;
    const bool budgetKept = w.rtcMem.load(rr);
    if (!budgetKept) {
        // The RTC copy is gone; the firmware takes the copy in flash
        // (zf/app/budget.hpp). S4 holds across it.
        phys.budgetLost();
        w.count("boot.retained_lost");
    }
    d.ctrl = std::make_unique<Controller>(
        ControllerDeps{d.clock, d.stage, d.sensors, d.guard, d.events, d.state, d.config, &w.nvs, &w.rtcMem});
    d.ctrl->configureCurrentSense(CurrentSenseConfig{});
    d.ctrl->attachZeroCross(&d.zeroCross, 0); // as app_main.cpp
    d.events.subscribe([](const Event &e, void *ctx) { static_cast<EventRing *>(ctx)->push(e); }, &d.oracleQueue);
    d.history.load();
    setHistoryLog(&d.history);
    {
        HistoryRecord b;
        b.utc = d.clock.nowUtc();
        b.kind = HistoryKind::Boot;
        d.history.append(b, d.mono(), true);
    }
    d.ctrl->begin();
    // After begin(), as app_main: the history recorder's subscription.
    constexpr EventMask kHistoryEvents = kAllEvents & ~eventBit(EventType::ZoneChanged) &
                                         ~eventBit(EventType::MasterChanged) & ~eventBit(EventType::QueueChanged) &
                                         ~eventBit(EventType::CommandRejected);
    d.events.subscribe([](const Event &e, void *ctx) { static_cast<EventRing *>(ctx)->push(e); }, &d.historyQueue,
                       kHistoryEvents);
    if (d.loaded.status == ConfigStore::LoadStatus::InvalidDefaults) {
        d.ctrl->reportFault(FaultCode::ConfigInvalid);
    }
    d.ctrl->setTimeStatus(d.time.status());
    d.services = std::make_unique<AppServices>(AppServices{d.events, d.state, d.commands, d.config, d.time});
    d.api = std::make_unique<web::Api>(*d.services, d.auth, d.platform, w.webNvs, d.clock);
    d.api->begin();

    // S9: what booted is a valid configuration.
    const Config inEffect = d.config.get();
    if (!validate(inEffect).ok) {
        violation("S9-config-invalid", "the configuration in effect after boot fails validation");
    }
    down = Down::No;
    lastBeatPhys = w.phys;
    cursorValid = false;
    configSeenThisBoot = false;
    bootChecked = false;
    bootDisturbedPhys = 0;
    w.note("boot " + std::to_string(boots) + " (" + pendingBootReason + "), config " +
           ConfigStore::toString(d.loaded.status) + ", time " + toString(d.time.status()) +
           (budgetKept ? "" : ", run budget lost"));
    // Drain begin()'s events into the oracle.
    Event e;
    while (d.oracleQueue.pop(e)) {
        onEvent(e);
    }
}

void Runner::shutdown(const std::string &why, bool powerLoss, bool tearRetained)
{
    if (dev) {
        // Shutdown, panic and brownout handlers drive the keep-alive low.
        dev->guard.trip();
        setHistoryLog(nullptr);
        dev.reset();
    }
    w.board.attach(w.resetGuard);
    w.board.cpuHalted = false;
    w.nvs.interrupt();
    w.webNvs.interrupt();
    w.flashBusyUntil = 0;
    if (powerLoss) {
        w.rtcMem.powerLoss(w.rng);
        w.sys.powerLoss();
    } else if (tearRetained) {
        w.rtcMem.tear(w.rng);
        w.count("retained.torn");
        w.note("retained record torn by the reset");
    }
    down = Down::Off;
    w.note("firmware down: " + why + " (last tick " + std::to_string(w.phys - lastBeatPhys) + " ms ago, flash busy " +
           std::to_string(w.flashBusyUntil > w.phys ? w.flashBusyUntil - w.phys : 0) + " ms)");
    w.count("shutdown." + why);
}

void Runner::halt(bool freeze, bool pinHigh, uint64_t forMs)
{
    if (!up()) {
        return;
    }
    if (freeze) {
        down = Down::Frozen;
        // The pin keeps its level; the scenario asks for high or low by
        // waiting for the level it wants.
        frozenPinHigh = w.board.kaHigh();
        (void)pinHigh;
        w.board.cpuHalted = true;
        w.note(std::string("processor frozen, keep-alive pin ") + (frozenPinHigh ? "high" : "low"));
        w.count(frozenPinHigh ? "halt.freeze_high" : "halt.freeze_low");
    } else {
        down = Down::Hung;
        w.note("control task hung");
        w.count("halt.hang");
    }
    // The task watchdog (5 s) panics a hung task; a frozen processor comes
    // back by the interrupt watchdog or a power cycle.
    const uint64_t at = w.phys + (freeze ? forMs : 5000);
    scenario.at(at, "fw.halt_end", [this, freeze] {
        if (down == Down::Hung || down == Down::Frozen) {
            shutdown(freeze ? "watchdog_reset" : "task_watchdog", false, w.rng.chance(0.05));
            pendingBootReason = freeze ? "watchdog" : "panic";
            bootAtPhys = w.phys + 300;
        }
    });
}

// ---- the API and MQTT as a client sees them ----------------------------------------------------------

web::ApiResponse Runner::api(const std::string &method, const std::string &path, const std::string &body, bool authed)
{
    web::ApiResponse none;
    none.status = 0;
    if (!up() || dev->inApi) {
        return none;
    }
    Device &d = *dev;
    if (authed && d.token.empty()) {
        ensureSession();
    }
    web::ApiRequest r;
    r.method = method;
    const std::size_t q = path.find('?');
    r.path = std::string_view(path).substr(0, q);
    r.query = q == std::string::npos ? std::string_view{} : std::string_view(path).substr(q + 1);
    r.body = body;
    r.contentLength = body.size();
    r.contentType = body.empty() ? std::string_view{} : std::string_view("application/json");
    r.host = kHost;
    r.peer = "192.168.1.50";
    const std::string bearer = "Bearer " + d.token;
    if (authed) {
        r.authorization = bearer;
    }
    d.inApi = true;
    web::ApiResponse resp = d.api->handle(r);
    if (dev) {
        dev->inApi = false;
    }
    w.count("api.calls");
    w.count("api.status." + std::to_string(resp.status));
    return resp;
}

void Runner::ensureSession()
{
    Device &d = *dev;
    const std::string pw = std::string("{\"password\":\"") + kPassword + "\"}";
    web::ApiResponse r = api("POST", "/api/v1/auth/login", pw, false);
    if (r.status != 200) {
        r = api("POST", "/api/v1/auth/setup",
                std::string("{\"setup_code\":\"K7Q2M9XW4R\",\"password\":\"") + kPassword + "\"}", false);
    }
    if (!dev) {
        return;
    }
    web::JsonPtr o = web::parseJson(r.body);
    const cJSON *t = o ? cJSON_GetObjectItem(o.get(), "token") : nullptr;
    if (cJSON_IsString(t)) {
        d.token = t->valuestring;
    }
}

void Runner::mqttMessage(const std::string &suffix, const std::string &payload, bool retained)
{
    if (!up()) {
        return;
    }
    // The broker callback: into the inbox, or dropped when it is full.
    Device &d = *dev;
    w.count("mqtt.messages");
    if (d.inbox.size() >= Device::kInboxDepth) {
        w.count("mqtt.inbox_full");
        return;
    }
    d.inbox.push_back({++d.inboxSeq, w.phys, suffix, payload, retained});
}

void Runner::mqttDrain()
{
    // The MQTT task, every kLoopMs: M1, every queued message handled once,
    // in order, within two loop periods, and the inbox never above its depth.
    Device &d = *dev;
    if (d.inbox.size() > Device::kInboxDepth) {
        violation("M1-mqtt-inbox", "inbox holds " + std::to_string(d.inbox.size()) + " messages");
    }
    while (!d.inbox.empty()) {
        const Device::Inbound in = d.inbox.front();
        d.inbox.pop_front();
        const uint64_t waited = w.phys - std::max(in.atPhys, d.servicedPhys);
        if (in.seq != d.inboxHandled + 1 || waited > 600) {
            violation("M1-mqtt-inbox", "message " + std::to_string(in.seq) + " handled after " +
                                           std::to_string(d.inboxHandled) + ", " + std::to_string(waited) +
                                           " ms after it arrived");
        }
        d.inboxHandled = in.seq;
        mqttHandle(in);
        w.count("mqtt.handled");
    }
}

void Runner::mqttHandle(const Device::Inbound &in)
{
    Device &d = *dev;
    const std::string &suffix = in.suffix;
    const std::string &payload = in.payload;
    const bool retained = in.retained;
    // zf_mqtt's handleMessage(), without the broker.
    const Config cfg = d.config.get();
    const mqtt::Topics topics(mqtt::rootTopic(cfg.mqtt, "a1b2c3d4e5f6"));
    if (!d.limiter.allow(d.mono())) {
        w.count("mqtt.rate_limited");
        return;
    }
    const bool timeValid = d.time.status() != TimeStatus::Unset;
    mqtt::ParsedCommand pc =
        mqtt::parseCommand(topics, topics.command(suffix), payload, retained, cfg, d.clock.nowUtc(), timeValid);
    switch (pc.action) {
    case mqtt::Action::None:
        w.count("mqtt.invalid");
        return;
    case mqtt::Action::StopZone: {
        const SystemState s = d.state.snapshot();
        if (!mqtt::resolveStopZone(pc.zone, s, pc.command).empty()) {
            return;
        }
        break;
    }
    case mqtt::Action::SetRunTime:
        d.edits.setRunTime(pc.zone, pc.value);
        return;
    case mqtt::Action::SetProgramEnabled:
        d.edits.setProgramEnabled(pc.programId, pc.flag);
        return;
    case mqtt::Action::Post:
        break;
    }
    if (!d.commands.post(pc.command)) {
        w.count("mqtt.busy");
    }
}

// ---- the loop ----------------------------------------------------------------------------------------

FirmwareView Runner::view() const
{
    FirmwareView v;
    v.running = up() && !dev->hung;
    v.halted = down == Down::Hung || down == Down::Frozen || (down == Down::Off && w.phys - lastBeatPhys < 4000);
    v.frozen = down == Down::Frozen;
    v.frozenPinHigh = frozenPinHigh;
    v.off = down == Down::Off;
    v.lastBeatMs = lastBeatPhys;
    FirmwareView &last = lastView_; // configuration kept while the firmware is down
    if (dev && dev->ctrl) {
        const ZoneDriver &drv = dev->ctrl->driver();
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            v.commanded[z] = drv.state(static_cast<ZoneIndex>(z)) == ValveState::Open;
            v.maxRunMs[z] = static_cast<uint64_t>(drv.effectiveMaxRunSec(static_cast<ZoneIndex>(z))) * 1000u;
        }
        v.commanded[kMasterOut] = drv.masterOn();
        v.maxConcurrent = drv.config().maxConcurrent;
        v.minRestMs = static_cast<uint64_t>(drv.config().minRestSec) * 1000u;
        v.currentUsable = dev->stage.status().currentLost == CurrentLoss::None;
        last.maxRunMs = v.maxRunMs;
        last.minRestMs = v.minRestMs;
        last.maxConcurrent = v.maxConcurrent;
    } else {
        v.maxRunMs = last.maxRunMs;
        v.minRestMs = last.minRestMs;
        v.maxConcurrent = last.maxConcurrent;
        for (auto &m : v.maxRunMs) {
            if (m == 0) {
                m = 3600000;
            }
        }
    }
    return v;
}

void Runner::stepIsr()
{
    w.phys += kIsrMs;
    w.boardClock.mono = w.phys;
    w.board.step5ms();
    w.physics();
    phys.step(w, view());
}

void Runner::controlTick()
{
    Device &d = *dev;
    Command cmd;
    for (int i = 0; i < 8 && d.commands.pop(cmd); ++i) {
        d.ctrl->handle(cmd);
    }
    d.ctrl->setTimeStatus(d.time.status());
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t a0 = tickAllocCount();
    if (!cursorValid) {
        // Which programs the scheduler has a key for before its first
        // evaluation with a valid clock (policy 5: none, no catch-up).
        keysBeforeEval.clear();
        const SchedulerPersist p = d.ctrl->scheduler().persist();
        for (std::size_t i = 0; i < p.count; ++i) {
            keysBeforeEval.insert(p.fired[i].programId);
        }
    }
    gInTick = true;
    d.ctrl->tick();
    gInTick = false;
    const uint64_t allocs = tickAllocCount() - a0;
    const auto ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    ++res.ticks;
    res.maxTickNs = std::max(res.maxTickNs, ns);
    if (opt.maxTickUs != 0 && ns / 1000 > opt.maxTickUs) {
        violation("S11-tick-time", "control tick took " + std::to_string(ns / 1000) + " us");
    }
    if (allocs != 0) {
        if (allocInTick == 0) {
            violation("S11-alloc-in-tick", std::to_string(allocs) + " heap allocations in one Controller::tick()");
        }
        allocInTick += allocs;
    }
    lastBeatPhys = w.phys;
    afterTick();
}

void Runner::serviceTasks()
{
    Device &d = *dev;
    const MonoMs m = d.mono();
    if (w.phys - d.lastServicePhys > 2 * kTickMs) {
        d.servicedPhys = w.phys; // the tasks resume: queued messages wait from here
    }
    // zf_isense: a 100 ms window.
    if (m % kWindowMs == 0) {
        d.lastWindow = w.takeWindow(++d.windowSeq, m - kWindowMs, m);
    }
    // zf_persist, every 250 ms.
    if (m % 250 == 0) {
        persistWriteOnce(d.ctrl->persistMailbox(), w.nvs);
    }
    // zf_mqtt's loop, every 250 ms.
    if (m % 250 == 0) {
        mqttDrain();
    }
    d.lastServicePhys = w.phys;
    // zf_history: on event, at least every 500 ms.
    Event e;
    while (d.historyQueue.pop(e)) {
        if (const auto r = historyFromEvent(e)) {
            d.history.append(*r, m, e.type == EventType::FaultRaised);
        }
    }
    if (d.history.flushDue(m)) {
        d.history.flush(m);
    }
    // zf_mqtt: coalesced configuration edits.
    if (m % 250 == 0 && !d.edits.empty() && m - d.lastConfigWrite >= mqtt::PendingEdits::kConfigWriteIntervalMs) {
        d.lastConfigWrite = m;
        auto cfg = std::make_unique<Config>(d.config.get());
        if (d.edits.applyTo(*cfg) && d.config.update(*cfg).ok) {
            d.commands.post(Command::simple(CommandType::ReloadConfig));
        }
    }
}

bool Runner::quiescent() const { return idleNow() && w.phys - quietSince >= 3000; }

bool Runner::idleNow() const
{
    if (!up() || dev->hung) {
        return false;
    }
    const Device &d = *dev;
    const OutputStageStatus st = d.stage.status();
    const ZoneDriver &drv = d.ctrl->driver();
    return (st.phase == StagePhase::Idle || st.phase == StagePhase::NoAc || st.phase == StagePhase::Fault) &&
           d.guard.request() == KeepAliveRequest::Off && !w.board.a.on && !w.board.b.on && !w.board.latched &&
           w.energisedCount() == 0 && drv.activeCount() == 0 && !drv.masterOn() && !drv.masterPending() &&
           d.ctrl->scheduler().queueSize() == 0 && d.edits.empty() && d.inbox.empty() && w.phys >= w.flashBusyUntil;
}

void Runner::advance(uint64_t ms, bool events)
{
    const uint64_t end = w.phys + ms;
    while (w.phys < end) {
        if (events) {
            scenario.runDue(w.phys);
            if (down == Down::Off && w.phys >= bootAtPhys && bootAtPhys != 0) {
                bootAtPhys = 0;
                boot();
            }
        }
        // Coarse steps while nothing can drive a valve: the scheduler still
        // evaluates every second.
        const bool coarse = events && quiescent() && down != Down::Off;
        if (coarse) {
            const uint64_t next = std::min({end, scenario.nextAt(), w.phys + 1000});
            uint64_t step = (next - w.phys) / kTickMs * kTickMs;
            if (step >= kTickMs) {
                w.phys += step;
                w.boardClock.mono = w.phys;
                w.board.step5ms(static_cast<uint32_t>(step));
                w.physics(step);
                phys.step(w, view(), step);
                Device &d = *dev;
                d.lastWindow = w.takeWindow(++d.windowSeq, d.mono() - kWindowMs, d.mono());
                controlTick();
                if (dev) {
                    Device &dd = *dev;
                    persistWriteOnce(dd.ctrl->persistMailbox(), w.nvs);
                    Event e;
                    while (dd.historyQueue.pop(e)) {
                        if (const auto r = historyFromEvent(e)) {
                            dd.history.append(*r, dd.mono(), e.type == EventType::FaultRaised);
                        }
                    }
                    if (dd.history.flushDue(dd.mono())) {
                        dd.history.flush(dd.mono());
                    }
                }
                w.count("loop.coarse_steps");
                continue;
            }
        }
        if (down == Down::Off && events) {
            // Nothing runs: step the board in big strides until the next
            // event or boot, once the detectors have let go.
            const bool settled = !w.board.a.on && !w.board.b.on && w.energisedCount() == 0 && !w.board.kaHigh();
            if (settled) {
                uint64_t next = std::min(end, scenario.nextAt());
                if (bootAtPhys != 0) {
                    next = std::min(next, bootAtPhys);
                }
                uint64_t step = (next - w.phys) / kIsrMs * kIsrMs;
                step = std::min<uint64_t>(step, 60000);
                if (step > kIsrMs) {
                    w.phys += step;
                    w.boardClock.mono = w.phys;
                    w.board.step5ms(static_cast<uint32_t>(step));
                    w.physics(step);
                    phys.step(w, view(), step);
                    continue;
                }
            }
        }
        stepIsr();
        if (up() && !dev->hung) {
            const MonoMs m = dev->mono();
            if (m % kTickMs == 0) {
                if (w.phys < w.flashBusyUntil) {
                    w.count("loop.ticks_stalled_by_flash");
                } else {
                    controlTick();
                }
            }
            if (dev) {
                serviceTasks();
            }
        }
        if (!idleNow()) {
            quietSince = w.phys;
        }
    }
}

void Runner::sleep(uint32_t ms)
{
    // The API task waits for the control task: the world runs meanwhile,
    // without new events.
    if (!dev) {
        return;
    }
    dev->sleepDebtMs += ms;
    const uint64_t whole = dev->sleepDebtMs / kIsrMs * kIsrMs;
    dev->sleepDebtMs -= static_cast<uint32_t>(whole);
    advance(whole, false);
}

// ---- oracles after each tick ------------------------------------------------------------------------

void Runner::afterTick()
{
    Device &d = *dev;
    if (d.zcWaitTooLongUs != 0) {
        violation("Z1-zero-cross-wait", "the zone driver waited " + std::to_string(d.zcWaitTooLongUs) +
                                            " us for a crossing, more than half a mains period");
        d.zcWaitTooLongUs = 0;
    }
    // The configuration first (handle() applied it before the tick's
    // evaluation), then the starts due up to now, then what the firmware did.
    std::vector<Event> evs;
    Event e;
    while (d.oracleQueue.pop(e)) {
        evs.push_back(e);
    }
    if (d.oracleQueue.dropped() != 0) {
        violation("harness-queue", "oracle event queue overflowed");
    }
    for (const Event &x : evs) {
        if (x.type == EventType::ConfigApplied) {
            onEvent(x);
        }
    }
    scheduleTick();
    for (const Event &x : evs) {
        if (x.type != EventType::ConfigApplied) {
            onEvent(x);
        }
        if (!dev) {
            return;
        }
    }
    const ZoneDriver &drv = d.ctrl->driver();
    const SystemState s = d.state.snapshot();
    const OutputStageStatus st = d.stage.status();
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        const auto zi = static_cast<ZoneIndex>(z);
        if (drv.state(zi) != ValveState::Open) {
            continue;
        }
        const std::string zn = "zone " + std::to_string(z);
        // S5: a faulted, locked-out or inhibited zone is never driven.
        if (drv.lockedOut(zi)) {
            violation("S5-locked-out-open", zn + " open while locked out");
        }
        if (st.phase != StagePhase::Ready) {
            violation("S5-open-without-stage", zn + " open with the output stage in " + toString(st.phase));
        }
        if (s.maintenance) {
            violation("S5-open-in-maintenance", zn + " open in maintenance");
        }
        if (!s.sensors.acPresent) {
            violation("S5-open-without-ac", zn + " open without 24 VAC");
        }
        // S7: what the display and the API read says it is open.
        if (s.zones[z].state != ValveState::Open) {
            violation("S7-state", zn + " open, snapshot says " + toString(s.zones[z].state));
        }
    }
    if (drv.masterOn() && s.masterEnabled && s.masterLockedOut) {
        violation("S5-master-locked-out-open", "master open while locked out");
    }
    if (drv.masterOn() != s.masterOn) {
        violation("S7-state", "master state differs from the snapshot");
    }
    // S7 through the REST API, now and then while something runs.
    if (drv.anyOutputOn() && !d.inApi && w.phys - lastApiStatusPhys > 600'000) {
        lastApiStatusPhys = w.phys;
        const web::ApiResponse r = api("GET", "/api/v1/status", {}, false);
        if (dev && r.status == 200) {
            web::JsonPtr o = web::parseJson(r.body);
            const cJSON *zones = o ? cJSON_GetObjectItem(o.get(), "zones") : nullptr;
            const SystemState now = dev->state.snapshot();
            for (std::size_t z = 0; z < kMaxZones && z < now.zoneCount; ++z) {
                const cJSON *zj = cJSON_GetArrayItem(zones, static_cast<int>(z));
                const cJSON *stj = zj ? cJSON_GetObjectItem(zj, "state") : nullptr;
                if (now.zones[z].state == ValveState::Open &&
                    (!cJSON_IsString(stj) || std::string(stj->valuestring) != "open")) {
                    violation("S7-api", "GET /status shows zone " + std::to_string(z) + " not open");
                }
            }
            w.count("check.api_status");
        }
        if (!dev) {
            return;
        }
    }
    // S9: a healthy board leaves the start-up test after a boot, within a
    // minute of the supply being in range: step 0 waits while VIN_MON is
    // under 0.60 V or above 1.50 V (hardware design 19.10), as in a sag.
    if (st.supplyLow || st.supplyHigh) {
        supplyBadPhys = w.phys;
        w.count("s9.supply_out_of_range_ticks");
    }
    if (!bootChecked && d.mono() > 60'000 && w.phys - supplyBadPhys > 60'000) {
        bootChecked = true;
        // Commissioning cycles after the start-up test report the same phase.
        if (st.phase == StagePhase::StartupTest && st.selfTestsPassed == 0 && w.boardFault == BoardFault::None &&
            bootDisturbedPhys == 0) {
            violation("S9-boot-stuck", "start-up self-test still running a minute after boot (VIN_MON " +
                                           std::to_string(st.vinMonMv) + " mV, supply low " +
                                           std::to_string(st.supplyLow) + ", volts " + std::to_string(w.volts) +
                                           ", lost " + toString(st.currentLost) + ")");
        }
    }
    checkDeadlines();
}

int64_t Runner::firmwareKey(uint16_t pid) const
{
    const SchedulerPersist p = dev->ctrl->scheduler().persist();
    for (std::size_t i = 0; i < p.count; ++i) {
        if (p.fired[i].programId == pid) {
            return p.fired[i].key;
        }
    }
    return Scheduler::kNoKey;
}

Due *Runner::findDue(uint16_t pid, int64_t key)
{
    const auto it = dueIndex.find({pid, key});
    if (it == dueIndex.end() || it->second < dueBase) {
        return nullptr;
    }
    return &dues[it->second - dueBase];
}

const Program *findProgram(const Config &c, uint16_t id)
{
    for (std::size_t i = 0; i < c.schedule.programCount; ++i) {
        if (c.schedule.programs[i].id == id) {
            return &c.schedule.programs[i];
        }
    }
    return nullptr;
}

std::string keyText(int64_t key)
{
    const civil::Date d = civil::civilFromDays(civil::floorDiv(key, 1440));
    const int m = static_cast<int>(civil::floorMod(key, 1440));
    char b[40];
    std::snprintf(b, sizeof b, "%04d-%02u-%02u %02d:%02d", d.year, d.month, d.day, m / 60, m % 60);
    return b;
}

void Runner::onEvent(const Event &e)
{
    switch (e.type) {
    case EventType::ConfigApplied:
        configApplied();
        break;
    case EventType::MasterChanged:
        w.note(std::string("master ") + (e.master.on ? "on" : "off"));
        break;
    case EventType::ZoneChanged:
        if (e.zone.state == ValveState::Open && dev) {
            const RunEntry *head = dev->ctrl->scheduler().entry(0);
            if (head != nullptr && head->source == RunSource::Schedule) {
                const auto it = lastStartedDue.find(head->programId);
                if (it != lastStartedDue.end()) {
                    if (Due *d = findDue(head->programId, it->second)) {
                        d->watered = true;
                    }
                }
            }
        }
        if (e.zone.state != ValveState::Waiting) {
            w.note("zone " + std::to_string(e.zone.zone) + " " + toString(e.zone.state) +
                   (e.zone.state == ValveState::Off ? std::string(" (") + toString(e.zone.reason) + ")" : ""));
        }
        break;
    case EventType::FaultRaised:
        w.note(std::string("fault raised: ") + toString(e.fault.code) +
               (e.fault.zone != kNoZone ? " zone " + std::to_string(e.fault.zone) : "") +
               (e.fault.detail != 0 ? std::string(" check ") + toString(static_cast<HwCheck>(e.fault.detail)) : "") +
               " (current " + std::to_string(static_cast<int>(w.currentMa())) + " mA, " +
               std::to_string(w.energisedCount()) + " energised)");
        w.count(std::string("fault.") + toString(e.fault.code));
        if (e.fault.code == FaultCode::KeepAliveDetector || e.fault.code == FaultCode::OutputExpander) {
            bootDisturbedPhys = bootDisturbedPhys == 0 ? w.phys : bootDisturbedPhys;
        }
        break;
    case EventType::ProgramStarted:
    case EventType::ProgramSkipped: {
        if (e.type == EventType::ProgramStarted && e.program.source != RunSource::Schedule) {
            break;
        }
        const uint16_t pid = e.program.programId;
        const int64_t k = firmwareKey(pid);
        Due *d = findDue(pid, k);
        const bool started = e.type == EventType::ProgramStarted;
        w.count(started ? "s8.started" : "s8.skipped");
        w.note("program " + std::to_string(pid) + (started ? " started" : " skipped (") +
               (started ? "" : toString(e.program.skipReason)) + (started ? "" : ")") + " for " + keyText(k) +
               (e.program.late ? ", late" : "") + " (clock " + keyText(local.key(e.utc)) + " local)");
        if (started && e.program.late) {
            w.count("s8.started_late");
        }
        const auto chg = programChangedPhys.find(pid);
        if (d == nullptr && chg != programChangedPhys.end() && w.phys - chg->second < 70'000) {
            w.count("s8.excluded.config_change");
            break;
        }
        if (d == nullptr) {
            violation(started ? "S8-unexpected-start" : "S8-unexpected-skip",
                      "program " + std::to_string(pid) + " consumed " + keyText(k) + " local, no start due then (" +
                          tz + ", oracle key " + (progKey.count(pid) != 0 ? keyText(progKey[pid]) : "none") +
                          (keysBeforeEval.count(pid) != 0 ? ", firmware had a key" : ", firmware had no key") + ")");
            break;
        }
        if ((d->st == Due::St::Started || d->st == Due::St::Skipped || d->st == Due::St::Missed) && d->replay) {
            d->replay = false;
            d->st = Due::St::Pending; // the replayed day, by the firmware's (wrong) clock
            w.count("s8.replayed_after_back_jump");
        }
        if (d->replaySmall) {
            d->replaySmall = false;
            violation("S8-rerun-after-clock-put-back", "program " + std::to_string(pid) + " start " + keyText(k) +
                                                           " ran again after the clock was put back");
        }
        if (runtimeDamagedAtBoot && (d->st == Due::St::Started || d->st == Due::St::Skipped) &&
            dueBoot[{pid, k}] != boots) {
            violation("S8-duplicate-damaged-record", "program " + std::to_string(pid) + " start " + keyText(k) +
                                                         " ran again from a damaged runtime record");
        } else if (d->st == Due::St::Started && !d->watered && dueBoot[{pid, k}] != boots) {
            // The restart came before any valve opened for it (the self-test
            // before a run takes seconds): running it now waters once.
            w.count("gap.rerun_of_start_cut_before_watering");
            w.note("start " + keyText(k) + " runs again: it was cut by a restart before any valve opened");
        } else if (d->st == Due::St::Started || d->st == Due::St::Skipped) {
            violation("S8-duplicate", "program " + std::to_string(pid) + " start " + keyText(k) + " " +
                                          (started ? "started" : "skipped") + " again" +
                                          (e.program.late ? " (late, after a restart)" : ""));
        } else if (d->st == Due::St::Missed) {
            violation("S8-ran-after-missed", "program " + std::to_string(pid) + " start " + keyText(k) +
                                                 " reported missed, then " + (started ? "started" : "skipped"));
        }
        d->st = started ? Due::St::Started : Due::St::Skipped;
        dueBoot[{pid, k}] = boots;
        if (started) {
            lastStartedDue[pid] = k;
        }
        if (started && armedRestartAfterStart) {
            armedRestartAfterStart = false;
            // A reset in the seconds after a start, before the runtime record
            // is written (firmware architecture section 5: "rebooting right
            // after a start never runs a start twice").
            const uint64_t after = static_cast<uint64_t>(w.rng.range(1, 600)) * 5;
            const bool cut = w.rng.chance(0.5);
            scenario.at(w.phys + after, "fw.restart", [this, cut] {
                if (!up()) {
                    return;
                }
                if (cut) {
                    powerCut();
                } else {
                    shutdown("software_restart", false, w.rng.chance(0.05));
                    pendingBootReason = "software";
                    bootAtPhys = (w.phys + 500) / kTickMs * kTickMs;
                }
            });
        }
        if (k != Scheduler::kNoKey && (progKey.count(pid) == 0 || k > progKey[pid])) {
            progKey[pid] = k;
            progAt[pid] = d->at;
        }
        if (opt.mode == Mode::Liveness) {
            if (!started) {
                violation("L-skipped", "program " + std::to_string(pid) + " skipped (" +
                                           toString(e.program.skipReason) + ") with no fault");
            } else if (e.program.late || e.utc - d->at > 10) {
                violation("L-late", "program " + std::to_string(pid) + " started " + std::to_string(e.utc - d->at) +
                                        " s after " + keyText(k));
            }
            const Program *p = findProgram(applied, pid);
            if (started && p != nullptr) {
                Expected x;
                x.pid = pid;
                x.at = d->at;
                uint64_t total = 0;
                for (std::size_t z = 0; z < applied.zoneCount; ++z) {
                    const uint32_t base = p->durationSec[z];
                    const uint32_t sec =
                        p->useSeasonalAdjust
                            ? static_cast<uint32_t>(
                                  (static_cast<uint64_t>(base) * applied.schedule.seasonalPercent + 50) / 100)
                            : base;
                    if (sec > 0 && applied.zones[z].enabled) {
                        x.zones[static_cast<ZoneIndex>(z)] = sec;
                        total += sec + 30;
                    }
                }
                // Programs queue behind each other: allow for everything queued.
                for (const Expected &o : expected) {
                    for (const auto &[z, sec] : o.zones) {
                        total += sec + 30;
                    }
                }
                x.deadlinePhys = w.phys + (total + 120) * 1000;
                if (!x.zones.empty()) {
                    expected.push_back(x);
                }
            }
        }
        break;
    }
    case EventType::ProgramMissed: {
        const uint16_t pid = e.program.programId;
        w.count("s8.missed");
        w.note("program " + std::to_string(pid) + " missed " + keyText(local.key(e.program.scheduledAt)) +
               " local (clock " + keyText(local.key(e.utc)) + ", oracle key " +
               (progKey.count(pid) != 0 ? keyText(progKey[pid]) : std::string("none")) + ")");
        Due *found = nullptr;
        for (Due &d : dues) {
            if (d.pid == pid && d.at == e.program.scheduledAt) {
                found = &d;
            }
        }
        const auto ch = programChangedPhys.find(pid);
        if (found == nullptr && ch != programChangedPhys.end() && w.phys - ch->second < 70'000) {
            w.count("s8.excluded.config_change");
            break;
        }
        if (found == nullptr && smallBackJumpPhys != 0 && w.phys - smallBackJumpPhys < 25ull * 3600 * 1000) {
            // Policy 8 for a start the oracle never had due: consumed by an
            // edit while the clock was ahead, passed again after it was put
            // back.
            w.count("s8.missed_after_clock_put_back");
            break;
        }
        if (found == nullptr) {
            violation("S8-unexpected-missed", "program " + std::to_string(pid) + " missed at UTC " +
                                                  std::to_string(e.program.scheduledAt) + ", no start due then");
            break;
        }
        if (found->replay) {
            found->replay = false;
            found->st = Due::St::Pending;
            w.count("s8.replayed_after_back_jump");
        }
        if (found->replaySmall) {
            // Missed-start policy 8: the clock passed it again after being
            // put back by less than a day; reported missed, never run.
            found->replaySmall = false;
            if (found->st == Due::St::Pending) {
                found->st = Due::St::Missed;
            }
            w.count("s8.missed_after_clock_put_back");
            break;
        }
        if (runtimeDamagedAtBoot && dueBoot[{pid, found->key}] != boots) {
            // The newest copy of the runtime record may be the damaged one:
            // the catch-up reports a start that may have run rather than run
            // it again (policy 7).
            w.count("s8.missed_after_damaged_runtime_record");
        } else if (found->st == Due::St::Started && !found->watered && dueBoot[{pid, found->key}] != boots) {
            w.count("gap.missed_report_for_start_cut_before_watering");
        } else if (found->st == Due::St::Started || found->st == Due::St::Skipped) {
            violation("S8-missed-after-ran", "program " + std::to_string(pid) + " start " + keyText(found->key) +
                                                 " reported missed after it ran");
        } else {
            found->st = Due::St::Missed;
        }
        if (opt.mode == Mode::Liveness) {
            violation("L-missed", "program " + std::to_string(pid) + " missed " + keyText(found->key));
        }
        if (progKey.count(pid) == 0 || found->key > progKey[pid]) {
            progKey[pid] = found->key;
            progAt[pid] = found->at;
        }
        break;
    }
    case EventType::RunEnded: {
        if (opt.mode != Mode::Liveness || e.run.source != RunSource::Schedule) {
            break;
        }
        for (Expected &x : expected) {
            const auto it = x.zones.find(e.run.zone);
            if (x.pid != e.run.programId || it == x.zones.end() || x.done.count(e.run.zone) != 0) {
                continue;
            }
            x.done.insert(e.run.zone);
            if (e.run.reason != StopReason::Completed || static_cast<uint32_t>(e.run.actualSec) + 1 < it->second ||
                static_cast<uint32_t>(e.run.actualSec) > it->second + 1) {
                violation("L-run", "program " + std::to_string(x.pid) + " zone " + std::to_string(e.run.zone) +
                                       " ran " + std::to_string(e.run.actualSec) + " of " + std::to_string(it->second) +
                                       " s, stop " + toString(e.run.reason));
            }
            w.count("liveness.zone_runs");
            break;
        }
        std::erase_if(expected, [](const Expected &x) { return x.done.size() == x.zones.size(); });
        break;
    }
    default:
        break;
    }
}

UtcSeconds Runner::instantOf(int64_t key) const
{
    // The first instant whose local time reaches `key`: the end of a DST gap
    // for a time inside it, the first occurrence for a repeated time
    // (firmware architecture section 5).
    UtcSeconds s = key * 60 - 15 * 3600;
    for (int guard = 0; guard < 100000; ++guard) {
        const int64_t k = local.key(s);
        if (k >= key) {
            return s;
        }
        const int64_t diff = key - k;
        s += diff > 180 ? (diff - 120) * 60 : 60;
    }
    return s;
}

void Runner::enumerate(const Program &p, UtcSeconds from, UtcSeconds to, bool catchup, bool noKey)
{
    if (!p.enabled || p.startCount == 0) {
        return;
    }
    const int64_t pk = progKey.count(p.id) != 0 ? progKey[p.id] : INT64_MIN;
    int64_t d0 = 0;
    int m0 = 0;
    int64_t d1 = 0;
    int m1 = 0;
    local.at(from, d0, m0);
    local.at(to, d1, m1);
    // A start whose instant lies in (from, to] has a local time no later
    // than the latest local time in that window (plus an hour for DST).
    const int64_t bound = std::max(d0 * 1440 + m0, d1 * 1440 + m1) + 60;
    for (int64_t day = d0 - 1; day <= d1 + 1; ++day) {
        if (!dayMatches(p.days, day)) {
            continue;
        }
        for (uint8_t i = 0; i < p.startCount; ++i) {
            const int64_t key = day * 1440 + p.startMinutes[i];
            if (key <= pk || key > bound) {
                continue;
            }
            const UtcSeconds at = instantOf(key);
            if (at <= from || at > to) {
                continue;
            }
            if (Due *d = findDue(p.id, key)) {
                if (d->st == Due::St::Pending) {
                    d->catchup = true;
                    d->deadlinePhys = w.phys + 20'000;
                    dueBoot[{p.id, key}] = boots;
                }
                continue;
            }
            Due d;
            d.pid = p.id;
            d.key = key;
            d.at = at;
            d.foundPhys = w.phys;
            d.deadlinePhys = w.phys + 20'000;
            d.catchup = catchup;
            if (noKey) {
                d.st = Due::St::Excluded; // policy 5: no stored key, no catch-up
                w.count("s8.excluded.no_stored_key");
            }
            dueIndex[{p.id, key}] = dueBase + dues.size();
            dueBoot[{p.id, key}] = boots;
            dues.push_back(d);
            w.count(catchup ? "s8.due_catchup" : "s8.due");
        }
    }
}

void Runner::scheduleTick()
{
    Device &d = *dev;
    if (!haveApplied || d.time.status() == TimeStatus::Unset) {
        cursorValid = false;
        return;
    }
    local.use(tz);
    const UtcSeconds now = d.clock.nowUtc();
    const int64_t nowKey = local.key(now);
    if (!cursorValid) {
        cursorValid = true;
        cursorUtc = now;
        // The scheduler keeps the keys from before a large backward jump in
        // RAM only; a restart loses them.
        aheadValid = false;
        aheadKey.clear();
        for (std::size_t i = 0; i < applied.schedule.programCount; ++i) {
            const Program &p = applied.schedule.programs[i];
            const bool stored = keysBeforeEval.count(p.id) != 0;
            if (progKey.count(p.id) != 0 && progKey[p.id] > nowKey + 1440) {
                progKey[p.id] = nowKey; // stored while the clock was wrong (firmware clamps it too)
            }
            enumerate(p, now - Scheduler::kLookBackSec, now, true, !stored);
            if (!stored) {
                progKey[p.id] = std::max(progKey.count(p.id) != 0 ? progKey[p.id] : INT64_MIN, nowKey - 1);
                progAt[p.id] = now;
            }
        }
        return;
    }
    if (now < cursorUtc) {
        if (cursorUtc - now > 2) {
            smallBackJumpPhys = w.phys;
        }
        if (cursorUtc - now > 2 && cursorUtc - now <= Scheduler::kLargeBackJumpSec) {
            // Policy 8: the starts the clock will pass again were consumed
            // while it was ahead; the firmware reports each missed when the
            // clock reaches it again.
            for (Due &d : dues) {
                if (d.at > now && d.at <= cursorUtc && d.st != Due::St::Pending && d.st != Due::St::Excluded) {
                    d.replaySmall = true;
                }
            }
        }
        if (cursorUtc - now > Scheduler::kLargeBackJumpSec) {
            // As the scheduler: clamp, and remember what was consumed.
            for (auto &[id, k] : progKey) {
                if (!aheadValid || aheadKey.count(id) == 0 || k > aheadKey[id]) {
                    aheadKey[id] = k;
                    aheadAt[id] = progAt.count(id) != 0 ? progAt[id] : now;
                }
                if (k > nowKey) {
                    k = nowKey;
                    progAt[id] = now;
                }
            }
            aheadCursor = aheadValid ? std::max(aheadCursor, cursorUtc) : cursorUtc;
            aheadValid = true;
            for (Due &d : dues) {
                d.replay = d.key > nowKey;
            }
            w.count("s8.large_back_jump");
            w.note("oracle: clock back more than a day, keys clamped to " + keyText(nowKey));
        }
        cursorUtc = now;
        return;
    }
    if (now == cursorUtc) {
        return;
    }
    if (aheadValid) {
        if (now - cursorUtc > Scheduler::kForwardJumpSec && now >= aheadCursor - Scheduler::kLookBackSec) {
            for (const auto &[id, k] : aheadKey) {
                if (progKey.count(id) == 0 || k > progKey[id]) {
                    progKey[id] = k;
                    progAt[id] = aheadAt[id];
                }
            }
            for (Due &d : dues) {
                d.replay = false;
            }
            aheadValid = false;
            aheadKey.clear();
            w.count("s8.clock_returned");
            w.note("oracle: clock returned, keys restored");
        } else if (now >= aheadCursor) {
            aheadValid = false;
            aheadKey.clear();
        }
    }
    const UtcSeconds from = std::max(cursorUtc, now - Scheduler::kLookBackSec);
    const bool jump = now - cursorUtc > 3;
    for (std::size_t i = 0; i < applied.schedule.programCount; ++i) {
        enumerate(applied.schedule.programs[i], from, now, jump, false);
    }
    cursorUtc = now;
}

bool sameSchedule(const Program &a, const Program &b)
{
    if (a.enabled != b.enabled || a.startCount != b.startCount || a.days.type != b.days.type ||
        a.days.weekdayMask != b.days.weekdayMask || a.days.intervalDays != b.days.intervalDays ||
        a.days.intervalAnchorDay != b.days.intervalAnchorDay) {
        return false;
    }
    std::vector<uint16_t> x(a.startMinutes.begin(), a.startMinutes.begin() + a.startCount);
    std::vector<uint16_t> y(b.startMinutes.begin(), b.startMinutes.begin() + b.startCount);
    std::sort(x.begin(), x.end());
    std::sort(y.begin(), y.end());
    return x == y;
}

void Runner::configApplied()
{
    if (!dev) {
        return;
    }
    const Config c = dev->config.get();
    const bool valid = dev->time.status() != TimeStatus::Unset;
    const UtcSeconds now = dev->clock.nowUtc();
    const std::string newTz = c.timezone.c_str();
    if (haveApplied && newTz != tz) {
        tzChanged = true;
        tzChangedPhys = w.phys;
        local.use(newTz);
        for (auto &[id, k] : progKey) {
            if (progAt.count(id) != 0) {
                k = local.key(progAt[id]);
            }
        }
        w.count("s8.tz_changes");
        // Local keys of the old zone mean other instants now.
        aheadValid = false;
        aheadKey.clear();
        dues.clear();
        dueIndex.clear();
        dueBoot.clear();
        dueBase = 0;
    }
    tz = newTz;
    local.use(tz);
    for (std::size_t i = 0; i < c.schedule.programCount; ++i) {
        const Program &p = c.schedule.programs[i];
        const Program *old = haveApplied ? findProgram(applied, p.id) : nullptr;
        if (haveApplied && old != nullptr && sameSchedule(*old, p) && valid && configSeenThisBoot) {
            // The scheduler advances every program's key to the current minute
            // on an edit. Mirrored to the minute before, so that a start in the
            // same minute as an unrelated edit is still expected.
            const int64_t k = local.key(now) - 1;
            if (progKey.count(p.id) == 0 || progKey[p.id] < k) {
                progKey[p.id] = k;
                progAt[p.id] = now - 60;
            }
        }
        if (haveApplied && (old == nullptr || !sameSchedule(*old, p))) {
            programChangedPhys[p.id] = w.phys;
            // At boot the scheduler cannot tell a changed program (a write
            // interrupted by the power cut) and keeps its stored key.
            if (valid && configSeenThisBoot) {
                // Policy 6: an edit never triggers a start whose time has
                // passed, this minute included.
                const int64_t k = local.key(now);
                if (progKey.count(p.id) == 0 || progKey[p.id] < k) {
                    progKey[p.id] = k;
                    progAt[p.id] = now;
                }
            }
        }
    }
    applied = c;
    haveApplied = true;
    configSeenThisBoot = true;
    w.note("configuration applied: " + std::to_string(c.schedule.programCount) + " programs, " + tz);
    if (opt.trace) {
        for (std::size_t i = 0; i < c.schedule.programCount; ++i) {
            const Program &p = c.schedule.programs[i];
            std::string t = "  program " + std::to_string(p.id) + (p.enabled ? "" : " (disabled)") + " days " +
                            std::to_string(static_cast<int>(p.days.type)) + "/" + std::to_string(p.days.weekdayMask) +
                            "/" + std::to_string(p.days.intervalDays) + "@" + std::to_string(p.days.intervalAnchorDay) +
                            " starts";
            for (uint8_t k = 0; k < p.startCount; ++k) {
                t += " " + std::to_string(p.startMinutes[k]);
            }
            const int64_t fk = firmwareKey(p.id);
            t += ", firmware key " + (fk == Scheduler::kNoKey ? std::string("none") : keyText(fk)) + ", oracle key " +
                 (progKey.count(p.id) != 0 ? keyText(progKey[p.id]) : std::string("none")) + ", clock " +
                 keyText(local.key(now));
            w.note(t);
        }
    }
}

void Runner::checkDeadlines()
{
    for (Due &d : dues) {
        if (d.st != Due::St::Pending || d.deadlinePhys > w.phys) {
            continue;
        }
        const Program *p = findProgram(applied, d.pid);
        const auto ch = programChangedPhys.find(d.pid);
        const bool changed = ch != programChangedPhys.end() && ch->second + 70'000 >= d.foundPhys;
        const bool tzRecent = tzChanged && w.phys - tzChangedPhys < 25ull * 3600 * 1000;
        if (p == nullptr || !p->enabled || changed || tzRecent) {
            d.st = Due::St::Excluded;
            w.count("s8.excluded.config_change");
            continue;
        }
        if (dueBoot[{d.pid, d.key}] != boots) {
            // The firmware restarted before it could act: caught up after
            // the next boot (or excluded there).
            continue;
        }
        if (dev && dev->clock.nowUtc() - d.at > Scheduler::kLookBackSec) {
            d.st = Due::St::Excluded; // policy 1: older than a day
            w.count("s8.excluded.older_than_a_day");
            continue;
        }
        d.st = Due::St::Excluded;

        violation(d.catchup ? "S8-lost-catchup" : "S8-lost", "program " + std::to_string(d.pid) + " start " +
                                                                 keyText(d.key) + " (" + tz +
                                                                 ") neither started, skipped nor reported missed");
    }
    // Bounded: forget settled starts after three days.
    while (!dues.empty() && dues.front().st != Due::St::Pending &&
           w.phys - dues.front().foundPhys > 3 * 86'400'000ull) {
        dueIndex.erase({dues.front().pid, dues.front().key});
        dueBoot.erase({dues.front().pid, dues.front().key});
        dues.pop_front();
        ++dueBase;
    }
    if (dues.size() > 20000) {
        violation("harness-dues", "schedule oracle holds too many starts");
    }
    for (Expected &x : expected) {
        if (x.deadlinePhys <= w.phys) {
            std::string why;
            if (dev) {
                const SystemState st = dev->state.snapshot();
                const OutputStageStatus ss = dev->stage.status();
                why = std::string("; stage ") + toString(ss.phase) + ", check " + toString(ss.fault) + ", queue " +
                      std::to_string(st.queue.length) + " " + toString(st.queue.state) + ", inhibit " +
                      std::to_string(st.inhibitFlags) + ", faults 0x" + std::to_string(st.activeFaults) +
                      ", keep-alive request " + std::to_string(static_cast<int>(dev->guard.request())) +
                      ", flash busy " + std::to_string(w.flashBusyUntil > w.phys ? w.flashBusyUntil - w.phys : 0);
                if (st.queue.shownCount > 0) {
                    const ZoneIndex z = st.queue.items[0].zone;
                    why += ", head zone " + std::to_string(z) + " " + toString(st.zones[z].state) + " rest " +
                           std::to_string(st.zones[z].restRemainingSec);
                }
            }
            violation("L-program", "program " + std::to_string(x.pid) + " did not complete its zones in time (" +
                                       std::to_string(x.done.size()) + " of " + std::to_string(x.zones.size()) + ")" +
                                       why);
            x.done.clear();
            for (const auto &[z, s] : x.zones) {
                x.done.insert(z);
            }
        }
    }
    std::erase_if(expected, [](const Expected &x) { return x.done.size() == x.zones.size(); });
}

// ---- the scenario's actions -------------------------------------------------------------------------

void Runner::powerCut()
{
    const uint64_t outage = logUniform(w.rng, 200, 6ull * 3600 * 1000);
    w.transformer = false;
    shutdown("power_cut", true, false);
    pendingBootReason = "power_on";
    const uint64_t back = (w.phys + outage) / kTickMs * kTickMs;
    scenario.at(back, "fault.end", [this] { w.transformer = true; });
    bootAtPhys = back + 300;
}

void Runner::syncTime()
{
    if (!up() || !wifi) {
        return;
    }
    const int64_t err = w.sys.now() - w.trueUtc();
    if (err != 0) {
        w.count("time.sntp_step");
        if (std::llabs(err) > 2) {
            w.note("SNTP steps the clock by " + std::to_string(-err) + " s");
        }
    }
    w.sys.set(w.trueUtc());
    dev->time.onNetworkSync();
}

void Runner::manualRun()
{
    if (!up()) {
        return;
    }
    const Config c = dev->config.get();
    const auto z = static_cast<ZoneIndex>(w.rng.below(c.zoneCount));
    const uint32_t sec = static_cast<uint32_t>(logUniform(w.rng, 5, 4 * 3600));
    dev->commands.post(Command::runZone(z, sec));
}

void Runner::displayCommand()
{
    if (!up()) {
        return;
    }
    Device &d = *dev;
    const Config c = d.config.get();
    Command cmd;
    switch (w.rng.below(12)) {
    case 0:
    case 1:
        cmd = Command::runZone(static_cast<ZoneIndex>(w.rng.below(10)),
                               static_cast<uint32_t>(logUniform(w.rng, 1, 100000)));
        break;
    case 2:
        cmd = Command::runProgram(c.schedule.programCount > 0 && w.rng.chance(0.8)
                                      ? c.schedule.programs[w.rng.below(c.schedule.programCount)].id
                                      : static_cast<uint16_t>(w.rng.below(65536)));
        break;
    case 3:
        cmd = Command::simple(CommandType::StopAll);
        break;
    case 4:
        cmd = Command::simple(CommandType::Pause);
        break;
    case 5:
        cmd = Command::simple(CommandType::Resume);
        break;
    case 6:
        cmd = Command::simple(CommandType::Skip);
        break;
    case 7:
        cmd =
            Command::rainDelay(w.rng.chance(0.5) ? 0 : d.clock.nowUtc() + static_cast<int64_t>(w.rng.range(60, 86400)));
        break;
    case 8:
        cmd = Command::simple(CommandType::ClearFaults);
        break;
    case 9: {
        cmd = Command::acknowledgeRepair();
        cmd.flag = w.rng.chance(0.7);
        break;
    }
    case 10:
        cmd = Command::simple(CommandType::ReloadConfig);
        break;
    default:
        cmd = Command::runZone(static_cast<ZoneIndex>(w.rng.below(8)), static_cast<uint32_t>(w.rng.range(0, 3)));
        break;
    }
    d.commands.post(cmd);
    w.count("user.display_commands");
}

std::string programJson(const Program &p, uint8_t zoneCount)
{
    Config c = Config::defaults();
    c.zoneCount = zoneCount;
    c.schedule.programCount = 1;
    c.schedule.programs[0] = p;
    web::JsonPtr doc = web::parseJson(configToJson(c));
    cJSON *progs = cJSON_GetObjectItem(cJSON_GetObjectItem(doc.get(), "schedule"), "programs");
    return web::printJson(cJSON_GetArrayItem(progs, 0));
}

void Runner::restCall()
{
    if (!up()) {
        return;
    }
    const Config c = dev->config.get();
    Rng &r = w.rng;
    const int zone = static_cast<int>(r.range(0, 10));
    const uint16_t pid = c.schedule.programCount > 0 && r.chance(0.8)
                             ? c.schedule.programs[r.below(c.schedule.programCount)].id
                             : static_cast<uint16_t>(r.below(70000));
    auto num = [&]() -> std::string {
        switch (r.below(6)) {
        case 0:
            return "0";
        case 1:
            return "-5";
        case 2:
            return "1e12";
        case 3:
            return "\"abc\"";
        case 4:
            return std::to_string(r.range(1, 100));
        default:
            return std::to_string(logUniform(r, 1, 20000));
        }
    };
    switch (r.below(22)) {
    case 0:
    case 1:
    case 2:
        api("POST", "/api/v1/zones/" + std::to_string(zone) + "/run", "{\"duration_s\":" + num() + "}");
        break;
    case 3:
        api("POST", "/api/v1/zones/" + std::to_string(zone) + "/stop");
        break;
    case 4:
        api("POST", "/api/v1/programs/" + std::to_string(pid) + "/run");
        break;
    case 5:
        api("POST", std::string("/api/v1/queue/") + (const char *[]){"pause", "resume", "skip", "stop"}[r.below(4)]);
        break;
    case 6:
        api("PUT", "/api/v1/rain-delay", r.chance(0.5) ? "{\"hours\":" + num() + "}" : "{\"until_utc\":" + num() + "}");
        break;
    case 7:
        api("DELETE", "/api/v1/rain-delay");
        break;
    case 8:
        api("POST", "/api/v1/faults/clear");
        break;
    case 9:
        api("POST", "/api/v1/faults/acknowledge-repair",
            r.chance(0.7) ? "{\"repaired\":true}" : "{\"repaired\":false}");
        break;
    case 10:
    case 11: {
        static const char *kFields[] = {"max_run_s",      "enabled",       "current_min_ma",
                                        "current_max_ma", "default_run_s", "name"};
        const char *f = kFields[r.below(std::size(kFields))];
        std::string v = num();
        if (std::string(f) == "enabled") {
            v = r.chance(0.8) ? "true" : "false";
        } else if (std::string(f) == "name") {
            v = r.chance(0.5) ? "\"Lawn\"" : "\"" + std::string(40, 'x') + "\"";
        }
        api("PATCH", "/api/v1/zones/" + std::to_string(zone), std::string("{\"") + f + "\":" + v + "}");
        break;
    }
    case 12:
        api("POST", "/api/v1/programs",
            programJson(randomProgram(r, static_cast<uint16_t>(r.range(1, 300)), c.zoneCount, w.trueUtc() / 86400),
                        c.zoneCount));
        break;
    case 13:
        api("PUT", "/api/v1/programs/" + std::to_string(pid),
            programJson(randomProgram(r, pid, c.zoneCount, w.trueUtc() / 86400), c.zoneCount));
        break;
    case 14:
        api("DELETE", "/api/v1/programs/" + std::to_string(pid));
        break;
    case 15:
        configEdit();
        break;
    case 16:
        api("GET", std::string("/api/v1/history?limit=") + std::to_string(r.range(1, 200)), {}, false);
        break;
    case 17:
        api("GET", "/api/v1/status", {}, false);
        break;
    case 18: {
        // Garbage bodies.
        static const char *kBad[] = {"{", "[]", "null", "{\"duration_s\":}", "\xff\xfe", ""};
        api("POST", "/api/v1/zones/1/run", kBad[r.below(std::size(kBad))]);
        break;
    }
    case 19: {
        std::string deep(200, '[');
        api("PUT", "/api/v1/config", deep);
        break;
    }
    case 20:
        api("GET", "/api/v1/queue", {}, false);
        break;
    default:
        api("GET", "/api/v1/faults", {}, false);
        break;
    }
}

void Runner::configEdit()
{
    if (!up()) {
        return;
    }
    Rng &r = w.rng;
    Config c = dev->config.get();
    const double u = r.real();
    if (u < 0.3) {
        // A whole new configuration (restore of another backup).
        c = randomConfig(r, opt.mode);
    } else if (u < 0.5) {
        // Change the time zone only.
        c.timezone.assign(r.pick(timeZones()));
    } else if (u < 0.7) {
        c.driver.maxConcurrent = static_cast<uint8_t>(r.range(1, 2));
        c.driver.minRestSec = static_cast<uint32_t>(r.range(60, 3600));
        c.zones[r.below(kMaxZones)].maxRunSec = static_cast<uint32_t>(r.range(30, 14400));
    } else if (u < 0.8) {
        c.schedule.seasonalPercent = static_cast<uint16_t>(r.range(0, 250));
        c.schedule.missedStartGraceMin = static_cast<uint16_t>(r.range(0, 720));
    }
    std::string json = configToJson(c);
    if (r.chance(0.15)) {
        // Invalid: out of range, wrong type, newer schema, truncated.
        switch (r.below(4)) {
        case 0:
            json.replace(json.find("\"max_concurrent\":") + 17, 1, "9");
            break;
        case 1:
            json.replace(json.find("\"schema\":") + 9, 1, "9");
            break;
        case 2:
            json.resize(json.size() / 2);
            break;
        default:
            json.replace(json.find("\"zone_count\":") + 13, 1, "\"x\"");
            break;
        }
    }
    const char *path = r.chance(0.5) ? "/api/v1/config" : "/api/v1/config/restore";
    const web::ApiResponse resp = api(std::string(path) == "/api/v1/config" ? "PUT" : "POST", path, json);
    w.count(resp.status == 200 ? "user.config_ok" : "user.config_rejected");
}

void Runner::mqttRandom()
{
    if (!up() || !wifi) {
        return;
    }
    Rng &r = w.rng;
    const Config c = dev->config.get();
    const int zone = static_cast<int>(r.range(0, 9));
    const uint16_t pid = c.schedule.programCount > 0 && r.chance(0.8)
                             ? c.schedule.programs[r.below(c.schedule.programCount)].id
                             : static_cast<uint16_t>(r.below(70000));
    const bool retained = r.chance(0.05);
    switch (r.below(12)) {
    case 0:
    case 1:
        mqttMessage("zone/" + std::to_string(zone) + "/run", std::to_string(logUniform(r, 1, 30000)), retained);
        break;
    case 2:
        mqttMessage("zone/" + std::to_string(zone) + "/set", r.chance(0.5) ? "OPEN" : "CLOSE", retained);
        break;
    case 3:
        mqttMessage("zone/" + std::to_string(zone) + "/stop", "1", retained);
        break;
    case 4:
        mqttMessage("zone/" + std::to_string(zone) + "/default_run_s", std::to_string(r.range(-5, 20000)));
        break;
    case 5:
        mqttMessage("program/" + std::to_string(pid) + "/run", "1", retained);
        break;
    case 6:
        mqttMessage("program/" + std::to_string(pid) + "/enabled", r.chance(0.5) ? "ON" : "OFF");
        break;
    case 7:
        mqttMessage((const char *[]){"stop_all", "pause", "resume", "skip", "clear_faults"}[r.below(5)], "1", retained);
        break;
    case 8:
        mqttMessage("rain_delay", r.chance(0.5) ? std::to_string(r.range(0, 800)) : "{\"hours\":3}");
        break;
    case 9:
        mqttMessage("zone/1/run", std::string(300, '9'));
        break;
    case 10:
        mqttMessage("zone/1/run", "");
        break;
    default:
        mqttMessage("nonsense/x/y/z", "{]");
        break;
    }
}

void Runner::flood()
{
    if (!up()) {
        return;
    }
    // Someone's automation in a loop: a burst through every interface.
    const int n = static_cast<int>(w.rng.range(20, 200));
    for (int i = 0; i < n && up(); ++i) {
        switch (w.rng.below(3)) {
        case 0:
            displayCommand();
            break;
        case 1:
            mqttRandom();
            break;
        default:
            if (w.rng.chance(0.2)) {
                restCall();
            } else {
                dev->commands.post(Command::runZone(static_cast<ZoneIndex>(w.rng.below(8)), 600));
            }
            break;
        }
    }
    w.count("user.floods");
}

void Runner::setupScenario()
{
    Rng &r = w.rng;
    // The board's detector corner, fixed for the board's life.
    static constexpr test::DetectorCorner kCorners[] = {
        test::DetectorCorner::Typical, test::DetectorCorner::LatestRelease, test::DetectorCorner::EarliestRelease,
        test::DetectorCorner::SlowCharge};
    w.board.setCorner(opt.mode == Mode::Liveness ? test::DetectorCorner::Typical : kCorners[r.below(4)]);
    w.board.eightZone = opt.mode == Mode::Liveness || !r.chance(0.1);
    Config c = randomConfig(r, opt.mode);
    if (opt.mode == Mode::Liveness) {
        // No faults: programs that cannot collide with themselves or the
        // run budget, at times that cross every DST rule in the zone.
        static constexpr uint16_t kSlots[] = {0, 90, 150, 300, 360, 450, 720, 1125, 1260, 1439};
        c.schedule.programCount = static_cast<uint8_t>(r.range(1, 6));
        std::array<uint32_t, kMaxZones> perZone{};
        for (uint8_t i = 0; i < c.schedule.programCount; ++i) {
            Program p = randomProgram(r, static_cast<uint16_t>(i + 1), c.zoneCount, w.trueUtc() / 86400);
            p.enabled = true;
            p.obeySensorInhibit = true;
            p.startCount = static_cast<uint8_t>(r.range(1, 2));
            const auto a = static_cast<std::size_t>(r.below(std::size(kSlots)));
            p.startMinutes[0] = kSlots[a];
            p.startMinutes[1] = kSlots[(a + 5) % std::size(kSlots)];
            p.durationSec.fill(0);
            for (int k = 0; k < 3; ++k) {
                const auto z = static_cast<std::size_t>(r.below(c.zoneCount));
                const auto d = static_cast<uint32_t>(r.range(60, 600));
                if (perZone[z] + d * 2 <= 2400) {
                    perZone[z] += d * 2;
                    p.durationSec[z] = d;
                }
            }
            if (std::all_of(p.durationSec.begin(), p.durationSec.end(), [](uint32_t d) { return d == 0; })) {
                // Every pick was over the cap: the least used zone, briefly
                // (a program with no zone is skipped as nothing to run).
                const auto z = static_cast<std::size_t>(
                    std::min_element(perZone.begin(), perZone.begin() + c.zoneCount) - perZone.begin());
                perZone[z] += 120;
                p.durationSec[z] = 60;
            }
            c.schedule.programs[i] = p;
        }
    }
    if (!validate(c).ok) {
        c = Config::defaults();
    }
    w.nvs.data[ConfigStore::kConfigKey] = configToJson(c);
    if (opt.mode == Mode::Liveness) {
        return;
    }
    if (opt.mode == Mode::Halt) {
        haltScript();
        return;
    }
    scenario.addWorldFaults([this] {
        // A person sees the fault, has it repaired, and confirms it.
        scenario.at(w.phys + logUniform(w.rng, 60'000, 86'400'000), "user.clear", [this] {
            if (up()) {
                dev->commands.post(Command::acknowledgeRepair());
                dev->commands.post(Command::simple(CommandType::ClearFaults));
            }
        });
    });
    constexpr double kHour = 3'600'000.0;
    constexpr double kDay = 24 * kHour;
    scenario.every("user.run", 6 * kHour, [this] { manualRun(); });
    scenario.every("user.display", 4 * kHour, [this] { displayCommand(); });
    scenario.every("user.rest", 3 * kHour, [this] { restCall(); });
    scenario.every("user.mqtt", 3 * kHour, [this] { mqttRandom(); });
    scenario.every("user.flood", 4 * kDay, [this] { flood(); });
    scenario.every("user.config", 2 * kDay, [this] { configEdit(); });
    scenario.every("user.time", 10 * kDay, [this] {
        if (up()) {
            // Someone sets the clock by hand, not always right.
            const int64_t off =
                w.rng.chance(0.5) ? w.rng.range(-120, 120) : w.rng.range(-3, 3) * 86400 + w.rng.range(-7200, 7200);
            api("PUT", "/api/v1/time", "{\"utc\":" + std::to_string(w.trueUtc() + off) + "}");
            w.note("user sets the clock off by " + std::to_string(off) + " s");
        }
    });
    scenario.every("user.clear", 1 * kDay, [this] {
        if (up()) {
            dev->commands.post(Command::simple(CommandType::ClearFaults));
        }
    });
    scenario.every("user.maintenance", 6 * kDay, [this] {
        if (up()) {
            dev->commands.post(Command::maintenance(true));
            scenario.at(w.phys + logUniform(w.rng, 10'000, 1'800'000), "fault.end", [this] {
                if (up()) {
                    dev->commands.post(Command::maintenance(false));
                }
            });
        }
    });
    scenario.every("net.wifi", 2 * kDay, [this] {
        wifi = false;
        w.note("WiFi down");
        scenario.at(w.phys + logUniform(w.rng, 60'000, 2 * 86'400'000ull), "fault.end", [this] {
            wifi = true;
            w.note("WiFi back");
        });
    });
    scenario.every("net.sntp", 1 * kHour, [this] { syncTime(); });
    scenario.every("power.cut", 3 * kDay, [this] {
        if (up() || down == Down::Hung) {
            powerCut();
        }
    });
    scenario.every("power.brownout", 8 * kDay, [this] {
        if (up()) {
            shutdown("brownout", true, false);
            pendingBootReason = "brownout";
            bootAtPhys = (w.phys + 500) / kTickMs * kTickMs;
        }
    });
    scenario.every("fw.restart", 3 * kDay, [this] {
        if (up()) {
            // A restart from the API, an update, or a panic.
            shutdown("software_restart", false, w.rng.chance(0.05));
            pendingBootReason = "software";
            bootAtPhys = (w.phys + static_cast<uint64_t>(w.rng.range(300, 3000))) / kTickMs * kTickMs;
        }
    });
    scenario.every("fw.hang", 6 * kDay, [this] { halt(false, false, 0); });
    scenario.every("fw.freeze", 8 * kDay, [this] { halt(true, false, logUniform(w.rng, 300, 8000)); });
    // Restarts just after scheduled starts, where a start could run twice.
    scenario.every("fw.restart", 2 * kDay, [this] { armedRestartAfterStart = true; });
}

void Runner::haltScript()
{
    // Halt mode: one board fault at a time, cycled by seed; a run; the fault
    // appears while valves are on; the firmware halts before it can react.
    const auto f = static_cast<BoardFault>(opt.seed % static_cast<uint64_t>(BoardFault::Count));
    const uint64_t period = 120'000;
    const auto cycles = static_cast<uint64_t>(opt.days * 86'400'000.0 / period);
    for (uint64_t i = 0; i < cycles; ++i) {
        const uint64_t t0 = 20'000 + i * period;
        scenario.at(t0, "halt.script", [this] {
            if (!up()) {
                return;
            }
            const Config c = dev->config.get();
            dev->commands.post(Command::runZone(static_cast<ZoneIndex>(w.rng.below(c.zoneCount)), 3600));
            if (c.driver.maxConcurrent > 1 && w.rng.chance(0.5)) {
                dev->commands.post(Command::runZone(static_cast<ZoneIndex>(w.rng.below(c.zoneCount)), 3600));
            }
        });
        // The fault appears 5 to 20 s after the run was asked for (the
        // self-test takes about 5 s), then the firmware halts 0 to 400 ms
        // later, before it can act on what it sees.
        const uint64_t tf = t0 + static_cast<uint64_t>(w.rng.range(1000, 4000)) * 5;
        const uint64_t th = tf + static_cast<uint64_t>(w.rng.range(0, 80)) * 5;
        const bool freeze = w.rng.chance(0.6);
        scenario.at(tf, "halt.script", [this, f] {
            if (up()) {
                w.setBoardFault(f);
            }
        });
        scenario.at(th, "halt.script", [this, freeze] {
            w.count(w.energisedCount() > 0 ? "halt.with_valve_on" : "halt.with_valves_off");
            halt(freeze, false, logUniform(w.rng, 3000, 8000));
        });
        // Repaired before the next cycle.
        scenario.at(t0 + period - 15'000, "halt.script", [this] {
            w.setBoardFault(BoardFault::None);
            if (up()) {
                dev->commands.post(Command::acknowledgeRepair());
                dev->commands.post(Command::simple(CommandType::ClearFaults));
            }
        });
    }
}

void Runner::finish()
{
    w.count("sim_seconds", w.phys / 1000);
    w.count("s1.max_uncommanded_ms", phys.maxUncommandedMs());
    w.count("s2.max_halt_to_safe_ms", phys.maxHaltToSafeMs());
    w.count("s11.alloc_in_tick", allocInTick);
    w.count("flash.bytes_end", w.nvs.bytes());
    w.count("flash.keys_end", w.nvs.keys());
    // S10: the stores stay bounded. History: 24 chunks of 1012 bytes, plus
    // the configuration, the runtime record and the detector reference.
    if (w.nvs.bytes() > 40'000 || w.nvs.keys() > 32) {
        violation("S10-flash-growth",
                  std::to_string(w.nvs.bytes()) + " bytes in " + std::to_string(w.nvs.keys()) + " keys");
    }
    if (maxLiveBytesDay2 != 0 && maxLiveBytesLate > maxLiveBytesDay2 + 4'000'000) {
        violation("S10-heap-growth", "live heap grew from " + std::to_string(maxLiveBytesDay2) + " to " +
                                         std::to_string(maxLiveBytesLate) + " bytes");
    }
    // Starts still pending at the end are not judged.
    res.stats = w.stats;
    res.simDays = static_cast<double>(w.phys) / 86'400'000.0;
}

Result Runner::run()
{
    setupScenario();
    boot();
    const uint64_t total = static_cast<uint64_t>(opt.days * 86'400'000.0);
    while (w.phys < total) {
        const uint64_t chunk = std::min<uint64_t>(total - w.phys, 60'000);
        advance(chunk, true);
        // S10: heap sampled once a simulated hour.
        if (w.phys >= nextHeapSample) {
            nextHeapSample = w.phys + 3'600'000;
            const uint64_t live = liveBytes();
            if (w.phys > 86'400'000 && w.phys <= 2 * 86'400'000ull) {
                maxLiveBytesDay2 = std::max(maxLiveBytesDay2, live);
            } else if (w.phys > total - 86'400'000 && total > 3 * 86'400'000ull) {
                maxLiveBytesLate = std::max(maxLiveBytesLate, live);
            }
        }
        if (opt.stopAtFirst && res.violationCount > 0) {
            break;
        }
    }
    finish();
    if (dev) {
        setHistoryLog(nullptr);
        dev.reset();
    }
    return res;
}

} // namespace

Result runStandalone(const Options &opt)
{
    Runner r(opt);
    return r.run();
}

} // namespace zf::soak
