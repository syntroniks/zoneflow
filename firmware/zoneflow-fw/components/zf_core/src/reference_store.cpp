// SPDX-License-Identifier: Apache-2.0
#include "zf/hw/reference_store.hpp"

#include "zf/app/retained.hpp"

namespace zf::hw {

namespace {

constexpr std::size_t kSize = 14;

void put16(std::string &s, uint16_t v)
{
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>(v >> 8));
}

void put32(std::string &s, uint32_t v)
{
    put16(s, static_cast<uint16_t>(v & 0xFFFF));
    put16(s, static_cast<uint16_t>(v >> 16));
}

uint16_t get16(std::string_view s, std::size_t at)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(s[at]) | (static_cast<uint8_t>(s[at + 1]) << 8));
}

uint32_t get32(std::string_view s, std::size_t at)
{
    return static_cast<uint32_t>(get16(s, at)) | (static_cast<uint32_t>(get16(s, at + 2)) << 16);
}

} // namespace

std::string BlobReferenceStore::encode(const DetectorReference &r)
{
    std::string s;
    s.reserve(kSize);
    put32(s, kMagic);
    put16(s, kVersion);
    put16(s, r.driveMs);
    put16(s, r.relayMs);
    put32(s, crc32(s.data(), s.size()));
    return s;
}

bool BlobReferenceStore::decode(std::string_view data, DetectorReference &out)
{
    if (data.size() != kSize || get32(data, 0) != kMagic || get16(data, 4) != kVersion ||
        get32(data, 10) != crc32(data.data(), 10)) {
        return false;
    }
    DetectorReference r;
    r.driveMs = get16(data, 6);
    r.relayMs = get16(data, 8);
    if (!r.valid()) {
        return false;
    }
    out = r;
    return true;
}

bool BlobReferenceStore::load(DetectorReference &out)
{
    std::string data;
    return storage_.read(kKey, data) && decode(data, out);
}

bool BlobReferenceStore::save(const DetectorReference &r) { return storage_.write(kKey, encode(r)); }

bool BlobReferenceStore::clear() { return storage_.erase(kKey); }

} // namespace zf::hw
