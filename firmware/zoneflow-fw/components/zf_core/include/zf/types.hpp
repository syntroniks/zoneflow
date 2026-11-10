// SPDX-License-Identifier: Apache-2.0
// Shared types and compile-time limits for the ZoneFlow core.
#pragma once

#include <cstddef>
#include <cstdint>

namespace zf {

// Compile-time capacities. The zone count actually fitted on a board is a
// runtime setting bounded by kMaxZones.
inline constexpr std::size_t kMaxZones = 8;
inline constexpr std::size_t kMaxPrograms = 16;
inline constexpr std::size_t kMaxStartTimes = 4;
inline constexpr std::size_t kRunQueueCapacity = 64;

// System spec section 1: up to 2 zones plus master on at once.
inline constexpr uint8_t kMaxConcurrentZonesLimit = 2;

// Upper bound for any configured per-zone maximum run time (4 h). The zone
// driver clamps every request to min(zone max, this).
inline constexpr uint32_t kHardMaxRunSec = 4u * 3600u;

using ZoneIndex = uint8_t;  // 0-based; zone "Z1" on the terminal is index 0
using UtcSeconds = int64_t; // seconds since 1970-01-01T00:00:00Z
using MonoMs = uint64_t;    // monotonic milliseconds since boot

inline constexpr ZoneIndex kNoZone = 0xFF;

} // namespace zf
