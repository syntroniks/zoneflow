// SPDX-License-Identifier: Apache-2.0
#include "zf_web/auth.hpp"

#include <algorithm>
#include <cstdlib>

namespace zf::web {

// ---- password hash -----------------------------------------------------------

std::string PasswordRecord::encode() const
{
    return "pbkdf2-sha256$" + std::to_string(iterations) + "$" + toHex(salt.data(), salt.size()) + "$" +
           toHex(hash.data(), hash.size());
}

std::optional<PasswordRecord> PasswordRecord::decode(std::string_view s)
{
    constexpr std::string_view kPrefix = "pbkdf2-sha256$";
    if (s.substr(0, kPrefix.size()) != kPrefix) {
        return std::nullopt;
    }
    s.remove_prefix(kPrefix.size());
    const std::size_t a = s.find('$');
    if (a == std::string_view::npos || a == 0 || a > 9) {
        return std::nullopt;
    }
    PasswordRecord r;
    uint64_t iter = 0;
    for (char c : s.substr(0, a)) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        iter = iter * 10 + static_cast<uint64_t>(c - '0');
    }
    if (iter == 0 || iter > 10000000) {
        return std::nullopt;
    }
    r.iterations = static_cast<uint32_t>(iter);
    s.remove_prefix(a + 1);
    const std::size_t b = s.find('$');
    if (b == std::string_view::npos || !fromHex(s.substr(0, b), r.salt.data(), r.salt.size()) ||
        !fromHex(s.substr(b + 1), r.hash.data(), r.hash.size())) {
        return std::nullopt;
    }
    return r;
}

PasswordRecord PasswordRecord::make(std::string_view password, uint32_t iterations, IRandom &rng)
{
    PasswordRecord r;
    r.iterations = iterations;
    rng.fill(r.salt.data(), r.salt.size());
    pbkdf2Sha256(password, r.salt.data(), r.salt.size(), iterations, r.hash.data(), r.hash.size());
    return r;
}

bool PasswordRecord::verify(std::string_view password) const
{
    Sha256Digest d{};
    pbkdf2Sha256(password, salt.data(), salt.size(), iterations, d.data(), d.size());
    return constantTimeEqual(std::string_view(reinterpret_cast<const char *>(d.data()), d.size()),
                             std::string_view(reinterpret_cast<const char *>(hash.data()), hash.size()));
}

bool passwordAcceptable(std::string_view password)
{
    if (password.size() < kMinPasswordLen || password.size() > kMaxPasswordLen) {
        return false;
    }
    return std::none_of(password.begin(), password.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7f;
    });
}

// ---- rate limiting -------------------------------------------------------------

LoginLimiter::Entry *LoginLimiter::find(uint64_t client)
{
    for (Entry &e : entries_) {
        if (e.used && e.client == client) {
            return &e;
        }
    }
    return nullptr;
}

const LoginLimiter::Entry *LoginLimiter::find(uint64_t client) const
{
    return const_cast<LoginLimiter *>(this)->find(client);
}

bool LoginLimiter::known(uint64_t client) const
{
    return std::find(known_.begin(), known_.begin() + knownCount_, client) != known_.begin() + knownCount_;
}

uint32_t LoginLimiter::retryAfterMs(uint64_t client, MonoMs now) const
{
    MonoMs until = known(client) ? 0 : globalLockedUntil_;
    if (const Entry *e = find(client)) {
        until = std::max(until, e->lockedUntil);
    }
    return until > now ? static_cast<uint32_t>(until - now) : 0;
}

void LoginLimiter::failure(uint64_t client, MonoMs now)
{
    Entry *e = find(client);
    if (e == nullptr) {
        // Take a free slot, else the least recently seen one.
        e = &entries_[0];
        for (Entry &cand : entries_) {
            if (!cand.used) {
                e = &cand;
                break;
            }
            if (cand.lastSeen < e->lastSeen) {
                e = &cand;
            }
        }
        *e = Entry{};
        e->used = true;
        e->client = client;
    }
    e->lastSeen = now;
    ++e->failures;
    if (e->failures >= kFreeFailures) {
        const uint32_t extra = std::min<uint32_t>(e->failures - kFreeFailures, 20);
        const uint64_t lock = std::min<uint64_t>(uint64_t{kBaseLockMs} << extra, kMaxLockMs);
        e->lockedUntil = now + lock;
    }

    recent_[recentNext_] = now;
    recentNext_ = (recentNext_ + 1) % kGlobalFailures;
    recentCount_ = std::min(recentCount_ + 1, kGlobalFailures);
    if (recentCount_ == kGlobalFailures) {
        const MonoMs oldest = recent_[recentNext_]; // ring is full: next slot holds the oldest
        if (now - oldest < kGlobalWindowMs) {
            globalLockedUntil_ = now + kGlobalLockMs;
        }
    }
}

