// SPDX-License-Identifier: Apache-2.0
// The controller on the simulated rev A board with the current measurement
// as the device delivers it: raw 100 ms windows (count, sums, extremes) of
// both current channels, GPIO7 and VIN_MON, processed by CurrentSense.
#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "fake_board.hpp"
#include "zf/app/controller.hpp"

namespace zf::test {

class WindowSensors final : public ISensorHal {
public:
    explicit WindowSensors(FakeBoard &b) : board_(b) {}
    SensorSample read() override
    {
        SensorSample s;
        s.acPresent = board_.acPresent;
        s.currentWindow = board_.windowStats();
        s.overCurrentTripped = board_.latched;
        s.mainsMilliHz = 50000;
        s.sensorSupplyOn = true;
        s.dieTempKnown = true;
        s.dieTempC = 35.0f;
        return s;
    }

private:
    FakeBoard &board_;
};

struct WindowRig {
    FakeClock clock;
    OutputEnableGuard guard{OutputEnableMode::Toggle, 500};
    FakeBoard board{guard, clock};
    FakeReferenceStore reference = StageRig::storeWith(true);
    hw::OutputStage stage{board, guard, clock, revALayout(), hw::OutputStageConfig{}, &reference};
    WindowSensors sensors{board};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    std::unique_ptr<Controller> ctrl;
    std::vector<Event> events;

    explicit WindowRig(const std::function<void(Config &)> &tweak = {}, IRetainedStore *retained = nullptr)
    {
        clock.mono = 0;
        bus.subscribe([](const Event &e, void *ctx) { static_cast<std::vector<Event> *>(ctx)->push_back(e); }, &events);
        Config c = Config::defaults();
        c.master.enabled = false;
        if (tweak) {
            tweak(c);
        }
        EXPECT_TRUE(config.update(c).ok);
        ctrl = std::make_unique<Controller>(
            ControllerDeps{clock, stage, sensors, guard, bus, state, config, &storage, retained});
        ctrl->begin();
        ctrl->setTimeStatus(TimeStatus::Synced);
    }
    void run(uint64_t ms)
    {
        for (uint64_t t = 0; t < ms; t += 5) {
            clock.advanceMs(5);
            board.step5ms();
            if (clock.mono % 50 == 0) {
                ctrl->tick();
            }
        }
    }
    bool has(FaultCode c) const { return (ctrl->activeFaults() & (1u << static_cast<unsigned>(c))) != 0; }
    template <typename Pred> bool runUntil(Pred pred, uint64_t limitMs)
    {
        for (uint64_t t = 0; t < limitMs && !pred(); t += 5) {
            run(5);
        }
        return pred();
    }
    bool raised(FaultCode c) const
    {
        return std::any_of(events.begin(), events.end(),
                           [c](const Event &e) { return e.type == EventType::FaultRaised && e.fault.code == c; });
    }
};

} // namespace zf::test
