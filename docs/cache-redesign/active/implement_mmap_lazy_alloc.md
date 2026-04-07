# Implementation Plan: mmap Lazy Allocation for flat_cache

**Priority:** High — single largest efficiency gain available (see optimization-opportunities.md §1)
**Branch:** `implement-mmap-cache` (cut from `dev`, merge back to `dev`, then sync to `refactor-caching`)
**Date:** 2026-04-07
**Estimated scope:** 3 files created/modified, ~60 lines changed

---

## Problem

`flat_cache` constructor calls `clusters.resize(num_clusters)`, which zero-fills the
entire buffer immediately. At default capacity (100M entries → 50M clusters × 64 bytes =
**3.2 GB**), this takes ~200–400 ms before any solving begins.

For short searches (thousands of states, touching <<1% of the table), this startup cost
dominates and makes benchmarking misleading. For the default benchmarking workflow that
creates a fresh cache per game, this cost is paid on every seed.

---

## Solution

Replace `std::vector<cluster>` with a raw pointer to a platform-allocated memory region.
The OS provides zero-filled virtual pages but **defers physical page commitment until
first touch** (demand paging). A 3.2 GB allocation that is only 1% accessed incurs only
~32 MB of actual page faults.

**Platform support:**
- **macOS** (`__APPLE__`): `mmap` + `madvise(MADV_FREE)` — full support
- **Linux** (`__linux__`): `mmap` + `madvise(MADV_DONTNEED)` — full support
- **Other (Windows, etc.):** fall back to current `std::vector` eager allocation — no regression

---

## Branch Workflow

```bash
# 1. Cut from dev (NOT from refactor-caching — dev has the actual code)
git checkout dev
git pull
git checkout -b implement-mmap-cache

# 2. Make the three changes below (platform_memory.h, flat_cache.h, flat_cache.cpp)

# 3. Build and test
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
./scripts/container-build.sh --test   # verifies Linux path

# 4. If all pass, merge back to dev
git checkout dev
git merge implement-mmap-cache --no-ff
git push origin dev
git branch -d implement-mmap-cache
git push origin --delete implement-mmap-cache

# 5. Sync design branch
git checkout refactor-caching
git merge dev
git push origin refactor-caching
```

---

## File 1: CREATE `src/main/game/platform_memory.h`

New header-only file. No `.cpp` needed. No CMakeLists.txt change needed.

```cpp
#ifndef SOLVITAIRE_PLATFORM_MEMORY_H
#define SOLVITAIRE_PLATFORM_MEMORY_H

#include <cstddef>
#include <new>      // std::bad_alloc

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
    if (p == MAP_FAILED) throw std::bad_alloc();
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
    // MADV_DONTNEED on Linux: immediately frees physical pages.
    // Subsequent reads guaranteed to return zero. Safe for is_occupied() check.
    // Do NOT use MADV_FREE on Linux: it only hints that pages may be reclaimed;
    // reads may return stale non-zero data until the kernel acts, which would
    // cause is_occupied() to return true for logically empty slots.
    madvise(ptr, bytes, MADV_DONTNEED);
#else
    // MADV_FREE on macOS (Darwin): lazily reclaims pages under memory pressure.
    // On Darwin, subsequent reads DO return zero (unlike Linux MADV_FREE).
    madvise(ptr, bytes, MADV_FREE);
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
```

---

## File 2: MODIFY `src/main/game/flat_cache.h`

**Changes:**
1. Add `#include "platform_memory.h"`
2. Replace `std::vector<cluster> clusters;` with a conditional block
3. Add explicit destructor declaration
4. Keep `#include <vector>` (still needed on non-lazy platforms)

Show the full private section replacement:

**Before** (lines 38–46):
```cpp
private:
    // Maps a 64-bit hash to a cluster index in [0, num_clusters)
    uint64_t cluster_index(uint64_t hash) const;

    std::vector<cluster> clusters;
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};
```

**After:**
```cpp
    // cache_interface implementation
    explicit flat_cache(uint64_t max_entries);
    ~flat_cache() override;    // <-- ADD this line (was implicit before)

    // ... (existing public method declarations unchanged) ...

private:
    uint64_t cluster_index(uint64_t hash) const;

#if defined(__APPLE__) || defined(__linux__)
    cluster* clusters;
    size_t   alloc_bytes;
#else
    std::vector<cluster> clusters;
#endif
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};
```