void LoginLimiter::success(uint64_t client)
{
    if (Entry *e = find(client)) {
        *e = Entry{};
    }
    if (!known(client)) {
        known_[knownNext_] = client;
        knownNext_ = (knownNext_ + 1) % kKnownClients;
        knownCount_ = std::min(knownCount_ + 1, kKnownClients);
    }
}

void LoginLimiter::forgetKnown()
{
    knownCount_ = 0;
    knownNext_ = 0;
}

uint64_t clientKey(std::string_view peerAddress)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (char c : peerAddress) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001b3ull;
    }
    return h;
}

// ---- sessions ------------------------------------------------------------------

namespace {

std::string randomHex(IRandom &rng, std::size_t bytes)
{
    std::array<uint8_t, 32> buf{};
    bytes = std::min(bytes, buf.size());
    rng.fill(buf.data(), bytes);
    return toHex(buf.data(), bytes);
}

bool expired(const Session &s, MonoMs now)
{
    return now - s.lastUsedMs > SessionTable::kIdleTimeoutMs || now - s.createdMs > SessionTable::kAbsoluteTimeoutMs;
}

} // namespace

Session SessionTable::create(MonoMs now, IRandom &rng)
{
    Session s;
    s.token = randomHex(rng, 16);
    s.csrf = randomHex(rng, 16);
    s.createdMs = now;
    s.lastUsedMs = now;
    std::optional<Session> *slot = &slots_[0];
    for (auto &cand : slots_) {
        if (!cand.has_value() || expired(*cand, now)) {
            slot = &cand;
            break;
        }
        if (cand->lastUsedMs < (*slot)->lastUsedMs) {
            slot = &cand;
        }
    }
    *slot = s;
    return s;
}

std::optional<Session> SessionTable::use(std::string_view token, MonoMs now)
{
    if (token.empty()) {
        return std::nullopt;
    }
    for (auto &slot : slots_) {
        if (slot.has_value() && constantTimeEqual(slot->token, token)) {
            if (expired(*slot, now)) {
                slot.reset();
                return std::nullopt;
            }
            slot->lastUsedMs = now;
            return slot;
        }
    }
    return std::nullopt;
}

bool SessionTable::valid(std::string_view token, MonoMs now) const
{
    if (token.empty()) {
        return false;
    }
    return std::any_of(slots_.begin(), slots_.end(), [&](const std::optional<Session> &slot) {
        return slot.has_value() && constantTimeEqual(slot->token, token) && !expired(*slot, now);
    });
}

void SessionTable::remove(std::string_view token)
{
    for (auto &slot : slots_) {
        if (slot.has_value() && constantTimeEqual(slot->token, token)) {
            slot.reset();
        }
    }
}

void SessionTable::clear()
{
    for (auto &slot : slots_) {
        slot.reset();
    }
}

std::size_t SessionTable::count() const
{
    return static_cast<std::size_t>(
        std::count_if(slots_.begin(), slots_.end(), [](const auto &s) { return s.has_value(); }));
}

// ---- service -------------------------------------------------------------------

const char *toString(AuthError e)
{
    switch (e) {
    case AuthError::Ok:
        return "ok";
    case AuthError::BadCredentials:
        return "bad_credentials";
    case AuthError::RateLimited:
        return "rate_limited";
    case AuthError::NotSet:
        return "password_not_set";
    case AuthError::AlreadySet:
        return "password_already_set";
    case AuthError::WeakPassword:
        return "weak_password";
    case AuthError::StorageError:
        return "storage_error";
    }
    return "?";
}

