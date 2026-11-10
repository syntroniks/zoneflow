// SPDX-License-Identifier: Apache-2.0
#include "schedule_logic.h"

#include <cstring>

#include "zf/time/civil.hpp"

namespace zfe::sched {
namespace {
constexpr const char *kLabels[kDayChoiceCount] = {
    "Every day", "Weekdays", "Weekends", "Odd days", "Even days", "Every 2 days", "Every 3 days", "Custom",
};
}  // namespace

const char *day_choice_label(DayChoice c) {
  const auto i = static_cast<std::size_t>(c);
  return i < kDayChoiceCount ? kLabels[i] : "Every day";
}

int day_choice_from_label(const char *label) {
  if (label == nullptr)
    return -1;
  for (std::size_t i = 0; i < kDayChoiceCount; ++i) {
    if (std::strcmp(label, kLabels[i]) == 0)
      return static_cast<int>(i);
  }
  return -1;
}

zf::DayRule day_rule(DayChoice choice, uint8_t custom_mask) {
  zf::DayRule r;
  r.type = zf::DayRuleType::Weekdays;
  switch (choice) {
    case DayChoice::EveryDay:
      r.weekdayMask = 0x7F;
      break;
    case DayChoice::Weekdays:
      r.weekdayMask = 0x3E;  // Monday (bit 1) to Friday (bit 5)
      break;
    case DayChoice::Weekends:
      r.weekdayMask = 0x41;  // Sunday and Saturday
      break;
    case DayChoice::OddDays:
      r.type = zf::DayRuleType::OddDays;
      break;
    case DayChoice::EvenDays:
      r.type = zf::DayRuleType::EvenDays;
      break;
    case DayChoice::Every2Days:
    case DayChoice::Every3Days:
      // Anchored on 1970-01-01, so the days do not move when the device
      // restarts or the program is edited.
      r.type = zf::DayRuleType::Interval;
      r.intervalDays = choice == DayChoice::Every2Days ? 2 : 3;
      r.intervalAnchorDay = 0;
      break;
    case DayChoice::Custom:
      r.weekdayMask = static_cast<uint8_t>(custom_mask & 0x7F);
      break;
  }
  return r;
}

LocalMinute local_minute(int year, unsigned month, unsigned day_of_month, int hour, int minute) {
  LocalMinute m;
  m.day = zf::civil::daysFromCivil(year, month, day_of_month);
  m.minute = hour * 60 + minute;
  return m;
}

void rearm(const LocalMinute &now, int64_t &last_key) {
  if (last_key == kNoKey || last_key < now.key())
    last_key = now.key();
}

Decision evaluate(const ProgramSpec &p, const LocalMinute &now, uint16_t grace_min, int64_t &last_key) {
  Decision d;
  const int64_t now_key = now.key();
  if (last_key == kNoKey) {
    last_key = now_key;  // never ran here: no catch-up, no missed reports
    return d;
  }
  if (last_key > now_key + kMinutesPerDay) {
    last_key = now_key;  // the clock went back more than a day: re-arm from now
    return d;
  }
  if (!p.enabled) {
    rearm(now, last_key);  // enabling it later never runs a start that passed meanwhile
    return d;
  }

  // Starts in (last_key, now_key], no older than 24 h: today's and yesterday's.
  int64_t newest = kNoKey;
  uint8_t count = 0;
  for (int64_t day = now.day - 1; day <= now.day; ++day) {
    if (!p.days.matches(day))
      continue;
    for (std::size_t i = 0; i < kMaxStarts; ++i) {
      const int16_t s = p.start_minute[i];
      if (s < 0 || s >= kMinutesPerDay)
        continue;
      bool duplicate = false;  // a time listed twice counts once
      for (std::size_t j = 0; j < i; ++j)
        duplicate = duplicate || p.start_minute[j] == s;
      if (duplicate)
        continue;
      const int64_t key = day * kMinutesPerDay + s;
      if (key > now_key || key <= last_key || key <= now_key - kMinutesPerDay)
        continue;
      ++count;
      if (key > newest)
        newest = key;
    }
  }
  if (newest == kNoKey)
    return d;
  d.start_key = newest;
  last_key = newest;
  if (now_key - newest <= static_cast<int64_t>(grace_min)) {
    d.run = true;
    d.late = newest != now_key;
    d.missed = static_cast<uint8_t>(count - 1);
  } else {
    d.missed = count;
  }
  return d;
}

uint8_t parse_zone_list(const char *text, uint8_t zone_count) {
  const uint8_t all = static_cast<uint8_t>(zone_count >= 8 ? 0xFF : (1u << zone_count) - 1u);
  if (text == nullptr)
    return all;
  const char *c = text;
  while (*c == ' ')
    ++c;
  if (*c == '\0' || std::strcmp(c, "all") == 0 || std::strcmp(c, "All") == 0)
    return all;
  uint8_t mask = 0;
  int value = -1;
  for (;; ++c) {
    if (*c >= '0' && *c <= '9') {
      value = (value < 0 ? 0 : value * 10) + (*c - '0');
      if (value > 99)
        value = 99;
      continue;
    }
    if (value >= 1 && value <= zone_count)
      mask |= static_cast<uint8_t>(1u << (value - 1));
    value = -1;
    if (*c == '\0')
      break;
  }
  return mask;
}

int64_t next_start(const ProgramSpec &p, const LocalMinute &now) {
  if (!p.enabled)
    return kNoKey;
  const int64_t now_key = now.key();
  for (int64_t day = now.day; day <= now.day + 8; ++day) {
    if (!p.days.matches(day))
      continue;
    int64_t best = kNoKey;
    for (const int16_t s : p.start_minute) {
      if (s < 0 || s >= kMinutesPerDay)
        continue;
      const int64_t key = day * kMinutesPerDay + s;
      if (key > now_key && (best == kNoKey || key < best))
        best = key;
    }
    if (best != kNoKey)
      return best;
  }
  return kNoKey;
}

KeyParts key_parts(int64_t key) {
  KeyParts k;
  k.day = zf::civil::floorDiv(key, kMinutesPerDay);
  const auto minute = static_cast<int>(key - k.day * kMinutesPerDay);
  k.weekday = zf::civil::weekdayFromDays(k.day);
  k.hour = minute / 60;
  k.minute = minute % 60;
  return k;
}

}  // namespace zfe::sched
