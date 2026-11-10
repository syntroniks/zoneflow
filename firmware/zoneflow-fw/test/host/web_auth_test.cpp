// SPDX-License-Identifier: Apache-2.0
// Admin password, login rate limiting and sessions.
#include <gtest/gtest.h>

#include "fakes.hpp"
#include "web_fakes.hpp"
#include "zf_web/auth.hpp"

using namespace zf;
using namespace zf::web;
using zf::test::MemoryStorage;
using zf::test::SeqRandom;

TEST(PasswordRecord, EncodeDecodeVerify)
{
    SeqRandom rng;
    const PasswordRecord r = PasswordRecord::make("correct horse", 10, rng);
    const std::string enc = r.encode();
    EXPECT_EQ(enc.rfind("pbkdf2-sha256$10$", 0), 0u);
    const auto back = PasswordRecord::decode(enc);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->verify("correct horse"));
    EXPECT_FALSE(back->verify("correct horsf"));
    EXPECT_FALSE(back->verify(""));
}

TEST(PasswordRecord, RejectsMalformed)
{
    EXPECT_FALSE(PasswordRecord::decode("").has_value());
    EXPECT_FALSE(PasswordRecord::decode("md5$1$00$00").has_value());
    EXPECT_FALSE(
        PasswordRecord::decode("pbkdf2-sha256$0$" + std::string(32, '0') + "$" + std::string(64, '0')).has_value());
    EXPECT_FALSE(
        PasswordRecord::decode("pbkdf2-sha256$10$" + std::string(31, '0') + "$" + std::string(64, '0')).has_value());
    EXPECT_FALSE(
        PasswordRecord::decode("pbkdf2-sha256$x$" + std::string(32, '0') + "$" + std::string(64, '0')).has_value());
}

TEST(PasswordRecord, SaltDiffersPerRecord)
{
    SeqRandom rng;
    const PasswordRecord a = PasswordRecord::make("same password", 2, rng);
    const PasswordRecord b = PasswordRecord::make("same password", 2, rng);
    EXPECT_NE(a.salt, b.salt);
    EXPECT_NE(a.hash, b.hash);
}

TEST(PasswordRules, LengthAndControlCharacters)
{
    EXPECT_FALSE(passwordAcceptable("short"));
    EXPECT_TRUE(passwordAcceptable("eight ch"));
    EXPECT_TRUE(passwordAcceptable(std::string(64, 'x')));
    EXPECT_FALSE(passwordAcceptable(std::string(65, 'x')));
    EXPECT_FALSE(passwordAcceptable("with\nnewline"));
    EXPECT_TRUE(passwordAcceptable("ünïcødé pass"));
}

TEST(LoginLimiter, FiveFreeFailuresThenDoublingLock)
{
    LoginLimiter l;
    const uint64_t c = clientKey("192.168.1.20");
    MonoMs now = 1000;
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(l.retryAfterMs(c, now), 0u);
        l.failure(c, now);
    }
    EXPECT_EQ(l.retryAfterMs(c, now), 0u);
    l.failure(c, now); // 5th
    EXPECT_EQ(l.retryAfterMs(c, now), 1000u);
    now += 1000;
    EXPECT_EQ(l.retryAfterMs(c, now), 0u);
    l.failure(c, now); // 6th
    EXPECT_EQ(l.retryAfterMs(c, now), 2000u);
    now += 2000;
    l.failure(c, now); // 7th
    EXPECT_EQ(l.retryAfterMs(c, now), 4000u);
    // Another client is not affected.
    EXPECT_EQ(l.retryAfterMs(clientKey("192.168.1.21"), now), 0u);
    l.success(c);
    EXPECT_EQ(l.retryAfterMs(c, now), 0u);
}

TEST(LoginLimiter, LockIsCapped)
{
    LoginLimiter l;
    const uint64_t c = clientKey("10.0.0.1");
    MonoMs now = 0;
    for (int i = 0; i < 40; ++i) {
        now += l.retryAfterMs(c, now);
        l.failure(c, now);
    }
    EXPECT_EQ(l.retryAfterMs(c, now), LoginLimiter::kMaxLockMs);
}

