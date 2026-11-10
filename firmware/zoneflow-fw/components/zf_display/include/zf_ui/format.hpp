// SPDX-License-Identifier: Apache-2.0
// Text formatting for the display. All wording comes from the string table.
#pragma once

#include <cstdint>
#include <string>

#include "zf/time/time_zone.hpp"
#include "zf/types.hpp"
#include "zf_ui/strings.hpp"

namespace zf::ui {

// printf with a format string from the table.
std::string fmt(Str id, ...);

// "06:00" (24-hour clock).
std::string clockText(int minuteOfDay);
// Time left on a run: "12:34", from one hour up "1:05:00".
std::string countdownText(uint32_t sec);
// A length of time: "45 s", "10 min", "2:30 min", "1 h 30 min".
std::string durationText(uint32_t sec);
// "Tue 10 Nov"
std::string dateText(const civil::LocalDateTime &local);
// The day of `at` as seen from `now`: "Today", "Tomorrow", "Thu", and from a
// week ahead "Thu 19 Nov".
std::string dayText(UtcSeconds at, UtcSeconds now, const TimeZone &tz);
// dayText plus the time: "Tomorrow 06:00".
std::string whenText(UtcSeconds at, UtcSeconds now, const TimeZone &tz);
// The same for a list row, where room is short: tomorrow is its weekday
// ("Wed 06:00") and a date has no weekday ("19 Nov 06:00").
std::string whenShortText(UtcSeconds at, UtcSeconds now, const TimeZone &tz);
// "Mo We Fr", "Every day", "None". Monday first; mask bit 0 = Sunday.
std::string weekdaysText(uint8_t mask);
std::string currentText(int32_t milliamp); // "320 mA"; negative: "Not measured"
std::string flowText(float litresPerMinute);
std::string litresText(float litres);
std::string percentText(int percent);
Str weekdayShort(unsigned weekday); // 0 = Sunday -> Str::Sun
Str weekdayTwo(unsigned weekday);   // 0 = Sunday -> Str::Su

} // namespace zf::ui