Also add after the existing includes at the top:
```cpp
#include "platform_memory.h"
```

---

## File 3: MODIFY `src/main/game/flat_cache.cpp`

### 3a. Constructor — replace `clusters.resize(num_clusters)`

**Before** (lines 11–17):
```cpp
flat_cache::flat_cache(uint64_t max_entries)
    : num_clusters(std::max<uint64_t>(1, max_entries / 2))
    , occupied_count(0)
    , eviction_count(0)
{
    clusters.resize(num_clusters);  // zero-initialised
}
```

**After:**
```cpp
flat_cache::flat_cache(uint64_t max_entries)
    : num_clusters(std::max<uint64_t>(1, max_entries / 2))
    , occupied_count(0)
    , eviction_count(0)
{
#if defined(__APPLE__) || defined(__linux__)
    alloc_bytes = num_clusters * sizeof(cluster);
    clusters = static_cast<cluster*>(platform::alloc_zeroed(alloc_bytes));
#else
    clusters.resize(num_clusters);  // eager zero-fill on unsupported platforms
#endif
}
```

### 3b. Add destructor — insert after the constructor

**Add this new function:**
```cpp
flat_cache::~flat_cache() {
#if defined(__APPLE__) || defined(__linux__)
    platform::release(clusters, alloc_bytes);
#endif
    // std::vector destructor handles the non-lazy path automatically.
}
```

### 3c. `clear()` — replace per-entry loop with reset_to_zero

**Before** (lines 95–102):
```cpp
void flat_cache::clear() {
    for (auto& cl : clusters) {
        cl.entries[0].clear();
        cl.entries[1].clear();
    }
    occupied_count = 0;
    eviction_count = 0;
}
```

**After:**
```cpp
void flat_cache::clear() {
#if defined(__APPLE__) || defined(__linux__)
    platform::reset_to_zero(clusters, alloc_bytes);
#else
    for (auto& cl : clusters) {
        cl.entries[0].clear();
        cl.entries[1].clear();
    }
#endif
    occupied_count = 0;
    eviction_count = 0;
}
```

### 3d. No other changes to flat_cache.cpp

`clusters[idx]` indexing works identically for `cluster*` and `std::vector<cluster>`.
`insert()`, `contains()`, `cluster_index()`, `size()`, `bucket_count()`,
`get_states_removed_from_cache()` — all unchanged.

---

## Nothing Else to Change

- **`CMakeLists.txt`** — no change. `platform_memory.h` is header-only; `sys/mman.h` is
  a standard POSIX header requiring no extra link flags on macOS or Linux.
- **`hash_only_cache`** and **`predecessor_flat_cache`** — out of scope for this task.
  They can be migrated to `platform_memory.h` in follow-on work if desired.
- **`cache_factory.h`** — no change.
- **No oracle regeneration** — this is an allocation-only change with no effect on search
  behaviour or outcomes.

---

## Testing Checklist

Run in this order. Stop and report on failure; do not proceed to the next step.

1. **macOS build and unit tests:**
   ```bash
   ./build.sh --release --unit-tests
   cd cmake-build-release && ctest -R unit_tests --output-on-failure
   ```

2. **macOS regression Level 1:**
   ```bash
   cd cmake-build-release && ctest -R regression_level1 --output-on-failure
   ```

3. **Linux container (verifies MADV_DONTNEED path):**
   ```bash
   ./scripts/container-build.sh --test
   ```
   This builds inside a Linux container and runs unit tests. If container tools are not
   available, skip and note it. Linux testing on a physical machine can follow later.

4. **Optional smoke benchmark** (not required for merge, but useful):
   ```bash
   # Compare startup time for a trivially short search
   time ./cmake-build-release/bin/solvitaire --type klondike --random 1 --json
   # Should be noticeably faster than before for a completed easy solve
   ```

---

## Expected Outcome

On macOS and Linux: `flat_cache` construction becomes near-instantaneous for any capacity.
Physical memory is only consumed in proportion to the number of states actually visited.
`clear()` no longer iterates over all clusters; it issues a single syscall.

On Windows (and any other platform): behaviour identical to today — `std::vector` eager
zero-fill, no regression.

All tests pass. No change to solver correctness or cache semantics.
