# Prompt: Implement mmap Lazy Allocation for flat_cache

You are working on **ReSolvitaire**, a C++14 DFS solver for solitaire games.
Your task is to implement lazy memory allocation for `flat_cache` using `mmap` on
macOS and Linux. Full implementation details are in
`docs/cache-redesign/active/implement_mmap_lazy_alloc.md` — read that file first.

## Repository

Working directory: the root of the ReSolvitaire-caching repository.
Current branch: `dev` (you will create a new branch from here).

## Your task

1. **Read** `docs/cache-redesign/active/implement_mmap_lazy_alloc.md` in full before
   touching any code.

2. **Create branch:**
   ```bash
   git checkout dev && git pull
   git checkout -b implement-mmap-cache
   ```

3. **Create** `src/main/game/platform_memory.h` — exact content in the plan.

4. **Modify** `src/main/game/flat_cache.h`:
   - Add `#include "platform_memory.h"` after the existing includes
   - Add `~flat_cache() override;` to the public section (after the constructor)
   - Replace `std::vector<cluster> clusters;` with the conditional `#if` block from
     the plan (keeping `std::vector` on the else branch for non-Linux/macOS)
   - Add `size_t alloc_bytes;` in the `#if` branch

5. **Modify** `src/main/game/flat_cache.cpp`:
   - Replace the body of the constructor with the `#if` branched version
   - Add the destructor `flat_cache::~flat_cache()` after the constructor
   - Replace the body of `clear()` with the `#if` branched version
   - Do NOT change `insert()`, `contains()`, `cluster_index()`, or any other method

6. **Build and test** (stop and report on any failure):
   ```bash
   ./build.sh --release --unit-tests
   cd cmake-build-release && ctest -R unit_tests --output-on-failure
   cd cmake-build-release && ctest -R regression_level1 --output-on-failure
   ```
   Then test Linux if container tools are available:
   ```bash
   ./scripts/container-build.sh --test
   ```

7. **Commit** when all tests pass:
   ```bash
   git add src/main/game/platform_memory.h \
           src/main/game/flat_cache.h \
           src/main/game/flat_cache.cpp
   git commit -m "perf: lazy mmap allocation for flat_cache on macOS and Linux

   Replace std::vector<cluster> eager zero-fill with mmap-backed demand
   paging. Physical pages are only committed on first write. clear() uses
   madvise(MADV_DONTNEED/FREE) instead of iterating all clusters.

   Windows and other platforms fall back to std::vector (no regression).
   No change to cache semantics, replacement policy, or solver behaviour.
   "
   ```

8. **Push** the branch:
   ```bash
   git push origin implement-mmap-cache
   ```

## Constraints

- C++14 only. No C++17 features.
- The `#if defined(__APPLE__) || defined(__linux__)` guard must wrap **both** the
  struct member declarations in the header and the implementation in the .cpp.
  Do not use runtime detection — this is compile-time.
- On Linux, use `MADV_DONTNEED` (not `MADV_FREE`) in `reset_to_zero`. See the plan for
  why: `MADV_FREE` on Linux does not guarantee zeroes on next read, which would break
  `is_occupied()`.
- Do not modify `hash_only_cache`, `predecessor_flat_cache`, `cache_factory.h`,
  `CMakeLists.txt`, or any test file.
- Do not regenerate oracles — this change has no effect on solver outcomes.
- If any test fails, stop, report the failure output, and do not proceed to commit.

## What "done" looks like

- `platform_memory.h` exists with `alloc_zeroed`, `release`, `reset_to_zero`,
  `has_lazy_alloc()` in namespace `platform`
- `flat_cache.h` has the conditional member declarations and destructor declaration
- `flat_cache.cpp` has the conditional constructor, new destructor, and conditional clear
- All unit tests pass
- Regression Level 1 passes
- Linux container tests pass (or noted as unavailable)
- One clean commit on branch `implement-mmap-cache`
- Branch pushed to origin
