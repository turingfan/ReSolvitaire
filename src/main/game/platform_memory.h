#ifndef SOLVITAIRE_PLATFORM_MEMORY_H
#define SOLVITAIRE_PLATFORM_MEMORY_H

#include <cstddef>
#include <cstring> // memset
#include <new>     // std::bad_alloc

namespace platform {

#if defined(__APPLE__) || defined(__linux__)

#include <sys/mman.h>

// Allocate `bytes` of virtual memory, zero-filled on first access (demand
// paging). Physical pages are only committed when actually written. Returns
// page-aligned pointer. Throws std::bad_alloc on failure.
inline void *alloc_zeroed(size_t bytes) {
  void *p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (p == MAP_FAILED) {
    throw ::std::bad_alloc();
  }
  return p;
}

// Release memory previously obtained from alloc_zeroed.
inline void release(void *ptr, size_t bytes) { munmap(ptr, bytes); }

// Logically clear the region: subsequent reads will return zero.
// Does NOT wait for physical pages to be reclaimed by the OS.
inline void reset_to_zero(void *ptr, size_t bytes) {
#if defined(__linux__)
  // MADV_DONTNEED on Linux: immediate physical page reclaim, zeroes guaranteed
  // on next read.
  madvise(ptr, bytes, MADV_DONTNEED);
#else
  // On macOS, MADV_FREE/MADV_DONTNEED are advisory only — no zeroing guarantee.
  // MAP_FIXED atomically replaces the region with a fresh zero-filled anonymous
  // mapping.
  void *result = mmap(ptr, bytes, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
  if (result == MAP_FAILED) {
    // Should not happen in practice (same address, same size, no new
    // reservation needed). Fall back to memset to preserve correctness.
    ::std::memset(ptr, 0, bytes);
  }
#endif
}

inline constexpr bool has_lazy_alloc() { return true; }

// RAII wrapper for a lazy-allocated memory region.
// Owns the allocation; destructor calls release().
// Not copyable — intended as a direct member of cache classes.
struct lazy_buffer {
    void*  ptr;
    size_t bytes;

    explicit lazy_buffer(size_t n) : ptr(alloc_zeroed(n)), bytes(n) {}
    ~lazy_buffer() { release(ptr, bytes); }

    // Reset: logically zeroes the region; subsequent reads return zero.
    void reset() { reset_to_zero(ptr, bytes); }

    // Typed access to the buffer contents.
    template<typename T> T*       as()       { return static_cast<T*>(ptr); }
    template<typename T> const T* as() const { return static_cast<const T*>(ptr); }

private:
    lazy_buffer(const lazy_buffer&);
    lazy_buffer& operator=(const lazy_buffer&);
};

#else

// Fallback stubs for unsupported platforms (Windows, etc.).
// Cache classes fall back to std::vector on these platforms.
// Stubs allow platform_memory.h to be included unconditionally.
inline void *alloc_zeroed(size_t) { return nullptr; }
inline void release(void *, size_t) {}
inline void reset_to_zero(void *, size_t) {}
inline constexpr bool has_lazy_alloc() { return false; }

// lazy_buffer is not defined on unsupported platforms; caches use #if guards
// to select std::vector instead.

#endif

} // namespace platform

#endif // SOLVITAIRE_PLATFORM_MEMORY_H
