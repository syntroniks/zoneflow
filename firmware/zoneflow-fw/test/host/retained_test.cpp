// SPDX-License-Identifier: Apache-2.0
// State kept over a software restart: integrity check of the record and the
// zone run budget (firmware security residual risk 10).
#include <gtest/gtest.h>

#include <cstring>

#include "fakes.hpp"
#include "zf/app/retained.hpp"
#include "zf/zone/zone_driver.hpp"

using namespace zf;
using namespace zf::test;

TEST(Retained, Crc32KnownValue)
{
    // CRC-32/ISO-HDLC check value.
    EXPECT_EQ(crc32("123456789", 9), 0xCBF43926u);
}

TEST(Retained, SealedRecordIsIntactAndAnyChangeIsNot)
{
    RetainedRecord r;
    r.burstMs[3] = 1234;
    seal(r);
    EXPECT_TRUE(intact(r));
    for (std::size_t byte = 0; byte < sizeof r; ++byte) {
        RetainedRecord c = r;
        reinterpret_cast<uint8_t *>(&c)[byte] ^= 0x10;
        EXPECT_FALSE(intact(c)) << "byte " << byte;
    }
}

TEST(Retained, PowerOnGarbageIsRejected)
{
    RetainedRecord r;
    std::memset(&r, 0xA5, sizeof r);
    EXPECT_FALSE(intact(r));
    std::memset(&r, 0x00, sizeof r);
    EXPECT_FALSE(intact(r));
}

TEST(Retained, NoPaddingInTheRecord)
{
    // Every byte is covered by a field, so the CRC is deterministic.
    EXPECT_EQ(sizeof(RetainedRecord), 4u + 2 + 2 + 4 * 8 + 4 * 8 + 4 + 4 * 3 * 9 + 4 + 4);
}

namespace {
ZoneDriverConfig cfg()
{
    ZoneDriverConfig c;
    c.zoneEnabled.fill(true);
    c.maxRunSec.fill(600);
    c.minRestSec = 1800;
    return c;
}
} // namespace

TEST(Retained, RestartDoesNotShortenTheRest)
{
    FakeClock clock;
    FakeOutputs out;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    ZoneDriver drv{out, oe, clock};
    drv.init(cfg());
    ASSERT_TRUE(drv.start(0, 600).ok());
    for (int i = 0; i < 610; ++i) {
        clock.advanceMs(1000);
        drv.tick();
    }
    ASSERT_EQ(drv.startBlocker(0), StartError::Resting);
    clock.advanceMs(600'000); // 10 of 30 minutes rested
    const auto saved = drv.exportBudget();
    EXPECT_GE(saved[0].burstMs, 600'000u);
    EXPECT_NEAR(saved[0].closedForMs, 610'000, 2000);

    // Restart: a new driver, the clock starts again.
    FakeClock clock2;
    clock2.mono = 50;
    ZoneDriver drv2{out, oe, clock2};
    drv2.init(cfg());
    EXPECT_EQ(drv2.startBlocker(0), StartError::Ok); // without the budget the rest is lost
    drv2.restoreBudget(saved);
    EXPECT_EQ(drv2.startBlocker(0), StartError::Resting);
    EXPECT_NEAR(drv2.restRemainingMs(0), 1'190'000, 2000);
    EXPECT_EQ(drv2.start(0, 60).error, StartError::Resting);
    clock2.advanceMs(1'200'000);
    EXPECT_EQ(drv2.startBlocker(0), StartError::Ok);
    EXPECT_TRUE(drv2.start(0, 60).ok());
}

TEST(Retained, ZoneOpenAtRestartKeepsItsOnTime)
{
    FakeClock clock;
    FakeOutputs out;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    ZoneDriver drv{out, oe, clock};
    drv.init(cfg());
    ASSERT_TRUE(drv.start(1, 600).ok());
    clock.advanceMs(500'000);
    drv.tick();
    const auto saved = drv.exportBudget();
    EXPECT_EQ(saved[1].burstMs, 500'000u);
    EXPECT_EQ(saved[1].closedForMs, 0u);

    FakeClock clock2;
    ZoneDriver drv2{out, oe, clock2};
    drv2.init(cfg());
    drv2.restoreBudget(saved);
    // 100 s of the 600 s maximum are left.
    EXPECT_EQ(drv2.start(1, 600).acceptedSec, 100u);
}
