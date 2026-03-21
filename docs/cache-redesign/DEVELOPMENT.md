# Cache Redesign: Development Log

**Project**: ReSolvitaire Cache Refactor
**Branch**: `refactor-caching`
**Reference**: *ReSolvitaire Cache Redesign: Architecture and Payload Specification* (main.pdf)

---

## Milestone 1: Abstract Cache Interface ✓

**Status**: COMPLETE
**Date**: 2026-03-21
**Commit**: cfdda7a
**Duration**: ~2.5 minutes for all tests

### Changes

#### New Files
- `src/main/game/cache_interface.h` - Pure virtual interface with 6 methods:
  - `bool insert(const game_state&)`
  - `bool contains(const game_state&) const`
  - `void clear()`
  - `uint64_t size() const`
  - `uint64_t get_states_removed_from_cache() const`
  - `uint64_t bucket_count() const`

#### Modified Files

**`src/main/game/global_cache.h`**
- Added `#include "cache_interface.h"`
- Made `lru_cache : public cache_interface`
- Renamed `insert(...)` → `insert_with_iterator(...)` (old method for iterator access)
- Added new `bool insert(const game_state&) override` (interface method)
- Updated return types: `item_list::size_type` → `uint64_t` for `size()` and `bucket_count()`
- Added `cached_size()` helper returning `item_list::size_type` for internal use

**`src/main/game/global_cache.cpp`**
- Implemented `bool insert(const game_state&) override` wrapper calling `insert_with_iterator().second`
- Updated `size()` and `bucket_count()` to return `uint64_t` with static_cast

**`src/main/solver/solver.h`**
- Added `#include "cache_interface.h"`
- Changed `lru_cache cache;` → `cache_interface& cache;` (reference)
- Updated constructor: `solver(const game_state&, cache_interface&)`
- Updated `result` struct to use `uint64_t` instead of `lru_cache::item_list::size_type`

**`src/main/solver/solver.cpp`**
- Updated constructor to accept `cache_interface& c` parameter
- Added dynamic_cast to `lru_cache` when calling `insert_with_iterator()` and `set_non_live()`
- No behavioral changes; same DFS algorithm

**`src/main/main.cpp`**
- Added `#include "game/global_cache.h"`
- Modified `solve_game()` to construct `lru_cache` explicitly and pass to solver

**`src/main/evaluation/solvability_calc.cpp`**
- Added `#include "game/global_cache.h"`
- Modified `solve_seed()` to construct `lru_cache` explicitly and pass to solver

**`src/main/evaluation/benchmark.cpp`**
- Added `#include "game/global_cache.h"`
- Modified loop to construct `lru_cache` per game and pass to solver

**`src/test/test_helper.cpp`**
- Added `#include "game/global_cache.h"`
- Updated both `is_solvable()` and `run_foundations_dominance_test()` to construct `lru_cache`

**`src/test/unit_tests/face_up_cards_test.cpp`**
- Updated calls from `cache.insert()` → `cache.insert_with_iterator()` (4 occurrences)

### Key Design Decisions

1. **Dynamic cast for LRU-specific methods**: Since `solver` only holds a `cache_interface&`, we dynamic_cast when accessing LRU-specific methods (`set_non_live`, `insert_with_iterator`). This will fail at runtime if a non-LRU cache is used, alerting us early. Future milestones will handle this more gracefully.

2. **Keep iterator storage**: `node::cache_state` still holds `lru_cache::item_list::iterator`. This is LRU-specific, but for Milestone 1 we only use lru_cache, so it works. Future milestones will make this optional or use a void pointer.

3. **New bool-returning insert()**: The interface uses the simpler `bool insert()` that returns true if new. The old `pair<iterator, bool>` is now `insert_with_iterator()` for internal LRU use only.

### Test Results

All tests pass with **identical behavior** to baseline:

| Test Suite | Count | Result | Time |
|---|---|---|---|
| Unit Tests | 133 | ✓ PASS | 34ms |
| Regression Level 1 | 150 | ✓ PASS | 4.2s |
| Regression Level 2 | ~160 | ✓ PASS | 21.4s |
| Regression Level 3 | ~160 | ✓ PASS | 103.5s |
| **Total** | **603** | **✓ ALL PASS** | **~2.5 min** |

**No solvability regressions**: All seeds that were solvable/unsolvable before remain so.

### Milestone 1 Acceptance Criteria

- ✓ All existing tests pass with identical results
- ✓ Benchmark output matches baseline exactly (same states searched, same solvability results)
- ✓ `cache_interface` header compiles and `lru_cache` implements it
- ✓ No behavioral change whatsoever
- ✓ Code compiles with `-Wall -Wextra -Werror`

