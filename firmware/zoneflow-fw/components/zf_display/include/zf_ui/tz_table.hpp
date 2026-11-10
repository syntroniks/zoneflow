// SPDX-License-Identifier: Apache-2.0
// Time zones offered on the device. The configuration stores a POSIX TZ
// string (zf::Config::timezone); the web interface can set any valid string,
// this table is the short list the knob can reach.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace zf::ui {

struct TzEntry {
    const char *posix;  // as stored in the configuration
    const char *cities; // proper names, not translated
};

std::size_t tzCount();
const TzEntry &tzEntry(std::size_t index);
// Index of the entry with exactly this string, or -1.
int tzFind(std::string_view posix);
// "+01:00" for the zone's standard time; "?" if the string does not parse.
std::string tzOffsetText(std::string_view posix);

} // namespace zf::ui
