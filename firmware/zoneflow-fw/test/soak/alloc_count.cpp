// SPDX-License-Identifier: Apache-2.0
#include "alloc_count.hpp"

#include <malloc.h>

#include <atomic>
#include <cstdlib>
#include <new>

namespace zf::soak {
bool gInTick = false;
}
namespace {
std::atomic<uint64_t> gAllocs{0};
std::atomic<int64_t> gLive{0};
std::atomic<uint64_t> gTickAllocs{0};
bool gTrap = std::getenv("ZF_SOAK_TRAP_ALLOC") != nullptr;

void *allocate(std::size_t n)
{
    void *p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    if (zf::soak::gInTick) {
        gTickAllocs.fetch_add(1, std::memory_order_relaxed);
        if (gTrap) {
            __builtin_trap();
        }
    }
    gLive.fetch_add(static_cast<int64_t>(malloc_usable_size(p)), std::memory_order_relaxed);
    return p;
}

void release(void *p)
{
    if (p != nullptr) {
        gLive.fetch_sub(static_cast<int64_t>(malloc_usable_size(p)), std::memory_order_relaxed);
        std::free(p);
    }
}
} // namespace

void *operator new(std::size_t n) { return allocate(n); }
void *operator new[](std::size_t n) { return allocate(n); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept
{
    try {
        return allocate(n);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept
{
    try {
        return allocate(n);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void *p) noexcept { release(p); }
void operator delete[](void *p) noexcept { release(p); }
void operator delete(void *p, std::size_t) noexcept { release(p); }
void operator delete[](void *p, std::size_t) noexcept { release(p); }

namespace zf::soak {
uint64_t allocCount() { return gAllocs.load(std::memory_order_relaxed); }
uint64_t tickAllocCount() { return gTickAllocs.load(std::memory_order_relaxed); }
uint64_t liveBytes()
{
    const int64_t v = gLive.load(std::memory_order_relaxed);
    return v < 0 ? 0 : static_cast<uint64_t>(v);
}
} // namespace zf::soak
