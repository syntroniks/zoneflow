// SPDX-License-Identifier: Apache-2.0
// SHA-256, HMAC-SHA-256 and PBKDF2-HMAC-SHA-256 (FIPS 180-4, RFC 2104,
// RFC 8018). Portable C++ so the device and the host tests run the same code;
// checked against the published test vectors in test/host/crypto_test.cpp.
// Used for the admin password hash and the TLS certificate fingerprint. No
// table lookup or branch depends on secret data. Key-derived state is wiped
// when it goes out of scope, so a core dump taken later does not hold it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace zf::web {

using Sha256Digest = std::array<uint8_t, 32>;

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void *data, std::size_t len);
    Sha256Digest finish();

    static Sha256Digest hash(const void *data, std::size_t len);

    // Zeroes the state (also when it was keyed), then reset().
    void wipe();

private:
    friend class HmacSha256;
    void block(const uint8_t *p);

    std::array<uint32_t, 8> h_{};
    std::array<uint8_t, 64> buf_{};
    std::size_t used_ = 0;
    uint64_t total_ = 0;
};

class HmacSha256 {
public:
    HmacSha256(const void *key, std::size_t keyLen);
    ~HmacSha256();
    HmacSha256(const HmacSha256 &) = delete;
    HmacSha256 &operator=(const HmacSha256 &) = delete;
    void update(const void *data, std::size_t len) { inner_.update(data, len); }
    Sha256Digest finish();

    static Sha256Digest mac(const void *key, std::size_t keyLen, const void *data, std::size_t len);

private:
    Sha256 innerStart_;
    Sha256 outerStart_;
    Sha256 inner_;
};

// Derives `outLen` bytes. `iterations` >= 1.
void pbkdf2Sha256(std::string_view password, const uint8_t *salt, std::size_t saltLen, uint32_t iterations,
                  uint8_t *out, std::size_t outLen);

// Zeroes memory in a way the compiler does not remove.
void secureZero(void *p, std::size_t len);

// Compares in time that depends only on the lengths.
bool constantTimeEqual(std::string_view a, std::string_view b);

std::string toHex(const uint8_t *data, std::size_t len);
// False on odd length or a non-hex character.
bool fromHex(std::string_view hex, uint8_t *out, std::size_t outLen);

} // namespace zf::web
