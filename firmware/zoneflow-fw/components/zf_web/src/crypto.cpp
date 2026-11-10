// SPDX-License-Identifier: Apache-2.0
#include "zf_web/crypto.hpp"

#include <algorithm>
#include <cstring>

namespace zf::web {
namespace {

constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

} // namespace

void Sha256::reset()
{
    h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    used_ = 0;
    total_ = 0;
}

void Sha256::block(const uint8_t *p)
{
    std::array<uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (uint32_t{p[4 * i]} << 24) | (uint32_t{p[4 * i + 1]} << 16) | (uint32_t{p[4 * i + 2]} << 8) |
               uint32_t{p[4 * i + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const void *data, std::size_t len)
{
    const auto *p = static_cast<const uint8_t *>(data);
    total_ += len;
    while (len > 0) {
        const std::size_t n = std::min(len, buf_.size() - used_);
        std::memcpy(buf_.data() + used_, p, n);
        used_ += n;
        p += n;
        len -= n;
        if (used_ == buf_.size()) {
            block(buf_.data());
            used_ = 0;
        }
    }
}

Sha256Digest Sha256::finish()
{
    const uint64_t bits = total_ * 8;
    const uint8_t pad = 0x80;
    update(&pad, 1);
    const uint8_t zero = 0;
    while (used_ != 56) {
        update(&zero, 1);
    }
    std::array<uint8_t, 8> len{};
    for (std::size_t i = 0; i < 8; ++i) {
        len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    }
    update(len.data(), len.size());
    Sha256Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<uint8_t>(h_[i] >> 24);
        out[4 * i + 1] = static_cast<uint8_t>(h_[i] >> 16);
        out[4 * i + 2] = static_cast<uint8_t>(h_[i] >> 8);
        out[4 * i + 3] = static_cast<uint8_t>(h_[i]);
    }
    return out;
}

Sha256Digest Sha256::hash(const void *data, std::size_t len)
{
    Sha256 s;
    s.update(data, len);
    return s.finish();
}

HmacSha256::HmacSha256(const void *key, std::size_t keyLen)
{
    std::array<uint8_t, 64> k{};
    if (keyLen > k.size()) {
        const Sha256Digest d = Sha256::hash(key, keyLen);
        std::memcpy(k.data(), d.data(), d.size());
    } else if (keyLen > 0) {
        std::memcpy(k.data(), key, keyLen);
    }
    std::array<uint8_t, 64> pad{};
    for (std::size_t i = 0; i < pad.size(); ++i) {
        pad[i] = k[i] ^ 0x36;
    }
    innerStart_.update(pad.data(), pad.size());
    for (std::size_t i = 0; i < pad.size(); ++i) {
        pad[i] = k[i] ^ 0x5c;
    }
    outerStart_.update(pad.data(), pad.size());
    inner_ = innerStart_;
}

Sha256Digest HmacSha256::finish()
{
    const Sha256Digest innerDigest = inner_.finish();
    Sha256 outer = outerStart_;
    outer.update(innerDigest.data(), innerDigest.size());
    inner_ = innerStart_;
    return outer.finish();
}

Sha256Digest HmacSha256::mac(const void *key, std::size_t keyLen, const void *data, std::size_t len)
{
    HmacSha256 h(key, keyLen);
    h.update(data, len);
    return h.finish();
}

void pbkdf2Sha256(std::string_view password, const uint8_t *salt, std::size_t saltLen, uint32_t iterations,
                  uint8_t *out, std::size_t outLen)
{
    // The keyed inner and outer states are computed once; each iteration
    // then costs two compression calls.
    HmacSha256 prf(password.data(), password.size());
    uint32_t blockIndex = 1;
    while (outLen > 0) {
        const std::array<uint8_t, 4> be = {static_cast<uint8_t>(blockIndex >> 24),
                                           static_cast<uint8_t>(blockIndex >> 16),
                                           static_cast<uint8_t>(blockIndex >> 8), static_cast<uint8_t>(blockIndex)};
        prf.update(salt, saltLen);
        prf.update(be.data(), be.size());
        Sha256Digest u = prf.finish();
        Sha256Digest t = u;
        for (uint32_t i = 1; i < iterations; ++i) {
            prf.update(u.data(), u.size());
            u = prf.finish();
            for (std::size_t k = 0; k < t.size(); ++k) {
                t[k] ^= u[k];
            }
        }
        const std::size_t n = std::min(outLen, t.size());
        std::memcpy(out, t.data(), n);
        out += n;
        outLen -= n;
        ++blockIndex;
    }
}

bool constantTimeEqual(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string toHex(const uint8_t *data, std::size_t len)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        s.push_back(kDigits[data[i] >> 4]);
        s.push_back(kDigits[data[i] & 0x0f]);
    }
    return s;
}

bool fromHex(std::string_view hex, uint8_t *out, std::size_t outLen)
{
    if (hex.size() != outLen * 2) {
        return false;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (std::size_t i = 0; i < outLen; ++i) {
        const int hi = nibble(hex[2 * i]);
        const int lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

} // namespace zf::web
