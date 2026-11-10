// SPDX-License-Identifier: Apache-2.0
// Admin authentication for the local API: salted password hash, login rate
// limiting and in-memory sessions. Pure C++, host-tested.
//
// Threat model and rationale: docs/design/firmware-security.md.
//
// Threads: every AuthService method may be called from any task; one mutex
// guards the state. PBKDF2 runs while the mutex is held, so concurrent logins
// queue behind each other (intended: it also slows guessing).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "zf/config/config_store.hpp"
#include "zf/types.hpp"
#include "zf_web/crypto.hpp"

namespace zf::web {

class IRandom {
public:
    virtual ~IRandom() = default;
    virtual void fill(uint8_t *out, std::size_t len) = 0;
};

// ---- password hash -----------------------------------------------------------

inline constexpr std::size_t kMinPasswordLen = 8;
inline constexpr std::size_t kMaxPasswordLen = 64;
// PBKDF2 iterations. Each costs two SHA-256 compressions. Not measured on the
// ESP32-S3 yet; see firmware-security.md for the estimate.
inline constexpr uint32_t kDefaultPbkdf2Iterations = 10000;

struct PasswordRecord {
    uint32_t iterations = 0;
    std::array<uint8_t, 16> salt{};
    Sha256Digest hash{};

    // "pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>"
    std::string encode() const;
    static std::optional<PasswordRecord> decode(std::string_view s);
    static PasswordRecord make(std::string_view password, uint32_t iterations, IRandom &rng);
    bool verify(std::string_view password) const;
};

// Length 8..64 bytes, no control characters.
bool passwordAcceptable(std::string_view password);

// ---- rate limiting -------------------------------------------------------------

// Failed attempts (login, setup code, password change) per client and in
// total. Per client: 5 free failures, then a lock of 1 s doubling per further
// failure, capped at 15 min. In total: 30 failures within 5 min lock every
// client out for 60 s, which bounds guessing from many spoofed addresses.
// The global lock spares the last 8 client addresses that proved the
// password since boot (security review F2): without that, a LAN device
// rotating through more addresses than there are slots keeps the owner out
// for as long as it keeps failing. Their own per-client lock still applies.
class LoginLimiter {
public:
    static constexpr std::size_t kClients = 16;
    static constexpr std::size_t kKnownClients = 8;
    static constexpr uint32_t kFreeFailures = 5;
    static constexpr uint32_t kBaseLockMs = 1000;
    static constexpr uint32_t kMaxLockMs = 15u * 60u * 1000u;
    static constexpr std::size_t kGlobalFailures = 30;
    static constexpr uint32_t kGlobalWindowMs = 5u * 60u * 1000u;
    static constexpr uint32_t kGlobalLockMs = 60u * 1000u;

    // 0 if an attempt is allowed now, else milliseconds to wait.
    uint32_t retryAfterMs(uint64_t client, MonoMs now) const;
    void failure(uint64_t client, MonoMs now);
    // Clears the client's failures and remembers it as known.
    void success(uint64_t client);
    // New password or factory reset: no client has proved it yet.
    void forgetKnown();

private:
    struct Entry {
        uint64_t client = 0;
        uint32_t failures = 0;
        MonoMs lockedUntil = 0;
        MonoMs lastSeen = 0;
        bool used = false;
    };
    Entry *find(uint64_t client);
    const Entry *find(uint64_t client) const;
    bool known(uint64_t client) const;

    std::array<Entry, kClients> entries_{};
    std::array<uint64_t, kKnownClients> known_{};
    std::size_t knownCount_ = 0;
    std::size_t knownNext_ = 0;
    std::array<MonoMs, kGlobalFailures> recent_{};
    std::size_t recentNext_ = 0;
    std::size_t recentCount_ = 0;
    MonoMs globalLockedUntil_ = 0;
};

// Client key for the limiter: FNV-1a 64 of the peer address text.
uint64_t clientKey(std::string_view peerAddress);

// ---- sessions ------------------------------------------------------------------

struct Session {
    std::string token; // 32 hex chars, 128 random bits; cookie or bearer value
    std::string csrf;  // 32 hex chars; must accompany cookie-authenticated writes
    MonoMs createdMs = 0;
    MonoMs lastUsedMs = 0;
};

class SessionTable {
public:
    static constexpr std::size_t kMaxSessions = 8;
    static constexpr MonoMs kIdleTimeoutMs = 24ull * 3600ull * 1000ull;
    static constexpr MonoMs kAbsoluteTimeoutMs = 30ull * 24ull * 3600ull * 1000ull;

    // Evicts the least recently used session when full.
    Session create(MonoMs now, IRandom &rng);
    // Expired sessions are removed. Updates the last-used time.
    std::optional<Session> use(std::string_view token, MonoMs now);
    void remove(std::string_view token);
    void clear();
    std::size_t count() const;

private:
    std::array<std::optional<Session>, kMaxSessions> slots_{};
};

// ---- service -------------------------------------------------------------------

enum class AuthError : uint8_t {
    Ok,
    BadCredentials, // wrong password or setup code
    RateLimited,
    NotSet,       // no admin password yet: use setup
    AlreadySet,   // setup refused: a password exists
    WeakPassword, // fails passwordAcceptable()
    StorageError,
};

const char *toString(AuthError e);

struct AuthOutcome {
    AuthError error = AuthError::Ok;
    uint32_t retryAfterS = 0;
    Session session; // valid when error == Ok and a session was created
};

struct Principal {
    bool authenticated = false;
    bool viaCookie = false; // cookie sessions need the CSRF header on writes
    std::string csrf;
};

class AuthService {
public:
    static constexpr const char *kPasswordKey = "admin_pw";

    AuthService(IBlobStorage &storage, IRandom &rng, uint32_t iterations = kDefaultPbkdf2Iterations)
        : storage_(storage), rng_(rng), iterations_(iterations)
    {
    }

    // Reads the stored password record. A malformed record counts as unset
    // and is reported by returning false.
    bool load();
    bool passwordSet() const;

    AuthOutcome login(uint64_t client, std::string_view password, MonoMs now);
    // First access: the per-device setup code proves physical access.
    AuthOutcome setup(uint64_t client, std::string_view expectedCode, std::string_view givenCode,
                      std::string_view password, MonoMs now);
    // Signs out every session and returns a new one for the caller.
    AuthOutcome changePassword(uint64_t client, std::string_view current, std::string_view next, MonoMs now);
    // Provisioning (BLE or SoftAP setup page) may set the first password.
    AuthError setInitialPassword(std::string_view password);

    Principal authenticate(std::string_view cookieToken, std::string_view bearerToken, MonoMs now);
    void logout(std::string_view token);
    // Factory reset: forget the password and every session.
    void erase();

    std::size_t sessionCount() const;

private:
    AuthError storeLocked(std::string_view password);

    IBlobStorage &storage_;
    IRandom &rng_;
    uint32_t iterations_;
    mutable std::mutex mutex_;
    std::optional<PasswordRecord> record_;
    LoginLimiter limiter_;
    SessionTable sessions_;
};

} // namespace zf::web
