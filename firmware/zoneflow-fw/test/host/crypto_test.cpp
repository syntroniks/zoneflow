// SPDX-License-Identifier: Apache-2.0
// SHA-256 (FIPS 180-4 examples), HMAC-SHA-256 (RFC 4231 cases 1, 2, 6) and
// PBKDF2-HMAC-SHA-256 (RFC 7914 section 11 and the common 4096-round vector).
// Every expected value was also recomputed with Python's hashlib/hmac.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "zf_web/crypto.hpp"

using namespace zf::web;

namespace {

std::string hex(const Sha256Digest &d) { return toHex(d.data(), d.size()); }

std::string sha(std::string_view s) { return hex(Sha256::hash(s.data(), s.size())); }

std::string pbkdf2(std::string_view p, std::string_view s, uint32_t c, std::size_t len)
{
    std::vector<uint8_t> out(len);
    pbkdf2Sha256(p, reinterpret_cast<const uint8_t *>(s.data()), s.size(), c, out.data(), out.size());
    return toHex(out.data(), out.size());
}

} // namespace

TEST(Sha256, FipsExamples)
{
    EXPECT_EQ(sha("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, MillionAInOddChunks)
{
    Sha256 s;
    const std::string chunk(997, 'a');
    std::size_t left = 1000000;
    while (left > 0) {
        const std::size_t n = std::min(left, chunk.size());
        s.update(chunk.data(), n);
        left -= n;
    }
    EXPECT_EQ(hex(s.finish()), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(HmacSha256, Rfc4231)
{
    const std::string k1(20, '\x0b');
    EXPECT_EQ(hex(HmacSha256::mac(k1.data(), k1.size(), "Hi There", 8)),
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    const std::string d2 = "what do ya want for nothing?";
    EXPECT_EQ(hex(HmacSha256::mac("Jefe", 4, d2.data(), d2.size())),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    const std::string k6(131, '\xaa');
    const std::string d6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    EXPECT_EQ(hex(HmacSha256::mac(k6.data(), k6.size(), d6.data(), d6.size())),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST(Pbkdf2Sha256, KnownVectors)
{
    EXPECT_EQ(pbkdf2("passwd", "salt", 1, 64), "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
                                               "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    EXPECT_EQ(pbkdf2("password", "salt", 4096, 32), "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
}

TEST(Pbkdf2Sha256, Rfc7914SecondVector)
{
    EXPECT_EQ(pbkdf2("Password", "NaCl", 80000, 64),
              "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
              "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
}

TEST(Hex, RoundTripAndRejects)
{
    uint8_t b[3] = {};
    EXPECT_TRUE(fromHex("00fFa5", b, 3));
    EXPECT_EQ(toHex(b, 3), "00ffa5");
    EXPECT_FALSE(fromHex("00ff", b, 3));
    EXPECT_FALSE(fromHex("00ffzz", b, 3));
}

TEST(ConstantTimeEqual, Basics)
{
    EXPECT_TRUE(constantTimeEqual("abc", "abc"));
    EXPECT_FALSE(constantTimeEqual("abc", "abd"));
    EXPECT_FALSE(constantTimeEqual("abc", "abcd"));
    EXPECT_TRUE(constantTimeEqual("", ""));
}
