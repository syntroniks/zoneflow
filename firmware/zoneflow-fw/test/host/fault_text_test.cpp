// SPDX-License-Identifier: Apache-2.0
// The one table of fault code to user text (zf/fault_text.hpp) against the
// core's codes and the display's string table. The web UI's copy is checked
// by web/tests/unit/fault-text.test.ts, which reads the same header.
#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <string>

#include "zf/fault_text.hpp"
#include "zf_ui/faults.hpp"

using namespace zf;
using namespace zf::ui;

namespace {

std::string withLabel(std::string_view format, const char *label)
{
    std::string out(format);
    const std::size_t at = out.find("%s");
    if (at != std::string::npos) {
        out.replace(at, 2, label);
    }
    return out;
}

} // namespace

TEST(FaultText, EveryCodeHasOneRowWithItsStableCode)
{
    std::set<std::string_view> ids;
    for (unsigned i = 1; i < static_cast<unsigned>(FaultCode::Count); ++i) {
        const auto code = static_cast<FaultCode>(i);
        const FaultText *t = faultText(code);
        ASSERT_NE(t, nullptr) << i;
        EXPECT_EQ(t->id, toString(code)) << "the REST/MQTT code and the table agree";
        EXPECT_FALSE(t->title.empty());
        EXPECT_TRUE(t->zoneTitle.empty() || t->zoneTitle.find("%s") != std::string_view::npos) << t->id;
        EXPECT_EQ(t->title.find('!'), std::string_view::npos) << "no exclamation marks";
        ids.insert(t->id);
    }
    EXPECT_EQ(ids.size(), kFaultTextCount);
    EXPECT_EQ(faultText(FaultCode::None), nullptr);
}

// The display's titles are the table's: the plain title for a fault that
// belongs to no output, the zone title with "Zone 3" for one that does, and
// "Master valve" for a zone-type fault no zone carries.
TEST(FaultText, DisplayTitlesAreTheTablesTitles)
{
    for (const FaultText &t : kFaultTexts) {
        const std::string none = faultTitle(FaultItem{t.code, kNoZone});
        if (t.zoneTitle.empty()) {
            EXPECT_EQ(none, t.title) << t.id;
            continue;
        }
        EXPECT_EQ(faultTitle(FaultItem{t.code, 2}), withLabel(t.zoneTitle, "Zone 3")) << t.id;
        if (t.code == FaultCode::OverCurrentTrip) {
            EXPECT_EQ(none, t.title) << "a trip with no output to blame is not the master's";
        } else {
            EXPECT_EQ(none, withLabel(t.zoneTitle, "Master valve")) << t.id;
        }
    }
}

// The core blames a hardware trip on the zone switched on just before it and
// also sets the system bit. The display listed it twice, once without the
// zone. Now it is one item, with the zone.
TEST(FaultText, TripCarriedByAZoneIsListedOnce)
{
    SystemState s{};
    s.zones[2].fault = FaultCode::OverCurrentTrip;
    s.activeFaults = 1u << static_cast<unsigned>(FaultCode::OverCurrentTrip);
    const std::vector<FaultItem> items = listFaults(s);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(faultTitle(items[0]), "Zone 3: protection tripped");
}

TEST(FaultText, StageChecksMapToTheirCodes)
{
    EXPECT_EQ(faultCodeFor(HwCheck::RelayWelded), FaultCode::RelayWelded);
    EXPECT_EQ(faultCodeFor(HwCheck::OutputShorted), FaultCode::OutputShorted);
    EXPECT_EQ(faultCodeFor(HwCheck::LatchStuck), FaultCode::TripLatchStuck);
    EXPECT_EQ(faultCodeFor(HwCheck::IdleDriveEnabled), FaultCode::KeepAliveDetector);
    EXPECT_EQ(faultCodeFor(HwCheck::BusError), FaultCode::OutputExpander);
    EXPECT_EQ(faultCodeFor(HwCheck::None), FaultCode::None);
}