TEST(LoginLimiter, GlobalLockAgainstManyAddresses)
{
    LoginLimiter l;
    MonoMs now = 0;
    for (std::size_t i = 0; i < LoginLimiter::kGlobalFailures; ++i) {
        l.failure(clientKey("10.0.1." + std::to_string(i)), now);
        now += 1000; // 30 failures in 30 s, each from a fresh address
    }
    EXPECT_GT(l.retryAfterMs(clientKey("10.9.9.9"), now), 0u);
    EXPECT_EQ(l.retryAfterMs(clientKey("10.9.9.9"), now + LoginLimiter::kGlobalLockMs), 0u);
}

TEST(LoginLimiter, SlowFailuresDoNotTripGlobalLock)
{
    LoginLimiter l;
    MonoMs now = 0;
    for (std::size_t i = 0; i < 2 * LoginLimiter::kGlobalFailures; ++i) {
        l.failure(clientKey("10.0.2." + std::to_string(i)), now);
        now += 20000; // 3 a minute
    }
    EXPECT_EQ(l.retryAfterMs(clientKey("10.9.9.9"), now), 0u);
}

// Security review F2. A LAN device that rotates through more addresses than
// the limiter has slots (16) keeps the global lock up for as long as it likes:
// LRU eviction forgets each address's failures. The lock must not keep out a
// client that has already proved the password since boot.
TEST(LoginLimiter, GlobalLockSparesAClientThatLoggedInBefore)
{
    LoginLimiter l;
    const uint64_t owner = clientKey("192.168.1.50");
    l.success(owner);
    MonoMs now = 1000;
    for (std::size_t i = 0; i < LoginLimiter::kGlobalFailures; ++i) {
        l.failure(clientKey("10.0.3." + std::to_string(i)), now);
    }
    EXPECT_GT(l.retryAfterMs(clientKey("10.9.9.9"), now), 0u); // strangers stay locked
    EXPECT_EQ(l.retryAfterMs(owner, now), 0u);
    // The owner's own failures still lock the owner's address.
    for (uint32_t i = 0; i < LoginLimiter::kFreeFailures; ++i) {
        l.failure(owner, now);
    }
    EXPECT_GT(l.retryAfterMs(owner, now), 0u);
}

TEST(Sessions, CreateUseExpireEvict)
{
    SeqRandom rng;
    SessionTable t;
    const Session s = t.create(0, rng);
    EXPECT_EQ(s.token.size(), 32u);
    EXPECT_EQ(s.csrf.size(), 32u);
    EXPECT_NE(s.token, s.csrf);
    EXPECT_TRUE(t.use(s.token, 1000).has_value());
    EXPECT_FALSE(t.use("nope", 1000).has_value());
    EXPECT_FALSE(t.use("", 1000).has_value());
    // Idle timeout.
    EXPECT_FALSE(t.use(s.token, 1000 + SessionTable::kIdleTimeoutMs + 1).has_value());
    EXPECT_EQ(t.count(), 0u);

    // Full table evicts the least recently used session.
    std::vector<Session> all;
    for (std::size_t i = 0; i < SessionTable::kMaxSessions; ++i) {
        all.push_back(t.create(i, rng));
    }
    ASSERT_TRUE(t.use(all[0].token, 100).has_value()); // all[1] is now the oldest
    const Session extra = t.create(200, rng);
    EXPECT_EQ(t.count(), SessionTable::kMaxSessions);
    EXPECT_FALSE(t.use(all[1].token, 201).has_value());
    EXPECT_TRUE(t.use(all[0].token, 201).has_value());
    EXPECT_TRUE(t.use(extra.token, 201).has_value());
}