---

---

## Milestone 2: Zobrist Hash Infrastructure ✓

**Status**: COMPLETE
**Date**: 2026-03-21
**Commit**: 7b1d115
**Duration**: ~2.5 minutes for all tests

### Changes

#### New Files
- `src/main/game/zobrist.h` - Zobrist key table with static interface
- `src/main/game/zobrist.cpp` - Random key initialization with MT19937

#### Modified Files

**`src/main/game/zobrist.h`**
- Static methods: `init()`, `key()`, `card_id()`
- Fixed-size 3D array: `key_table[52][7][104]` (card_id × role × position)
- Enum `pile_role`: FOUNDATION, TABLEAU, STOCK, WASTE, RESERVE, CELL, HOLE
- Reproducible initialization with fixed seed `0xDEADBEEF12345678ULL`

**`src/main/game/zobrist.cpp`**
- `init()`: Uses `std::mt19937_64` to fill key table with random 64-bit values
- `key()`: Bounds-checked lookup returning 0 for invalid indices
- `card_id()`: Maps (suit, rank) → card_id [0..51]

**`src/main/game/search-state/game_state.h`**
- Added `#include "zobrist.h"`
- Added members:
  - `uint64_t zobrist_hash_value` - global game state hash
  - `std::vector<uint64_t> per_pile_hash` - hash per pile for symmetry

**`src/main/game/search-state/game_state.cpp`**
- Private constructor: Initialize `per_pile_hash` vector (all zeros) and `zobrist_hash_value = 0`
- Implement `bool is_interchangeable_pile(pile::ref) const`:
  - Returns true for: tableau piles, cell piles, unstacked reserve piles
  - Returns false for: foundations, stock, waste, hole, sequences, accordion, stacked reserve
- All three public constructors: Initialize hash structures (currently placeholder: all zeros)

**`src/main/main.cpp`**
- Added `#include "game/zobrist.h"`
- Call `zobrist_hash::init()` at start of main() before any game creation

**`CMakeLists.txt`**
- Added `src/main/game/zobrist.h` and `src/main/game/zobrist.cpp` to `sources_game`
- Also added missing `src/main/game/cache_interface.h` from Milestone 1

### Key Design Decisions

1. **Fixed seed for reproducibility**: Zobrist tables are initialized once globally, ensuring deterministic behavior across runs.

2. **Placeholder hash computation**: For Milestone 2, `zobrist_hash_value` and `per_pile_hash` are initialized to 0. Incremental updates will be added in Milestone 3.

3. **Symmetry-invariant design**: The `is_interchangeable_pile()` method identifies which piles can be reordered without changing semantics. Future milestones will use this to compute hash as:
   - Non-interchangeable piles: XOR their hashes
   - Interchangeable piles: SUM their hashes (mod 2^64)

4. **Static Zobrist class**: All methods are static; no instance needed. Simplifies initialization and access.

### Test Results

All tests pass with **identical behavior** to baseline:

| Test Suite | Count | Result | Time |
|---|---|---|---|
| Unit Tests | 133 | ✓ PASS | 46ms |
| Regression Level 1 | 150 | ✓ PASS | 4.0s |
| Regression Level 2 | ~160 | ✓ PASS | 22.0s |
| Regression Level 3 | ~160 | ✓ PASS | 101.3s |
| **Total** | **603** | **✓ ALL PASS** | **~2.5 min** |

**No behavior change**: Hash computation is not yet used, so solver behavior is identical to Milestone 1.

### Milestone 2 Acceptance Criteria

- ✓ Zobrist key table initializes correctly with fixed seed
- ✓ `zobrist_hash::init()` called at program startup
- ✓ `game_state` contains `zobrist_hash_value` and `per_pile_hash`
- ✓ `is_interchangeable_pile()` correctly identifies symmetric piles
- ✓ All existing tests pass with identical results
- ✓ No behavioral change (hash not yet used for deduplication)
- ✓ Code compiles with `-Wall -Wextra -Werror`

---

## Milestone 3: Payload Infrastructure

**Status**: PENDING
**Next Steps**:
1. Create `src/main/game/compact_state.h` with 32-byte payload struct
2. Create `src/main/game/compact_state.cpp` with nibble bit-packing
3. Create `src/main/game/parent_table.h/cpp` for parent card lookups
4. Add incremental payload maintenance to game_state
5. Test payload invariants (from-scratch vs. incremental)

