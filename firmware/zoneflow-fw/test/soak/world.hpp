// SPDX-License-Identifier: Apache-2.0
// The world around the firmware in the soak simulation: the simulated rev A
// board (test/host/fake_board.hpp), the valves and their wiring, the
// transformer and mains, the flow meter and rain sensor, the ADC, the
// non-volatile stores with interrupted and corrupted writes, the RTC chip
// and the system clock. Physical time is `phys`, milliseconds since the
// start of the run; it keeps running while the firmware is off.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "alloc_count.hpp"
#include "fake_board.hpp"
#include "fakes.hpp"
#include "soak.hpp"
#include "zf/app/retained.hpp"
#include "zf/config/config_store.hpp"
#include "zf/diag/current_sense.hpp"
#include "zf/time/civil.hpp"
#include "zf/time/time_service.hpp"

namespace zf::soak {

inline constexpr uint64_t kIsrMs = 5;
inline constexpr uint64_t kTickMs = 50;
inline constexpr uint64_t kWindowMs = 100;
inline constexpr std::size_t kOutputs = kMaxZones + 1; // zones, then the master
inline constexpr std::size_t kMasterOut = kMaxZones;

class World;

// Counters reported at the end of a run.
using Stats = std::map<std::string, uint64_t>;

// One NVS namespace. An NVS blob write leaves the old or the new value
// (ESP-IDF NVS documentation); a write still in progress when the chip
// resets or loses power is resolved here: mostly old or new, sometimes torn
// (truncated or with flipped bits) to model damage beyond what NVS promises.
// Every write keeps the flash busy for a while, which stalls the control
// task as a real flash operation does (firmware architecture section 2).
class Flash final : public IBlobStorage {
public:
    Flash(World &w, const char *name) : w_(w), name_(name) {}
    bool read(const char *key, std::string &out) override;
    bool write(const char *key, std::string_view v) override;
    bool erase(const char *key) override;

    // Resolves writes in progress at a reset or power loss.
    void interrupt();
    // Flips one bit in one stored value (bit rot, a bad page).
    void rot();
    std::size_t bytes() const;
    std::size_t keys() const { return data.size(); }

    std::map<std::string, std::string> data;
    double failRate = 0.0; // share of writes that fail (NVS full, driver error)
    // Keys whose stored value is damaged beyond what NVS promises (torn or
    // rotten) and not written since.
    std::set<std::string> damaged;

private:
    struct InFlight {
        std::string key;
        bool had = false;
        std::string old;
        uint64_t untilMs = 0;
    };
    World &w_;
    const char *name_;
    std::vector<InFlight> inflight_;
};

// RTC_NOINIT memory: kept by a software reset, lost by a power cycle. A save
// interrupted by a reset leaves a mix of the old and the new record.
class RetainedMemory final : public IRetainedStore {
public:
    bool load(RetainedRecord &out) override
    {
        out = rec;
        return intact(rec);
    }
    void save(const RetainedRecord &r) override
    {
        prev = rec;
        rec = r;
    }
    void tear(Rng &rng);
    void powerLoss(Rng &rng);
    RetainedRecord rec{};
    RetainedRecord prev{};
};

// The ESP32's system clock: kept over a software reset, 1970 after power-on.
class SysTime final : public ISystemTime {
public:
    explicit SysTime(World &w) : w_(w) {}
    UtcSeconds now() const override;
    bool set(UtcSeconds v) override;
    void powerLoss();
    int64_t offsetMs = 0; // system time = phys + offsetMs
private:
    World &w_;
};

// The PCF8563 on its coin cell: drifts, may stop answering, may lose time.
class RtcChip final : public IRtc {
public:
    explicit RtcChip(World &w) : w_(w) {}
    bool read(UtcSeconds &out) override;
    bool write(UtcSeconds utc) override;
    bool answers = true;
    bool timeLost = false; // VL flag: oscillator stopped or battery low
    int64_t offsetMs = 0;  // from true UTC at refPhys
    double driftPpm = 0.0;
    uint64_t refPhys = 0;
    int64_t errorMs() const;

private:
    World &w_;
};

// Valve wiring as the current sees it.
enum class Wire : uint8_t {
    Ok,
    Open,         // cut wire or burnt coil: no current
    Short,        // shorted wire: far above the comparator's trip level
    Over,         // wrong or shorted-turn solenoid: above the window, below the trip
    Intermittent, // corroded splice: comes and goes
    HalfWave,     // one triac polarity not conducting: DC component
};
const char *toString(Wire w);

struct Output {
    int32_t holdMa = 300;
    float lpm = 10.0f;
    Wire wire = Wire::Ok;
    bool energised = false;
    uint64_t energisedSinceMs = 0;
    bool intermittentOn = true;
    uint64_t nextFlipMs = 0;
};

// The board faults the simulated board can model, one at a time (spec
// section 2 rule 3: no single component fault may leave a valve on).
enum class BoardFault : uint8_t {
    None,
    AStuckOn,        // detector A (gate drive) always on
    AOnPinHigh,      // C22 shorted: A on while the pin is held high
    ANeverOn,        // detector A never enables
    ALateRelease,    // open bleed: A releases after 1.7 s
    BStuckOn,        // detector B (relay transistor) always on
    BOnPinHigh,      // C51 shorted
    BNeverOn,        // relay never closes
    BLateRelease,    // relay detector releases after 2 s
    LatchStuck,      // over-current latch does not clear
    RelayWelded,     // K1 contact welded
    ShortedOutput,   // one triac failed short
    ExpIgnoresReset, // DRV_EN stuck high
    ExpNeverActive,  // expander never comes out of reset
    Q3Shorted,       // V_DRV on whatever DRV_EN does
    VDrvNeverOn,     // V_DRV never high
    R12Open,         // relay driver keeps its gate charge
    RelayRbLow,      // B_RB read-back stuck low
    Q2GateDrain,     // GPIO7 at 0.6 V
    KaDisconnected,  // KA pin not connected: detectors never see edges
    KaToggleStrong,  // KA bridged to a net that toggles by itself
    KaToggleWeak,
    KaHighStrong, // KA shorted to 3V3
    KaHighWeak,
    PinStuckLow,  // an expander pin forced low (a valve on with drive)
    PinStuckHigh, // an expander pin forced high
    SensorFault,  // sensor supply over-current
    Count,
};
const char *toString(BoardFault f);
// Which keep-alive layer a fault disables: the gate-drive layer (A), the
// relay layer (B), or neither as far as stopping current goes.
bool faultDisablesA(BoardFault f);
bool faultDisablesB(BoardFault f);

// One window of the two current channels and GPIO7, as the ADC task
// accumulates it (hardware design 19.2).
struct WindowAccumulator {
    double sumSq = 0.0; // full-wave part, mA^2 per 5 ms period
    double sumHalf = 0.0;
    uint32_t periods = 0;
};

class World {
public:
    World(uint64_t seed, Mode mode);

