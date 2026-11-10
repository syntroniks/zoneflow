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

// ---- Security review: vectors beyond the original set ------------------------------
// Each expected value is the published one (FIPS 180-2 appendix B.3 for the
// 896-bit message, RFC 4231 cases 3, 4, 5 and 7, the PBKDF2-HMAC-SHA256
// vectors in the RFC 6070 style used by many libraries) and was recomputed
// with Python 3 hashlib/hmac. The remaining ones are Python-only and cover the
// padding and key-length boundaries.

TEST(Sha256, Fips896BitMessage)
{
    EXPECT_EQ(
        sha("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrst"
            "nopqrstu"),
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

// Every message length from 0 to 200 bytes, so every padding case (55, 56,
// 63, 64, 119, 120 bytes ...) is hit. Byte i of a message is (7 i + 3) mod 256;
// the 201 digests are concatenated and hashed once more.
TEST(Sha256, EveryLengthUpTo200Bytes)
{
    Sha256 all;
    for (std::size_t n = 0; n <= 200; ++n) {
        std::vector<uint8_t> m(n);
        for (std::size_t i = 0; i < n; ++i) {
            m[i] = static_cast<uint8_t>(i * 7 + 3);
        }
        const Sha256Digest d = Sha256::hash(m.data(), m.size());
        all.update(d.data(), d.size());
    }
    EXPECT_EQ(hex(all.finish()), "3275febb4612d86d586eb9f11cd21e648a9fc7d95e0f6b8d362786e28c9c5b79");
}

TEST(HmacSha256, Rfc4231MoreCases)
{
    const std::string k3(20, '\xaa');
    const std::string d3(50, '\xdd');
    EXPECT_EQ(hex(HmacSha256::mac(k3.data(), k3.size(), d3.data(), d3.size())),
              "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    std::string k4;
    for (int i = 1; i <= 25; ++i) {
        k4.push_back(static_cast<char>(i));
    }
    const std::string d4(50, '\xcd');
    EXPECT_EQ(hex(HmacSha256::mac(k4.data(), k4.size(), d4.data(), d4.size())),
              "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    const std::string k5(20, '\x0c');
    EXPECT_EQ(hex(HmacSha256::mac(k5.data(), k5.size(), "Test With Truncation", 20)).substr(0, 32),
              "a3b6167473100ee06e0c796c2955552b");
    const std::string k7(131, '\xaa');
    const std::string d7 = "This is a test using a larger than block-size key and a larger than block-size data. The "
                           "key needs to be hashed before being used by the HMAC algorithm.";
    EXPECT_EQ(hex(HmacSha256::mac(k7.data(), k7.size(), d7.data(), d7.size())),
              "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

// A key of exactly one block is used as is; one byte more is hashed first.
TEST(HmacSha256, KeyLengthBoundary)
{
    std::string k;
    for (int i = 0; i < 65; ++i) {
        k.push_back(static_cast<char>(i));
    }
    const std::string d = "block-size key";
    EXPECT_EQ(hex(HmacSha256::mac(k.data(), 64, d.data(), d.size())),
              "1dad230598e011a4e4eabc6c8da8f55ef9a66a8881d1e16e23ea116ae28231ec");
    EXPECT_EQ(hex(HmacSha256::mac(k.data(), 65, d.data(), d.size())),
              "d0b754c1861695b26bca3bd3a8581ef73f46ba28ada969cf73dbdb39d991eb2d");
}

TEST(Pbkdf2Sha256, MorePublishedVectors)
{
    EXPECT_EQ(pbkdf2("password", "salt", 1, 32), "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    EXPECT_EQ(pbkdf2("password", "salt", 2, 32), "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    EXPECT_EQ(pbkdf2("passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 4096, 40),
              "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
    EXPECT_EQ(pbkdf2(std::string_view("pass\0word", 9), std::string_view("sa\0lt", 5), 4096, 16),
              "89b69d0516f829893c696226650a8687");
}

// A password longer than the SHA-256 block becomes a hashed HMAC key.
TEST(Pbkdf2Sha256, LongPassword)
{
    EXPECT_EQ(pbkdf2(std::string(100, 'x'), "0123456789abcdef", 1000, 32),
              "15d47c0768c0818b8f6d0160b7a5f0f697256000890573df3df8a6de1060fe51");
}
