#ifndef SOLVITAIRE_PLATFORM_MEMORY_H
#define SOLVITAIRE_PLATFORM_MEMORY_H

#include <cstddef>
#include <new>      // std::bad_alloc
#include <cstring>  // memset

namespace platform {

#if defined(__APPLE__) || defined(__linux__)

#include <sys/mman.h>

// Allocate `bytes` of virtual memory, zero-filled on first access (demand paging).
// Physical pages are only committed when actually written.
// Returns page-aligned pointer. Throws std::bad_alloc on failure.
inline void* alloc_zeroed(size_t bytes) {
    void* p = mmap(nullptr, bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) throw ::std::bad_alloc();
    return p;
}

// Release memory previously obtained from alloc_zeroed.
inline void release(void* ptr, size_t bytes) {
    munmap(ptr, bytes);
}

// Logically clear the region: subsequent reads will return zero.
// Does NOT wait for physical pages to be reclaimed by the OS.
inline void reset_to_zero(void* ptr, size_t bytes) {
#if defined(__linux__)
    // MADV_DONTNEED on Linux: immediate physical page reclaim, zeroes guaranteed on next read.
    madvise(ptr, bytes, MADV_DONTNEED);
#else
    // On macOS, MADV_FREE/MADV_DONTNEED are advisory only — no zeroing guarantee.
    // MAP_FIXED atomically replaces the region with a fresh zero-filled anonymous mapping.
    void* result = mmap(ptr, bytes, PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
    if (result == MAP_FAILED) {
        // Should not happen in practice (same address, same size, no new reservation needed).
        // Fall back to memset to preserve correctness.
        ::std::memset(ptr, 0, bytes);
    }
#endif
}

inline constexpr bool has_lazy_alloc() { return true; }

#else

// Fallback for unsupported platforms (Windows, etc.).
// flat_cache will use std::vector<cluster> with eager zeroing on these platforms.
// Stubs present so platform_memory.h can be included unconditionally.
inline void* alloc_zeroed(size_t) { return nullptr; }
inline void  release(void*, size_t) {}
inline void  reset_to_zero(void*, size_t) {}
inline constexpr bool has_lazy_alloc() { return false; }

#endif

} // namespace platform

#endif // SOLVITAIRE_PLATFORM_MEMORY_H
