# Milestone 3: Flat Cache Implementation — Detailed Plan

**Status:** Ready to implement
**Prerequisite:** Milestone 2 (complete) — descriptor-aligned Zobrist hash and compact_state payload are incrementally maintained in game_state.
**Branch:** `refactor-caching`

---

## Goal

Implement a flat, open-addressed hash table (`flat_cache`) with two-slot clusters as a new implementation of `cache_interface`. This cache is **not yet connected to the solver** — it exists alongside the existing `lru_cache` and is exercised only via unit tests in this milestone.

## Architecture Context

The solver uses a `cache_interface` abstract class (defined in `src/main/game/cache_interface.h`). The existing `lru_cache` (Boost MultiIndex) implements this interface. The new `flat_cache` will also implement it.

Each `game_state` already maintains:
- `uint64_t zobrist_hash_value` — incrementally updated Zobrist hash
- `compact_state payload` — 32-byte struct with per-card 4-bit descriptors, foundation tops, waste pointer

These are accessible via `gs.get_zobrist_hash()` and `gs.get_payload()`.

The `compact_state` struct (32 bytes) has:
- Byte 0: occupied flag (0 = empty, nonzero = occupied)
- Bytes 1-2: depth (16-bit, excluded from comparison)
- Bytes 3-31: game state data (compared via `matches()` which does `memcmp(data+3, other.data+3, 29)`)

---

## Task 3.1: Create flat_cache.h

Create `src/main/game/flat_cache.h`:

```cpp
#ifndef SOLVITAIRE_FLAT_CACHE_H
#define SOLVITAIRE_FLAT_CACHE_H

#include "cache_interface.h"
#include "compact_state.h"
#include <vector>
#include <cstdint>

class flat_cache : public cache_interface {
public:
    // A cluster is exactly 64 bytes (one cache line), holding 2 entries
    struct alignas(64) cluster {
        compact_state entries[2];   // 2 × 32 bytes = 64 bytes
    };

    // max_entries: approximate number of entries the cache should hold.
    // Internally rounded to determine cluster count.
    explicit flat_cache(uint64_t max_entries);

    // cache_interface implementation
    bool insert(const game_state& gs) override;
    bool contains(const game_state& gs) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t get_states_removed_from_cache() const override;
    uint64_t bucket_count() const override;

private:
    // Maps a 64-bit hash to a cluster index in [0, num_clusters)
    uint64_t cluster_index(uint64_t hash) const;

    std::vector<cluster> clusters;
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};

#endif
```

### Key design decisions:

1. **`alignas(64)`** on cluster ensures each cluster starts on a cache-line boundary, so accessing both slots in a cluster is a single L1 cache line read.

2. **Constructor** takes `max_entries` (approximate). Compute `num_clusters = max_entries / 2`. Round up to at least 1. Allocate `std::vector<cluster>(num_clusters)` — the vector zero-initialises, so all occupied flags start at 0 (empty).

3. **`cluster_index(hash)`**: Use Fibonacci hashing (multiply-high) for fast, well-distributed mapping:
   ```cpp
   uint64_t cluster_index(uint64_t hash) const {
       return (uint64_t)((__uint128_t)hash * num_clusters >> 64);
   }
   ```
   If `__uint128_t` is unavailable on the target platform, fall back to `hash % num_clusters`.

---

## Task 3.2: Create flat_cache.cpp

Create `src/main/game/flat_cache.cpp`:

### Constructor
```cpp
flat_cache::flat_cache(uint64_t max_entries)
    : num_clusters(std::max<uint64_t>(1, max_entries / 2))
    , occupied_count(0)
    , eviction_count(0)
{
    clusters.resize(num_clusters);  // zero-initialised
}
```

### insert(const game_state& gs)

