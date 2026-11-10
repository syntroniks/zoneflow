// SPDX-License-Identifier: Apache-2.0
#include "zf/app/retained.hpp"

#include <cstddef>

namespace zf {

uint32_t crc32(const void *data, std::size_t len, uint32_t crc)
{
    // CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320), bitwise: the
    // record is a few hundred bytes, written once per control tick.
    const auto *p = static_cast<const uint8_t *>(data);
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

namespace {
uint32_t bodyCrc(const RetainedRecord &r) { return crc32(&r, offsetof(RetainedRecord, crc)); }
} // namespace

void seal(RetainedRecord &r)
{
    r.magic = RetainedRecord::kMagic;
    r.version = RetainedRecord::kVersion;
    r.size = static_cast<uint16_t>(sizeof(RetainedRecord));
    r.crc = bodyCrc(r);
    r.crcInverted = ~r.crc;
}

bool intact(const RetainedRecord &r)
{
    return r.magic == RetainedRecord::kMagic && r.version == RetainedRecord::kVersion &&
           r.size == sizeof(RetainedRecord) && r.crc == ~r.crcInverted && r.crc == bodyCrc(r);
}

} // namespace zf
