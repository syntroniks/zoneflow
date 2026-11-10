// SPDX-License-Identifier: Apache-2.0
// Test doubles for the zf_core interfaces.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "zf/config/config_store.hpp"
#include "zf/hal.hpp"
#include "zf/sched/scheduler.hpp"
#include "zf/time/civil.hpp"
#include "zf/time/clock.hpp"
#include "zf/time/time_service.hpp"

namespace zf::test {

inline UtcSeconds utcOf(int y, unsigned mo, unsigned d, int h = 0, int mi = 0, int s = 0)
{
    return civil::daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

class FakeClock final : public IClock {
public:
    UtcSeconds utc = utcOf(2026, 6, 1);
    MonoMs mono = 1000;

    UtcSeconds nowUtc() const override { return utc; }
    MonoMs monoMs() const override { return mono; }

    // Advances both clocks together.
    void advanceMs(MonoMs ms)
    {
        const MonoMs before = mono;
        mono += ms;
        utc += static_cast<UtcSeconds>(mono / 1000 - before / 1000);
    }
};

class FakeOutputs final : public IOutputHal {
public:
    std::array<bool, kMaxZones> zone{};
    bool master = false;
    int allOffCalls = 0;
    std::vector<std::string> log;

    void setZone(ZoneIndex z, bool on) override
    {
        zone[z] = on;
        log.push_back("z" + std::to_string(z) + (on ? "+" : "-"));
    }
    void setMaster(bool on) override
    {
        master = on;
        log.push_back(on ? "m+" : "m-");
    }
    void allOff() override
    {
        zone.fill(false);
        master = false;
        ++allOffCalls;
        log.push_back("off");
    }
    int openCount() const
    {
        int n = 0;
        for (bool z : zone) {
            n += z ? 1 : 0;
        }
        return n;
    }
    bool anyOn() const { return master || openCount() > 0; }

    int tripClears = 0;
    bool anyOnAtClear = false;
    void clearOverCurrentTrip() override
    {
        ++tripClears;
        anyOnAtClear = anyOnAtClear || anyOn();
        log.push_back("clear");
    }
};

class FakeSensors final : public ISensorHal {
public:
    SensorSample sample{};
    FakeSensors() { sample.acPresent = true; }
    SensorSample read() override { return sample; }
};

class MemoryStorage final : public IBlobStorage {
public:
    std::map<std::string, std::string> data;
    bool failWrites = false;
    int writes = 0;

    bool read(const char *key, std::string &out) override
    {
        auto it = data.find(key);
        if (it == data.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
    bool write(const char *key, std::string_view v) override
    {
        if (failWrites) {
            return false;
        }
        ++writes;
        data[key] = std::string(v);
        return true;
    }
    bool erase(const char *key) override { return data.erase(key) > 0; }
};

class FakeSystemTime final : public ISystemTime {
public:
    UtcSeconds t = 0;
    UtcSeconds now() const override { return t; }
    bool set(UtcSeconds v) override
    {
        t = v;
        return true;
    }
};

class FakeRtc final : public IRtc {
public:
    bool ok = true;
    UtcSeconds t = 0;
    int writes = 0;
    bool read(UtcSeconds &out) override
    {
        out = t;
        return ok;
    }
    bool write(UtcSeconds v) override
    {
        ++writes;
        t = v;
        return ok;
    }
};

struct SchedulerRecorder final : public ISchedulerListener {
    struct Started {
        uint16_t id;
        RunSource source;
        bool late;
    };
    const IClock *clock = nullptr; // when set, start instants are recorded
    std::vector<Started> started;
    std::vector<UtcSeconds> startedAt;
    std::vector<std::pair<uint16_t, SkipReason>> skipped;
    std::vector<std::pair<uint16_t, UtcSeconds>> missed;
    int queueChanges = 0;
    int persistDirty = 0;
    std::vector<UtcSeconds> rainDelays;

    void onProgramStarted(uint16_t id, RunSource s, bool late) override
    {
        started.push_back({id, s, late});
        if (clock != nullptr) {
            startedAt.push_back(clock->nowUtc());
        }
    }
    void onProgramSkipped(uint16_t id, SkipReason r) override { skipped.emplace_back(id, r); }
    void onProgramMissed(uint16_t id, UtcSeconds at) override { missed.emplace_back(id, at); }
    void onQueueChanged() override { ++queueChanges; }
    void onRainDelayChanged(UtcSeconds until) override { rainDelays.push_back(until); }
    void onPersistDirty() override { ++persistDirty; }

    struct Dropped {
        ZoneIndex zone;
        uint16_t programId;
        StartError reason;
    };
    std::vector<Dropped> dropped;
    int pauseExpired = 0;
    void onPauseExpired() override { ++pauseExpired; }
    void onRunDropped(const RunEntry &e, StartError reason) override
    {
        dropped.push_back({e.zone, e.programId, reason});
    }
};

inline Program makeProgram(uint16_t id, std::initializer_list<uint16_t> startMinutes,
                           std::initializer_list<std::pair<ZoneIndex, uint32_t>> durations)
{
    Program p;
    p.id = id;
    p.name.assign("P" + std::to_string(id));
    for (uint16_t m : startMinutes) {
        p.startMinutes[p.startCount++] = m;
    }
    for (const auto &[z, d] : durations) {
        p.durationSec[z] = d;
    }
    return p;
}

} // namespace zf::test