    Rng rng;
    Mode mode;
    uint64_t phys = 0;
    int64_t startUtcMs = 0;
    int64_t trueUtcMs() const { return startUtcMs + static_cast<int64_t>(phys); }
    UtcSeconds trueUtc() const { return civil::floorDiv(trueUtcMs(), 1000); }

    test::FakeClock boardClock;
    // The guard the board follows while no firmware runs: never beaten, so
    // the keep-alive pin stays low (R33).
    OutputEnableGuard resetGuard{OutputEnableMode::Toggle, 500};
    test::FakeBoard board{resetGuard, boardClock};
    BoardFault boardFault = BoardFault::None;
    void setBoardFault(BoardFault f);

    Flash nvs{*this, "zoneflow"};
    Flash webNvs{*this, "zf_web"};
    uint64_t flashBusyUntil = 0;
    RetainedMemory rtcMem;
    SysTime sys{*this};
    RtcChip rtc{*this};

    std::array<Output, kOutputs> out{};
    int shortedOutput = -1; // the output whose triac failed short
    bool transformer = true;
    double volts = 1.0; // of nominal 24 VAC
    uint32_t mainsMilliHz = 50000;
    bool zeroCrossBridged = false; // AC_ZC follows the keep-alive: 100 Hz
    bool rainContact = false;
    bool spareContact = false;
    double leakLpm = 0.0;
    double flowPulses = 0.0;
    double meterPulsesPerLitre = 450.0;
    bool adcOk = true;
    double noiseMv = 1.0;
    double biasMv = 790.0;
    bool kaPinPlain = true;
    int32_t tripPeakMa = 4600;

    // One 5 ms period: the outputs, the current, the comparator, the flow.
    void physics(uint64_t dtMs = kIsrMs);
    bool energised(std::size_t o) const { return out[o].energised; }
    int energisedCount() const;
    double currentMa() const { return currentMa_; }
    double halfWaveMa() const { return halfMa_; }
    WindowAccumulator window;
    CurrentWindowStats takeWindow(uint32_t seq, MonoMs startMs, MonoMs endMs);

    // A flash write from the control task while an output is energised
    // (firmware architecture section 2: the control task never waits on
    // flash with a valve on).
    std::function<void(const char *)> onControlTaskWrite;

    // Trace and counters.
    Stats stats;
    bool tracing = false;
    std::string traceTail; // the last events, for the report
    void note(const std::string &what);
    void count(const std::string &k, uint64_t n = 1);

private:
    double currentMa_ = 0.0;
    double halfMa_ = 0.0;
    struct GridKey {
        int32_t fullMa;
        int32_t halfMa;
        bool operator<(const GridKey &o) const { return fullMa != o.fullMa ? fullMa < o.fullMa : halfMa < o.halfMa; }
    };
    struct GridStats {
        ChannelStats hi;
        ChannelStats lo;
    };
    std::map<GridKey, GridStats> grid_;
    GridStats gridFor(int32_t fullMa, int32_t halfMa);
};

// The expander bus as the output stage sees it: the simulated board, plus
// transient errors, and the observer of hardware design 19.1 rules 3, 4
// and 10 (relay sequencing, stagger, at most three valve bits low).
class ObservedBus final : public hw::IExpanderBus {
public:
    explicit ObservedBus(World &w) : w_(w) {}
    hw::BusStatus write(uint8_t reg, uint8_t value) override;
    hw::BusStatus read(uint8_t reg, uint8_t &value) override;
    hw::BusStatus probe() override;
    void recover() override;

    double transientRate = 0.0; // share of transactions that fail once
    // True while the firmware has stopped the keep-alive (a fault or trip
    // path): the hardware releases relay and valves in its own order then,
    // so rule 4's sequencing is not judged.
    std::function<bool()> keepAliveStopped;
    std::function<void(const std::string &, const std::string &)> violation;

private:
    uint16_t drivenWithRelay() const;
    World &w_;
    uint64_t lastValveOnMs_ = 0;
    bool anyValveOn_ = false;
    bool relayWasOn_ = false;
    uint64_t relayOnSinceMs_ = 0;
    uint64_t lastValveDrivenMs_ = 0;
    uint16_t lastDriven_ = 0;
};

// Days since 1970-01-01 of a civil date and back: for the schedule oracle.
int64_t daysFromCivil(int y, unsigned m, unsigned d);

} // namespace zf::soak
