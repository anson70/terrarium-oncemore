#include "alloc_guard.h"

#include <atomic>
#include <cstdlib>
#include <new>


namespace tfx::desktop {

namespace {
thread_local bool   g_armed = false;
std::atomic<size_t> g_count{0};
} // namespace

void   allocGuardArm() { g_armed = true; }
void   allocGuardDisarm() { g_armed = false; }
size_t allocGuardCount() { return g_count.load(); }
void   allocGuardReset() { g_count.store(0); }

namespace detail {
inline void note() {
    if (g_armed) g_count.fetch_add(1, std::memory_order_relaxed);
}
} // namespace detail

} // namespace tfx::desktop

// Replacements for every global allocation function. Plain forms use
// malloc/free; aligned forms use alignedAlloc/alignedFree, because on MSVC
// memory from _aligned_malloc must never reach free(). The compiler always
// pairs an aligned new with an aligned delete, so the two families never mix.
// The nothrow forms must be replaced too, or memory from the library's own
// nothrow new (used e.g. by std::stable_sort) would reach our free().

using tfx::desktop::alignedAlloc;
using tfx::desktop::alignedFree;

void* operator new(std::size_t size) {
    tfx::desktop::detail::note();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    tfx::desktop::detail::note();
    return std::malloc(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void* operator new(std::size_t size, std::align_val_t align) {
    tfx::desktop::detail::note();
    if (void* p = alignedAlloc(static_cast<std::size_t>(align), size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t align) { return ::operator new(size, align); }

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    tfx::desktop::detail::note();
    return alignedAlloc(static_cast<std::size_t>(align), size);
}
void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, align, tag);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

void operator delete(void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { alignedFree(p); }
