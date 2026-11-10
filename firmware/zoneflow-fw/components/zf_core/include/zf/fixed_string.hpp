// SPDX-License-Identifier: Apache-2.0
// Fixed-capacity string with no heap use, for names stored in config.
#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

namespace zf {

template <std::size_t N> class FixedString {
public:
    constexpr FixedString() = default;
    FixedString(std::string_view s) { assign(s); } // NOLINT(google-explicit-constructor)

    // Copies at most N bytes. Truncation never splits a UTF-8 sequence.
    void assign(std::string_view s)
    {
        std::size_t len = s.size() < N ? s.size() : N;
        if (len < s.size()) {
            while (len > 0 && (static_cast<unsigned char>(s[len]) & 0xC0u) == 0x80u) {
                --len;
            }
        }
        std::memcpy(buf_, s.data(), len);
        buf_[len] = '\0';
        len_ = len;
    }

    std::string_view view() const { return {buf_, len_}; }
    const char *c_str() const { return buf_; }
    std::size_t size() const { return len_; }
    bool empty() const { return len_ == 0; }
    static constexpr std::size_t capacity() { return N; }

    friend bool operator==(const FixedString &a, const FixedString &b) { return a.view() == b.view(); }

private:
    char buf_[N + 1] = {};
    std::size_t len_ = 0;
};

} // namespace zf