TEST(Sessions, AbsoluteTimeout)
{
    SeqRandom rng;
    SessionTable t;
    const Session s = t.create(0, rng);
    MonoMs now = 0;
    while (now < SessionTable::kAbsoluteTimeoutMs) {
        now += SessionTable::kIdleTimeoutMs / 2;
        if (now <= SessionTable::kAbsoluteTimeoutMs) {
            ASSERT_TRUE(t.use(s.token, now).has_value());
        }
    }
    EXPECT_FALSE(t.use(s.token, SessionTable::kAbsoluteTimeoutMs + 1).has_value());
}

class AuthServiceTest : public ::testing::Test {
protected:
    MemoryStorage storage;
    SeqRandom rng;
    AuthService auth{storage, rng, 3};
    const uint64_t client = clientKey("192.168.1.50");
};

TEST_F(AuthServiceTest, SetupNeedsSetupCodeAndStoresSaltedHash)
{
    ASSERT_TRUE(auth.load());
    EXPECT_FALSE(auth.passwordSet());
    EXPECT_EQ(auth.login(client, "whatever1", 0).error, AuthError::NotSet);
    EXPECT_EQ(auth.setup(client, "ABCDEFGHJK", "ABCDEFGHJX", "admin pass", 0).error, AuthError::BadCredentials);
    EXPECT_EQ(auth.setup(client, "ABCDEFGHJK", "ABCDEFGHJK", "short", 0).error, AuthError::WeakPassword);
    const AuthOutcome o = auth.setup(client, "ABCDEFGHJK", "ABCDEFGHJK", "admin pass", 0);
    ASSERT_EQ(o.error, AuthError::Ok);
    EXPECT_FALSE(o.session.token.empty());
    // Stored as a salted hash, never the password itself.
    const std::string stored = storage.data.at(AuthService::kPasswordKey);
    EXPECT_EQ(stored.find("admin pass"), std::string::npos);
    EXPECT_EQ(stored.rfind("pbkdf2-sha256$3$", 0), 0u);
    // Second setup refused.
    EXPECT_EQ(auth.setup(client, "ABCDEFGHJK", "ABCDEFGHJK", "other pass", 0).error, AuthError::AlreadySet);
    // Survives a reload.
    AuthService again(storage, rng, 3);
    ASSERT_TRUE(again.load());
    EXPECT_EQ(again.login(client, "admin pass", 0).error, AuthError::Ok);
}

TEST_F(AuthServiceTest, EmptySetupCodeNeverMatches)
{
    EXPECT_EQ(auth.setup(client, "", "", "admin pass", 0).error, AuthError::BadCredentials);
}

TEST_F(AuthServiceTest, LoginRateLimited)
{
    ASSERT_EQ(auth.setInitialPassword("admin pass"), AuthError::Ok);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(auth.login(client, "wrong pass", 0).error, AuthError::BadCredentials);
    }
    const AuthOutcome limited = auth.login(client, "admin pass", 0);
    EXPECT_EQ(limited.error, AuthError::RateLimited);
    EXPECT_EQ(limited.retryAfterS, 1u);
    EXPECT_EQ(auth.login(client, "admin pass", 1000).error, AuthError::Ok);
}

// Security review F2, end to end: 17 rotating addresses keep everyone else
// locked out, but the owner who logged in earlier still gets in.
TEST_F(AuthServiceTest, OwnerLogsInDuringAGlobalLockout)
{
    ASSERT_EQ(auth.setInitialPassword("admin pass"), AuthError::Ok);
    ASSERT_EQ(auth.login(client, "admin pass", 0).error, AuthError::Ok);
    MonoMs now = 0;
    for (int round = 0; round < 3; ++round) {
        now += LoginLimiter::kGlobalLockMs / 2;
        for (int a = 0; a < 17; ++a) {
            auth.login(clientKey("10.0.4." + std::to_string(a)), "wrong pass", now);
        }
    }
    EXPECT_EQ(auth.login(clientKey("192.168.1.77"), "admin pass", now).error, AuthError::RateLimited);
    EXPECT_EQ(auth.login(client, "admin pass", now).error, AuthError::Ok);
}

