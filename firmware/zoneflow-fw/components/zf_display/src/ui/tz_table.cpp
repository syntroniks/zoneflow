// SPDX-License-Identifier: Apache-2.0
// POSIX TZ strings copied from the footer line of the TZif files of the IANA
// time zone database, release 2026f (/usr/share/zoneinfo/<Area>/<City>, last
// line). Zones with the same rules share an entry. Ordered west to east.
#include "zf_ui/tz_table.hpp"

#include <cstdio>
#include <cstdlib>

#include "zf/time/time_zone.hpp"

namespace zf::ui {
namespace {

constexpr TzEntry kZones[] = {
    {"HST10", "Honolulu"},
    {"AKST9AKDT,M3.2.0,M11.1.0", "Anchorage"},
    {"PST8PDT,M3.2.0,M11.1.0", "Los Angeles"},
    {"MST7", "Phoenix"},
    {"MST7MDT,M3.2.0,M11.1.0", "Denver"},
    {"CST6CDT,M3.2.0,M11.1.0", "Chicago"},
    {"CST6", "Mexico City"},
    {"EST5EDT,M3.2.0,M11.1.0", "New York"},
    {"<-05>5", "Bogota"},
    {"AST4ADT,M3.2.0,M11.1.0", "Halifax"},
    {"<-04>4<-03>,M9.1.6/24,M4.1.6/24", "Santiago"},
    {"<-03>3", "Sao Paulo"},
    {"<-01>1<+00>,M3.5.0/0,M10.5.0/1", "Azores"},
    {"UTC0", "UTC"},
    {"GMT0BST,M3.5.0/1,M10.5.0", "London, Lisbon"},
    {"CET-1CEST,M3.5.0,M10.5.0/3", "Berlin, Paris"},
    {"WAT-1", "Lagos"},
    {"EET-2EEST,M3.5.0/3,M10.5.0/4", "Athens, Helsinki"},
    {"EET-2EEST,M4.5.5/0,M10.5.4/24", "Cairo"},
    {"SAST-2", "Johannesburg"},
    {"IST-2IDT,M3.4.4/26,M10.5.0", "Jerusalem"},
    {"<+03>-3", "Istanbul, Riyadh"},
    {"<+0330>-3:30", "Tehran"},
    {"<+04>-4", "Dubai"},
    {"PKT-5", "Karachi"},
    {"IST-5:30", "Kolkata"},
    {"<+06>-6", "Dhaka"},
    {"<+07>-7", "Bangkok"},
    {"CST-8", "Shanghai, Perth"},
    {"JST-9", "Tokyo, Seoul"},
    {"ACST-9:30ACDT,M10.1.0,M4.1.0/3", "Adelaide"},
    {"AEST-10", "Brisbane"},
    {"AEST-10AEDT,M10.1.0,M4.1.0/3", "Sydney"},
    {"NZST-12NZDT,M9.5.0,M4.1.0/3", "Auckland"},
};

} // namespace

std::size_t tzCount() { return sizeof kZones / sizeof kZones[0]; }

const TzEntry &tzEntry(std::size_t index) { return kZones[index < tzCount() ? index : 0]; }

int tzFind(std::string_view posix)
{
    for (std::size_t i = 0; i < tzCount(); ++i) {
        if (posix == kZones[i].posix) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string tzOffsetText(std::string_view posix)
{
    const auto tz = TimeZone::parse(posix);
    if (!tz) {
        return "?";
    }
    const int offset = tz->standardOffset();
    char buf[12];
    std::snprintf(buf, sizeof buf, "%c%02d:%02d", offset < 0 ? '-' : '+', std::abs(offset) / 3600,
                  (std::abs(offset) / 60) % 60);
    return buf;
}

} // namespace zf::ui