Algorithm:
1. Get hash from `gs.get_zobrist_hash()` and payload from `gs.get_payload()`.
2. Make a local copy of the payload. Set `occupied` flag to true. (Do NOT set depth yet — that's Milestone 4.)
3. Compute cluster index from hash.
4. Check both slots for a match using `compact_state::matches()`:
   - If either slot matches → return `false` (state already present, not newly inserted).
5. If no match, try to insert:
   - **Slot 0 (depth-preferred):** If slot 0 is empty, place there. If slot 0 is occupied, overwrite only if the new entry's depth ≤ stored entry's depth. (For Milestone 3, depth is always 0, so this effectively means: if empty, use it; if occupied, overwrite since 0 ≤ stored depth.)
   - **Slot 1 (always-replace):** If slot 0 couldn't be used, place in slot 1.
   - If an occupied slot was overwritten, increment `eviction_count`.
6. Increment `occupied_count`.
7. Return `true` (newly inserted).

### contains(const game_state& gs) const

1. Get hash and payload from `gs`.
2. Make a local copy of the payload. Set `occupied` flag to true.
3. Compute cluster index.
4. Check both slots using `compact_state::matches()`.
5. Return `true` if either matches, `false` otherwise.

### clear()
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

### Simple getters
```cpp
uint64_t flat_cache::size() const { return occupied_count; }
uint64_t flat_cache::get_states_removed_from_cache() const { return eviction_count; }
uint64_t flat_cache::bucket_count() const { return num_clusters * 2; }
```

### Important notes:
- The `insert` method must copy the payload before modifying it (setting occupied flag), not modify the game_state's payload.
- The `matches()` comparison ignores bytes 0-2 (occupied flag and depth), comparing only bytes 3-31.

---

## Task 3.3: Add to CMakeLists.txt

Add `flat_cache.h` and `flat_cache.cpp` to the `sources_game` variable in `CMakeLists.txt`, alongside the existing compact_state and parent_table entries.

---

## Task 3.4: Unit Tests

Add tests in `src/test/unit_tests/flat_cache_test.cpp` (new file). Add this file to the `sources_test` variable in `CMakeLists.txt`.

### Test 1: BasicInsertAndContains
Create a flat_cache with capacity 1000. Create a FreeCell game_state (seed 1). Make a few moves to get different states. Insert each, verify `insert()` returns true. Verify `contains()` returns true for each inserted state.

### Test 2: DuplicateInsertReturnsFalse
Insert a state, then insert the same state again. Second `insert()` should return `false`.

### Test 3: ContainsReturnsFalseForAbsent
Create a state, don't insert it. `contains()` should return false.

### Test 4: SizeTracking
Insert N unique states. Verify `size() == N`.

### Test 5: EvictionWorks
Create a flat_cache with very small capacity (e.g., 4 entries = 2 clusters). Insert more unique states than capacity. Verify no crash, `get_states_removed_from_cache() > 0`.

### Test 6: ClearResetsEverything
Insert states, call `clear()`. Verify `size() == 0`, `get_states_removed_from_cache() == 0`, previously inserted states are no longer found by `contains()`.

### Test 7: StressTest
Create a flat_cache with capacity 10000. Loop through 1000 FreeCell seeds, for each seed create a game_state and insert it. Verify no crash and `size() <= bucket_count()`.

### How to generate distinct game states for testing:
Use different random seeds: `game_state gs(rules, seed, streamliner_opts)` with varying `seed`. Each seed produces a unique deal → unique payload → unique hash.

---

## Task 3.5: Build and Verify

1. Build with `./build.sh --release --unit-tests`
2. Run all tests: `cd cmake-build-release && ctest --output-on-failure`
3. All existing tests must still pass
4. All new flat_cache tests must pass

---

## Review Criteria

- [ ] `flat_cache` implements `cache_interface` correctly
- [ ] `cluster` is `alignas(64)` and exactly 64 bytes
- [ ] `cluster_index` uses multiply-high (Fibonacci hashing)
- [ ] Two-slot replacement policy: slot 0 = depth-preferred, slot 1 = always-replace
- [ ] Empty slots detected via `is_occupied()` (byte 0)
- [ ] All existing tests pass unchanged
- [ ] All new flat_cache tests pass
- [ ] Code compiles with `-Wall -Wextra -Werror`
- [ ] No memory leaks or sanitizer errors