// A new password ends the exemption of addresses that proved the old one.
TEST_F(AuthServiceTest, PasswordChangeForgetsOtherKnownClients)
{
    ASSERT_EQ(auth.setInitialPassword("admin pass"), AuthError::Ok);
    const uint64_t other = clientKey("192.168.1.51");
    ASSERT_EQ(auth.login(other, "admin pass", 0).error, AuthError::Ok);
    ASSERT_EQ(auth.changePassword(client, "admin pass", "new password", 0).error, AuthError::Ok);
    for (std::size_t i = 0; i < LoginLimiter::kGlobalFailures; ++i) {
        auth.login(clientKey("10.0.5." + std::to_string(i)), "wrong pass", 10);
    }
    EXPECT_EQ(auth.login(other, "new password", 10).error, AuthError::RateLimited);
    EXPECT_EQ(auth.login(client, "new password", 10).error, AuthError::Ok);
}

TEST_F(AuthServiceTest, AuthenticateCookieAndBearer)
{
    ASSERT_EQ(auth.setInitialPassword("admin pass"), AuthError::Ok);
    const AuthOutcome o = auth.login(client, "admin pass", 0);
    const Principal viaCookie = auth.authenticate(o.session.token, "", 10);
    EXPECT_TRUE(viaCookie.authenticated);
    EXPECT_TRUE(viaCookie.viaCookie);
    EXPECT_EQ(viaCookie.csrf, o.session.csrf);
    const Principal viaBearer = auth.authenticate("", o.session.token, 10);
    EXPECT_TRUE(viaBearer.authenticated);
    EXPECT_FALSE(viaBearer.viaCookie);
    // A wrong bearer is not rescued by a good cookie.
    EXPECT_FALSE(auth.authenticate(o.session.token, "bad", 10).authenticated);
    auth.logout(o.session.token);
    EXPECT_FALSE(auth.authenticate(o.session.token, "", 10).authenticated);
}

TEST_F(AuthServiceTest, ChangePasswordSignsOutEveryone)
{
    ASSERT_EQ(auth.setInitialPassword("admin pass"), AuthError::Ok);
    const AuthOutcome a = auth.login(client, "admin pass", 0);
    const AuthOutcome b = auth.login(clientKey("192.168.1.51"), "admin pass", 0);
    EXPECT_EQ(auth.changePassword(client, "wrong pass", "new password", 0).error, AuthError::BadCredentials);
    EXPECT_EQ(auth.changePassword(client, "admin pass", "short", 0).error, AuthError::WeakPassword);
    const AuthOutcome c = auth.changePassword(client, "admin pass", "new password", 0);
    ASSERT_EQ(c.error, AuthError::Ok);
    EXPECT_FALSE(auth.authenticate(a.session.token, "", 1).authenticated);
    EXPECT_FALSE(auth.authenticate(b.session.token, "", 1).authenticated);
    EXPECT_TRUE(auth.authenticate(c.session.token, "", 1).authenticated);
    EXPECT_EQ(auth.login(client, "admin pass", 2).error, AuthError::BadCredentials);
    EXPECT_EQ(auth.login(client, "new password", 2).error, AuthError::Ok);
}

TEST_F(AuthServiceTest, CorruptRecordCountsAsUnsetAndEraseForgets)
{
    storage.data[AuthService::kPasswordKey] = "garbage";
    EXPECT_FALSE(auth.load());
    EXPECT_FALSE(auth.passwordSet());
    ASSERT_EQ(auth.setInitialPassword("admin pass"), AuthError::Ok);
    EXPECT_EQ(auth.setInitialPassword("other pass"), AuthError::AlreadySet);
    auth.erase();
    EXPECT_FALSE(auth.passwordSet());
    EXPECT_EQ(storage.data.count(AuthService::kPasswordKey), 0u);
}

TEST_F(AuthServiceTest, StorageFailureLeavesPasswordUnset)
{
    storage.failWrites = true;
    EXPECT_EQ(auth.setInitialPassword("admin pass"), AuthError::StorageError);
    EXPECT_FALSE(auth.passwordSet());
}
