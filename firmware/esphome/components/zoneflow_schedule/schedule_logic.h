// SPDX-License-Identifier: Apache-2.0
// When a program of the ESPHome package's on-device scheduler is due.
//
// The program itself lives in Home Assistant-visible entities (start times as
// `datetime`, the day rule as a `select`, weekday `switch`es); the component
// turns them into a ProgramSpec and asks this class, once a second, whether a
// start has come. Day rules are zf::DayRule from the standalone firmware's
// scheduler, so both firmwares agree on what "odd days" or "every 3 days" are.
//
// Time is local wall-clock time, as ESPHome's `time` component gives it after
// SNTP or the PCF8563. A start is identified by a key, local day * 1440 +
// minute. The last consumed key is kept in flash, so:
//  - a start runs at most once, also over a reboot or a DST repeated hour;
//  - after a power cut, the newest missed start runs late if it is no older
//    than the grace period, older ones are reported as missed (the policy of
//    the standalone firmware, firmware-architecture.md section 5);
//  - a start inside a spring-forward gap runs at the end of the gap;
//  - a program with no stored key starts from now: no catch-up;
//  - a clock set back by more than 24 h re-arms the program from now.
//
// Framework-free; host-tested in test/host/schedule_logic_test.cpp.
#pragma once

#include <array>
#include <cstdint>

#include "zf/sched/program.hpp"

namespace zfe::sched {

inline constexpr int64_t kNoKey = INT64_MIN;
inline constexpr std::size_t kMaxStarts = 4;
inline constexpr int64_t kMinutesPerDay = 1440;

// The choices of the "days" select, in this order.
enum class DayChoice : uint8_t {
  EveryDay,
  Weekdays,  // Monday to Friday
  Weekends,
  OddDays,
  EvenDays,
  Every2Days,
  Every3Days,
  Custom,  // the seven weekday switches
};
inline constexpr std::size_t kDayChoiceCount = 8;
const char *day_choice_label(DayChoice c);
// -1 if the label is not one of day_choice_label().
int day_choice_from_label(const char *label);

// bit 0 = Sunday ... bit 6 = Saturday, as zf::DayRule.
zf::DayRule day_rule(DayChoice choice, uint8_t custom_mask);

struct ProgramSpec {
  bool enabled = false;
  std::array<int16_t, kMaxStarts> start_minute{-1, -1, -1, -1};  // local minutes after midnight, -1 = unused
  zf::DayRule days{};
};

struct LocalMinute {
  int64_t day = 0;  // local calendar day, days since 1970-01-01
  int minute = 0;   // 0..1439
  int64_t key() const { return day * kMinutesPerDay + minute; }
};
LocalMinute local_minute(int year, unsigned month, unsigned day_of_month, int hour, int minute);

struct Decision {
  bool run = false;        // start the program now
  bool late = false;       // the start it runs was before this minute
  int64_t start_key = kNoKey;  // the start that runs, or the newest one missed
  uint8_t missed = 0;      // starts in the window that will not run
};

// Decides for one program and updates `last_key` (the persisted state).
// grace_min: how late a missed start may still run (0 = never).
Decision evaluate(const ProgramSpec &p, const LocalMinute &now, uint16_t grace_min, int64_t &last_key);

// After an edit of the program (start times, days, enabled): no start whose
// time has passed runs because of the edit (firmware-architecture.md, missed
// starts, rule 6). Never moves the key backwards.
void rearm(const LocalMinute &now, int64_t &last_key);

// The next start strictly after `now` within the next 8 days, or kNoKey.
int64_t next_start(const ProgramSpec &p, const LocalMinute &now);
// Calendar fields of a key, for display.
struct KeyParts {
  int64_t day = 0;
  unsigned weekday = 0;  // 0 = Sunday
  int hour = 0;
  int minute = 0;
};
KeyParts key_parts(int64_t key);

// A program's zone list as typed in its "zones" text entity: "all", empty,
// or numbers separated by commas or spaces ("1, 2, 5"). Bit z set = zone z+1.
// Numbers outside 1..zone_count are ignored; returns 0 if none is left.
uint8_t parse_zone_list(const char *text, uint8_t zone_count);

// Rain delay: until a UTC second, 0 = none.
inline bool rain_delay_active(int64_t until_utc, int64_t now_utc) { return until_utc > now_utc; }

}  // namespace zfe::sched