bool AuthService::load()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string stored;
    record_.reset();
    if (!storage_.read(kPasswordKey, stored)) {
        return true;
    }
    record_ = PasswordRecord::decode(stored);
    return record_.has_value();
}

bool AuthService::passwordSet() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return record_.has_value();
}

AuthError AuthService::storeLocked(std::string_view password)
{
    if (!passwordAcceptable(password)) {
        return AuthError::WeakPassword;
    }
    const PasswordRecord r = PasswordRecord::make(password, iterations_, rng_);
    if (!storage_.write(kPasswordKey, r.encode())) {
        return AuthError::StorageError;
    }
    record_ = r;
    sessions_.clear();
    limiter_.forgetKnown();
    return AuthError::Ok;
}

namespace {

AuthOutcome limited(uint32_t ms)
{
    AuthOutcome o;
    o.error = AuthError::RateLimited;
    o.retryAfterS = (ms + 999) / 1000;
    return o;
}

} // namespace

AuthOutcome AuthService::login(uint64_t client, std::string_view password, MonoMs now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (const uint32_t wait = limiter_.retryAfterMs(client, now); wait > 0) {
        return limited(wait);
    }
    AuthOutcome o;
    if (!record_.has_value()) {
        o.error = AuthError::NotSet;
        return o;
    }
    if (password.size() > kMaxPasswordLen || !record_->verify(password)) {
        limiter_.failure(client, now);
        o.error = AuthError::BadCredentials;
        return o;
    }
    limiter_.success(client);
    o.session = sessions_.create(now, rng_);
    return o;
}

AuthOutcome AuthService::setup(uint64_t client, std::string_view expectedCode, std::string_view givenCode,
                               std::string_view password, MonoMs now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    AuthOutcome o;
    if (record_.has_value()) {
        o.error = AuthError::AlreadySet;
        return o;
    }
    if (const uint32_t wait = limiter_.retryAfterMs(client, now); wait > 0) {
        return limited(wait);
    }
    if (expectedCode.empty() || !constantTimeEqual(expectedCode, givenCode)) {
        limiter_.failure(client, now);
        o.error = AuthError::BadCredentials;
        return o;
    }
    o.error = storeLocked(password);
    if (o.error == AuthError::Ok) {
        limiter_.success(client);
        o.session = sessions_.create(now, rng_);
    }
    return o;
}

AuthOutcome AuthService::changePassword(uint64_t client, std::string_view current, std::string_view next, MonoMs now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (const uint32_t wait = limiter_.retryAfterMs(client, now); wait > 0) {
        return limited(wait);
    }
    AuthOutcome o;
    if (!record_.has_value()) {
        o.error = AuthError::NotSet;
        return o;
    }
    if (current.size() > kMaxPasswordLen || !record_->verify(current)) {
        limiter_.failure(client, now);
        o.error = AuthError::BadCredentials;
        return o;
    }
    o.error = storeLocked(next);
    if (o.error == AuthError::Ok) {
        limiter_.success(client);
        o.session = sessions_.create(now, rng_);
    }
    return o;
}

AuthError AuthService::setInitialPassword(std::string_view password)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (record_.has_value()) {
        return AuthError::AlreadySet;
    }
    return storeLocked(password);
}

Principal AuthService::authenticate(std::string_view cookieToken, std::string_view bearerToken, MonoMs now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Principal p;
    if (!bearerToken.empty()) {
        if (auto s = sessions_.use(bearerToken, now)) {
            p.authenticated = true;
            p.csrf = s->csrf;
        }
        return p; // a bad bearer token is not retried as a cookie
    }
    if (auto s = sessions_.use(cookieToken, now)) {
        p.authenticated = true;
        p.viaCookie = true;
        p.csrf = s->csrf;
    }
    return p;
}

bool AuthService::sessionValid(std::string_view cookieToken, std::string_view bearerToken, MonoMs now) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.valid(bearerToken.empty() ? cookieToken : bearerToken, now);
}

void AuthService::logout(std::string_view token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.remove(token);
}

void AuthService::erase()
{
    std::lock_guard<std::mutex> lock(mutex_);
    storage_.erase(kPasswordKey);
    record_.reset();
    sessions_.clear();
    limiter_.forgetKnown();
}

std::size_t AuthService::sessionCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.count();
}

} // namespace zf::web
