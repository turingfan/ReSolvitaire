# ReSolvitaire Cache Refactor: Implementation Plan

**Version:** 1.0  
**Date:** 21 March 2026  
**Authors:** Ian Gent & AI Assistant  
**Reference Document:** *ReSolvitaire Cache Redesign: Architecture and Payload Specification* (main.pdf)

---

## Overview

This plan replaces Solvitaire's Boost.MultiIndex LRU cache with a flat open-addressed hash table using Zobrist hashing and a 32-byte per-entry payload. The implementation proceeds in 9 milestones, each independently testable. The plan is designed for implementation by AI coding assistants (Haiku 4.5 or above) or human developers.

**Repository setup:**
- All work on this branch refactor-caching` 
= regular merge from `mac-dev` to stay current
- Each milestone ends with a commit that passes all existing tests plus new milestone-specific tests

**Key source files (current codebase):**

| File | Role |
|---|---|
| `src/main/game/global_cache.h` | `cached_game_state`, `hasher`, `lru_cache` classes |
| `src/main/game/global_cache.cpp` | Cache implementation, state serialisation |
| `src/main/solver/solver.h` | `solver` class, `node` struct with `cache_state` iterator |
| `src/main/solver/solver.cpp` | DFS loop, `insert`, `contains`, `set_non_live`, backtracking |
| `src/main/game/search-state/game_state.h` | `game_state` class, pile references, streamliner_options |
| `src/main/game/search-state/game_state.cpp` | `make_move`, `undo_move`, `place_card`, `take_card` |
| `src/main/game/search-state/game_state.pile_order.cpp` | Pile symmetry sorting |
| `src/main/game/card.h` | `card` class (rank, suit, face_down) |
| `src/main/game/sol_rules.h` | `sol_rules` struct (all game rule parameters) |
| `src/main/game/move.h` | `move` struct |
| `src/main/input-output/input/command_line_helper.h` | CLI: `--cache-capacity` option |
| `src/main/main.cpp` | Entry point, solver construction |

---

## Milestone 0: Repository Setup and Baseline

**Goal:** Create the working repository and establish baseline test results.

### Tasks

1. Build and run all existing tests to confirm green baseline
2. Create a benchmark script that records solver results for a fixed set of test instances:
   - Use `--timeout 60000` (1 minute per instance) to prevent long-running tests
   - Run `--type free-cell --random 1 --classify` through `--random 100`
   - Run `--type klondike-deal-1 --random 1 --classify` through `--random 20`
   - Run `--type black-hole --random 1 --classify` through `--random 100`
   - Run `--type bakers-game --random 1 --classify` through `--random 100`
   - Run `--type spanish-patience --random 1 --classify` through `--random 50`
   - Save the output (solvable/unsolvable per seed + states searched + time)
3. Save this as `benchmark_baseline.txt` in the repo root

### Review criteria
- All existing tests pass
- Benchmark baseline captured
- Branch exists and builds cleanly

---

## Milestone 1: Abstract Cache Interface

**Goal:** Define a common interface that both old and new caches implement, and wire the solver to use it. The old cache continues to be used for all games. No behavioural change.

### Tasks

1. Create new file `src/main/game/cache_interface.h`:

```cpp
#ifndef SOLVITAIRE_CACHE_INTERFACE_H
#define SOLVITAIRE_CACHE_INTERFACE_H

#include <cstdint>

class game_state;

class cache_interface {
public:
    virtual ~cache_interface() = default;
    
    // Returns true if the state was newly inserted (not already present)
    virtual bool insert(const game_state& gs) = 0;
    
    // Returns true if the state is in the cache
    virtual bool contains(const game_state& gs) const = 0;
    
    virtual void clear() = 0;
    virtual uint64_t size() const = 0;
    virtual uint64_t get_states_removed_from_cache() const = 0;
    virtual uint64_t bucket_count() const = 0;
};

#endif
```

2. Make `lru_cache` implement `cache_interface`:
   - In `global_cache.h`, add `: public cache_interface` to `lru_cache`
   - Add `override` to matching methods
   - The existing `insert` returns a `pair<iterator, bool>`. Add a new `bool insert(const game_state&) override` that wraps it, calling the existing insert and returning `.second`
   - `contains`, `clear`, `size`, `bucket_count`, `get_states_removed_from_cache` already match signatures; add `override`

3. Modify `solver.h` and `solver.cpp`:
   - Change `lru_cache cache` to `cache_interface& cache` (reference) or `std::unique_ptr<cache_interface> cache`
   - The solver constructor currently creates the `lru_cache` inline. Change it to accept a `cache_interface&` or construct via a factory
   - **Critical:** The current solver stores `lru_cache::item_list::iterator` in `node::cache_state` and calls `cache.set_non_live()`. These are LRU-specific. For now, keep them but guard with a runtime check or downcast. The new cache will not use them.

4. In `main.cpp` and `solvability_calc.cpp`, construct `lru_cache` explicitly and pass to `solver`

### Review criteria
- All existing tests pass with identical results
- Benchmark output matches baseline exactly (same states searched, same solvability results)
- The `cache_interface` header compiles and `lru_cache` implements it
- No behavioural change whatsoever

---

## Milestone 2: Zobrist Hash Infrastructure

**Goal:** Add Zobrist hash tables and incremental hash computation to `game_state`. The hash is computed but not yet used for anything. Existing cache continues to work.

### Tasks

1. Create new file `src/main/game/zobrist.h`:

```cpp
#ifndef SOLVITAIRE_ZOBRIST_H
#define SOLVITAIRE_ZOBRIST_H

#include <cstdint>
#include <array>
#include <random>

// Zobrist key table: random 64-bit values for each (card_id, pile_role, position)
// card_id: 0-51 (suit*13 + rank-1)
// pile_role: enum identifying the container type
// position: index within the pile (0 = top/only card)
//
// For symmetry-invariant hashing:
// - Per-pile hash = XOR of Zobrist keys for all cards in that pile
// - Global hash = XOR of non-interchangeable pile hashes
//                + SUM of interchangeable pile hashes (mod 2^64)

class sol_rules;
class game_state;

class zobrist_hash {
public:
    // Maximum pile positions we support
    static constexpr int MAX_PILE_SIZE = 104;  // two-deck max
    static constexpr int NUM_CARDS = 52;
    
    enum class pile_role : uint8_t {
        FOUNDATION = 0,
        TABLEAU = 1,
        STOCK = 2,
        WASTE = 3,
        RESERVE = 4,
        CELL = 5,
        HOLE = 6,
        NUM_ROLES = 7
    };
    
    // Initialise random key table with a fixed seed (reproducible)
    static void init(uint64_t seed = 0xDEADBEEF12345678ULL);
    
    // Look up the Zobrist key for a card in a specific role and position
    static uint64_t key(uint8_t card_id, pile_role role, uint8_t position);
    
    // Card ID from suit and rank
    static uint8_t card_id(uint8_t suit, uint8_t rank);
    
private:
    // key_table[card_id][role][position]
    static uint64_t key_table[NUM_CARDS]
                              [static_cast<int>(pile_role::NUM_ROLES)]
                              [MAX_PILE_SIZE];
    static bool initialised;
};

#endif
```

2. Create `src/main/game/zobrist.cpp`: implement `init()` using `std::mt19937_64` with the fixed seed, filling `key_table` with random values. Implement `key()` and `card_id()`.

3. Add to `game_state`:
   - New member: `uint64_t zobrist_hash_value` (the running global hash)
   - New members: per-pile hash array (one `uint64_t` per pile in `piles` vector)
   - Initialise in constructors: compute full hash from initial state
   - In `place_card` and `take_card`: update per-pile hash (XOR) and global hash (subtract old pile hash, XOR-update pile hash, add new pile hash — for interchangeable piles; XOR for non-interchangeable)
   - Add helper method `bool is_interchangeable_pile(pile::ref pr) const` that returns true for tableau, cell, and unstacked reserve piles

4. Call `zobrist_hash::init()` once at program start (in `main()`)

### Testing

- Add a new test: compute hash from scratch for a state, make a move, undo it, verify hash returns to original value. Repeat for each move type.
- Add a test: compute hash from scratch for a state, compare to the incrementally-maintained hash. They must match.
- Add a test: create two states that differ only by swapping two tableau piles. Verify their hashes are identical (symmetry invariance).

### Review criteria
- All existing tests pass
- New Zobrist tests pass
- Benchmark output matches baseline (Zobrist hash is computed but not used)

---

## Milestone 3: Payload Infrastructure

**Goal:** Implement the 32-byte payload struct and its incremental maintenance. Not yet connected to any cache.

### Tasks

1. Create new file `src/main/game/compact_state.h`:

```cpp
#ifndef SOLVITAIRE_COMPACT_STATE_H
#define SOLVITAIRE_COMPACT_STATE_H

#include <cstdint>
#include <cstring>

// 32-byte compact state payload.
// Layout (256 bits):
//   Bits   0-15:  Depth counter (16-bit, excluded from comparison)
//   Bits  16-31:  Foundation top ranks (4x4 bits) OR hole-top (6 bits + 10 zero)
//   Bits  32-37:  Waste pointer (6 bits)
//   Bits  38-47:  Reserved/padding (10 bits, zero)
//   Bits  48-255: Per-card descriptors (52 x 4-bit nibbles)
//
// Descriptor values:
//   0 = STARTING (in original deal position; also used for foundation cards)
//   1 = IN_HOLE (played to hole)
//   2 = ROOT (bottom of tableau pile, moved from starting position)
//   3 = IN_CELL (in a free cell)
//   4 = PARENT_0 (built on first legal parent)
//   5 = PARENT_1 (built on second legal parent)
//   6 = PARENT_2 (built on third legal parent)
//   7 = PARENT_3 (built on fourth legal parent)
//   8-15 = RESERVED (zero, for future use)

struct compact_state {
    uint8_t data[32];
    
    enum descriptor : uint8_t {
        STARTING  = 0,
        IN_HOLE   = 1,
        ROOT      = 2,
        IN_CELL   = 3,
        PARENT_0  = 4,
        PARENT_1  = 5,
        PARENT_2  = 6,
        PARENT_3  = 7,
    };
    
    // Initialise to all zeros
    void clear();
    
    // Depth counter (bits 0-15, excluded from comparison)
    void set_depth(uint16_t d);
    uint16_t get_depth() const;
    
    // Foundation top ranks (bits 16-31)
    void set_foundation(uint8_t suit, uint8_t rank);
    uint8_t get_foundation(uint8_t suit) const;
    
    // Hole top card (bits 16-21, hole games only)
    void set_hole_top(uint8_t card_id);
    uint8_t get_hole_top() const;
    
    // Waste pointer (bits 32-37)
    void set_waste_ptr(uint8_t ptr);
    uint8_t get_waste_ptr() const;
    
    // Per-card descriptor (bits 48-255, 4 bits per card)
    void set_descriptor(uint8_t card_id, uint8_t value);
    uint8_t get_descriptor(uint8_t card_id) const;
    
    // Compare payloads (bytes 2-31 only, excluding depth)
    bool matches(const compact_state& other) const;
};
```

2. Create `src/main/game/compact_state.cpp`: implement all methods. The nibble access for descriptors:
   - Card `c` maps to byte `6 + c/2`, low nibble if `c` is even, high nibble if odd
   - `matches()` uses `memcmp(data + 2, other.data + 2, 30)`

3. Create `src/main/game/parent_table.h` and `.cpp`:
   - A lookup table mapping `(card_id, build_policy)` → list of parent card IDs in fixed order
   - For card of rank `r`, suit `s`:
     - RED_BLACK: parents are rank `r+1` cards of opposite colour, ordered by suit index
     - SAME_SUIT: parent is rank `r+1` of same suit
     - ANY_SUIT: parents are rank `r+1` of all four suits, ordered Clubs/Hearts/Spades/Diamonds
     - NO_BUILD: no parents
   - Also: a reverse lookup: given `(card_id, parent_card_id, build_policy)` → descriptor value (PARENT_0 through PARENT_3)

4. Add to `game_state`:
   - New member: `compact_state payload`
   - Initialise payload in constructors (all zeros = all STARTING, then set foundation/hole/waste-ptr initial values)
   - In `make_move` / `undo_move`: add payload update calls after existing state modifications
   - Create helper: `uint8_t compute_descriptor(card c, pile::ref from, pile::ref to) const` — determines what descriptor a card should get when moved to a given destination

5. Add helper: `bool use_new_cache(const sol_rules& rules)` — returns true if the game is single-deck, no sequences, no accordion. This determines which cache to use.

### Testing

- Unit test: create a FreeCell game state with seed 1. Verify payload initialisation (all descriptors = 0, foundations = 0).
- Unit test: make a move, check the affected card's descriptor changed correctly. Undo, check it reverted.
- Unit test: compare payload-from-scratch (computed by walking the full game state) against the incrementally-maintained payload. They must match after a sequence of 50 random legal moves.
- Repeat for Klondike, Baker's Game, Black Hole.

### Review criteria
- All existing tests pass
- New payload tests pass
- Benchmark output matches baseline (payload is maintained but not used)

---

## Milestone 4: Flat Cache Implementation

**Goal:** Implement the flat open-addressed cache with two-slot clusters and TwoBig1-style replacement. Not yet connected to the solver.

### Tasks

1. Create `src/main/game/flat_cache.h`:

```cpp
#ifndef SOLVITAIRE_FLAT_CACHE_H
#define SOLVITAIRE_FLAT_CACHE_H

#include "cache_interface.h"
#include "compact_state.h"
#include <cstdint>
#include <vector>

class game_state;

class flat_cache : public cache_interface {
public:
    // Each cluster is 64 bytes = 2 x 32-byte entries, cache-line aligned
    struct alignas(64) cluster {
        compact_state entries[2];
    };
    
    explicit flat_cache(uint64_t max_entries);
    
    // cache_interface
    bool insert(const game_state& gs) override;
    bool contains(const game_state& gs) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t get_states_removed_from_cache() const override;
    uint64_t bucket_count() const override;
    
private:
    // Map Zobrist hash to cluster index
    uint64_t cluster_index(uint64_t hash) const;
    
    // Replacement policy: TwoBig1
    // Slot 0: depth-preferred (overwrite only if new depth <= stored depth)
    // Slot 1: always-replace
    // Returns true if entry was new (not already present)
    bool insert_into_cluster(cluster& cl, const compact_state& state);
    
    std::vector<cluster> clusters;
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};

#endif
```

2. Create `src/main/game/flat_cache.cpp`:
   - Constructor: compute `num_clusters` from `max_entries / 2`, allocate vector, zero-fill
   - `cluster_index`: use `__uint128_t` multiply-high trick: `(uint64_t)((__uint128_t)hash * num_clusters >> 64)`. If `__uint128_t` not available, fall back to modulo.
   - `insert`: get `game_state.zobrist_hash_value`, compute cluster index, check both slots for match (using `compact_state::matches`), if match found return false (already present). If not, apply TwoBig1: try slot 0 (depth-preferred), else overwrite slot 1.
   - `contains`: same lookup, return true if either slot matches
   - An empty slot is identified by having all bytes 2-31 zero (since a real state always has at least one non-zero descriptor — the initial deal always has cards somewhere). Alternatively, use a sentinel value in a reserved field.

3. Important detail: the `insert` method needs access to the game state's `zobrist_hash_value` and `payload`. Add getter methods to `game_state` if not already public.

### Testing

- Unit test: create flat_cache with capacity 1000. Insert 500 unique states, verify `size() == 500`. Verify `contains()` returns true for all 500.
- Unit test: insert a state, verify contains. Insert a different state with the same hash (artificially), verify both coexist in the cluster or replacement occurs correctly.
- Unit test: overflow the cache and verify it doesn't crash, `size()` stays bounded, and `get_states_removed_from_cache()` increments.
- Stress test: insert 100,000 random FreeCell states, verify no crash and reasonable occupancy.

### Review criteria
- All existing tests pass
- New flat_cache tests pass
- Benchmark output still matches baseline (flat cache exists but solver still uses old cache)

---

## Milestone 5: Wire the Solver to the New Cache

**Goal:** Connect the flat cache to the solver for supported game types. This is the first milestone where behaviour changes.

### Tasks

1. In `solver.cpp` constructor (and wherever `lru_cache` is currently constructed):
   - Check `use_new_cache(rules)` 
   - If true: construct `flat_cache` and pass to solver
   - If false: construct `lru_cache` (existing behaviour)

2. Modify the DFS loop in `solver.cpp`:
   - Currently, the solver calls `cache.insert(state)` which returns a `pair<iterator, bool>` and stores the iterator in `current_node->cache_state`
   - For the new cache: `insert` returns just `bool` (was it new?)
   - Remove the `set_non_live` calls when using the new cache (no live bits)
   - Remove the iterator storage in `node::cache_state` when using the new cache
   - The cleanest approach: make `node::cache_state` optional, and skip `set_non_live` when using flat cache
   - Add a depth safety bound: if `res.depth` exceeds `cache_capacity * 2`, treat as TIMEOUT or MEM_LIMIT

3. Update `solvability_calc.cpp` similarly (it constructs solvers in `solve_seed`).

4. **Critical: disable pile ordering for new-cache games.** The pile ordering in `game_state.pile_order.cpp` (`eval_pile_order`) currently sorts interchangeable piles to maintain a canonical order for the old cache. For the new cache, this is unnecessary (the encoding is inherently canonical). However, **do not remove pile ordering yet** — it also affects move generation order, which could change search behaviour. Leave it in place for this milestone. Removing it is a separate optimisation for a later milestone.

### Testing

- Run the full benchmark suite with the new cache. **Results will differ** from baseline because:
  - No live bits means some cycle detection is lost (false negatives → extra search)
  - Different hash function means different collision patterns
  - TwoBig1 replacement differs from LRU
  - But: solvability results (solved/unsolvable) MUST match for all seeds
- **Primary correctness check:** For every seed, the solvability classification (solved/unsolvable/timeout) must match the baseline. If any seed flips from solved to unsolvable or vice versa, there is a bug.
- **Performance comparison:** Record states searched and time. Some seeds may be faster, some slower. Overall trends are interesting but not a pass/fail criterion.

### Review criteria
- All existing unit tests pass
- Solvability classifications match baseline for all benchmark seeds
- No crashes or memory errors (run with AddressSanitizer if available)
- New cache is used for FreeCell, Klondike, Baker's Game, Black Hole, Spanish Patience
- Old cache is used for Spider, Gaps, Accordion

---

## Milestone 6: Verification and Hardening

**Goal:** Thorough correctness testing across all supported game types.

### Tasks

1. Expand benchmark to all single-deck foundation/hole game presets:
   - Run 100 seeds each for: `free-cell`, `klondike-deal-1`, `klondike`, `bakers-game`, `black-hole`, `fan`, `spanish-patience`, `beleaguered-castle`, `somerset`, `king-albert`, `flower-garden`, `eight-off`, `canfield`, `raglan`, `fore-cell`, `seahaven-towers`, `american-canister`, `british-canister`, `worm-hole`, `golf`, `alina`, `one-cell`, `two-cell`, `stronghold`, `siegecraft`, `east-haven`, `fortunes-favor`, `chameleon`, `duchess`, `scotch-patience`, `trigon`, `thirtysix`, `thirty`
   - Compare solvability classifications against runs with old cache (both caches should agree on every seed)

2. Add a debug mode (`#ifndef NDEBUG`) that, on every cache insert:
   - Computes the payload from scratch (full state walk) and compares against the incrementally maintained payload
   - Asserts they match
   - This is expensive but catches incremental update bugs

3. Add a debug mode that, on every cache hit:
   - Verifies the matched state is genuinely identical by comparing the full `cached_game_state` (old-style) encoding
   - This catches encoding injectivity bugs

4. Run with AddressSanitizer and UndefinedBehaviourSanitizer.

5. Test with `--cache-capacity 1000` (very small cache) to stress replacement and eviction.

6. Test with `--cache-capacity 100000000` (large cache) on hard instances.

### Review criteria
- Solvability matches old cache for all tested games/seeds
- Debug assertions pass (no incremental update bugs, no injectivity failures)
- No sanitizer errors
- Document any games where the new cache is significantly slower or faster

---

## Milestone 7: Remove Pile Ordering for New-Cache Games

**Goal:** Eliminate the `eval_pile_order` calls for games using the new cache, saving per-move overhead.

### Tasks

1. In `game_state::place_card` and `game_state::take_card`, the pile ordering calls are guarded by `#ifndef NO_PILE_SYMMETRY`. Add a runtime flag (or compile-time ifdef) to skip them when using the new cache.

2. The cleanest approach: add a `bool use_pile_ordering` flag to `game_state`, set at construction based on `use_new_cache(rules)`. Guard `eval_pile_order` calls with this flag.

3. **This will change move generation order** (since `tableau_piles`, `cells`, `reserve` lists will no longer be sorted). This means the DFS will explore moves in a different order, potentially changing states searched and timing — but not solvability.

### Testing

- Re-run full benchmark suite
- Solvability classifications must still match
- Expect changes in states-searched counts (different move ordering)
- Measure time improvement from eliminating pile-order maintenance

### Review criteria
- Solvability matches for all tested games/seeds
- Measurable performance improvement on games with many interchangeable piles (FreeCell, Spanish Patience)
- No regressions on any game

---

## Milestone 8: Performance Benchmarking and Tuning

**Goal:** Measure the improvement and tune parameters.

### Tasks

1. Run comprehensive benchmarks comparing old cache vs new cache:
   - Wall-clock time per instance
   - States searched per second
   - Peak memory usage
   - Cache hit rate (add instrumentation: count insert-was-duplicate / total inserts)
   - Eviction count

2. Tune replacement policy:
   - Test always-replace vs TwoBig1 vs depth-only
   - Measure cache hit rate and states/second for each

3. Tune cluster size:
   - Test 2-entry clusters (current) vs 1-entry (simpler, more cache-line waste) vs 4-entry (128-byte clusters)
   
4. Profile hotspots:
   - Is nibble access a bottleneck? 
   - Is `memcmp` a bottleneck?
   - Is Zobrist hash computation a bottleneck?

5. Consider: large-page allocation for the flat array (2MB pages via `mmap` with `MAP_HUGETLB` on Linux, `madvise` with `MADV_HUGEPAGE`)

### Review criteria
- Documented performance comparison (table of times, states/second, memory)
- Chosen replacement policy with justification
- Identified and addressed any performance bottlenecks
- Overall improvement target: ≥2× more states cached in same memory, or ≥1.5× faster for hard instances

---

## Milestone 9: Documentation and Merge Preparation

**Goal:** Clean up, document, and prepare for merge to `mac-dev`.

### Tasks

1. Remove any dead code from the old cache path that's no longer needed
2. Update `--help` output if any CLI options changed
3. Add a comment block at the top of `flat_cache.h` and `compact_state.h` explaining the design (reference the LaTeX document)
4. Update `README.md` with a note about the cache refactor
5. Clean up any `TODO` or `FIXME` comments
6. Squash or organize commits for clean merge
7. Create a pull request from `refactor-caching` to `mac-dev` with a summary of changes and benchmark results

### Review criteria
- Code compiles with no warnings (`-Wall -Wextra`)
- All tests pass
- Documentation is present
- PR is ready for review

---

## Appendix A: Files Created or Modified

### New files
| File | Milestone | Purpose |
|---|---|---|
| `src/main/game/cache_interface.h` | M1 | Abstract cache interface |
| `src/main/game/zobrist.h` | M2 | Zobrist hash key table |
| `src/main/game/zobrist.cpp` | M2 | Zobrist hash implementation |
| `src/main/game/compact_state.h` | M3 | 32-byte payload struct |
| `src/main/game/compact_state.cpp` | M3 | Payload methods |
| `src/main/game/parent_table.h` | M3 | Parent card lookup table |
| `src/main/game/parent_table.cpp` | M3 | Parent table implementation |
| `src/main/game/flat_cache.h` | M4 | Flat cache class |
| `src/main/game/flat_cache.cpp` | M4 | Flat cache implementation |
| `benchmark_baseline.txt` | M0 | Baseline results |

### Modified files
| File | Milestone | Change |
|---|---|---|
| `src/main/game/global_cache.h` | M1 | `lru_cache` implements `cache_interface` |
| `src/main/solver/solver.h` | M1, M5 | Use `cache_interface`, optional `node::cache_state` |
| `src/main/solver/solver.cpp` | M1, M5 | Factory for cache, conditional live-bit logic |
| `src/main/game/search-state/game_state.h` | M2, M3 | Add `zobrist_hash_value`, `payload`, per-pile hashes |
| `src/main/game/search-state/game_state.cpp` | M2, M3 | Incremental hash and payload updates |
| `src/main/game/search-state/game_state.pile_order.cpp` | M7 | Conditional pile ordering |
| `src/main/main.cpp` | M2, M5 | Zobrist init, cache factory |
| `src/main/evaluation/solvability_calc.cpp` | M5 | Cache factory |

## Appendix B: Descriptor Value Reference

| Value | Name | Meaning |
|---|---|---|
| 0 | STARTING | Card in initial deal position (also used for foundation cards — ignored) |
| 1 | IN_HOLE | Card played to hole (hole games only) |
| 2 | ROOT | Bottom face-up card of a non-starting-position tableau pile |
| 3 | IN_CELL | Card in a free cell |
| 4 | PARENT_0 | Built on first legal parent (by fixed suit ordering) |
| 5 | PARENT_1 | Built on second legal parent |
| 6 | PARENT_2 | Built on third legal parent |
| 7 | PARENT_3 | Built on fourth legal parent |
| 8-15 | RESERVED | Zero (available for future: two-deck, streamliners) |

## Appendix C: Payload Byte Layout

```
Byte 0:  Depth (low byte)
Byte 1:  Depth (high byte)
Byte 2:  Foundation Clubs (low nibble) | Foundation Hearts (high nibble)
         OR Hole-top card ID (low 6 bits) for hole games
Byte 3:  Foundation Spades (low nibble) | Foundation Diamonds (high nibble)
         OR zero padding for hole games
Byte 4:  Waste pointer (low 6 bits) | padding (high 2 bits)
Byte 5:  Reserved (zero)
Bytes 6-31: Card descriptors
         Byte 6:  card 0 (low nibble) | card 1 (high nibble)
         Byte 7:  card 2 (low nibble) | card 3 (high nibble)
         ...
         Byte 31: card 50 (low nibble) | card 51 (high nibble)
```

Comparison: `memcmp(payload.data + 2, other.data + 2, 30)`

## Appendix D: Card ID Mapping

`card_id = suit * 13 + (rank - 1)`

| Suit | Index | Card ID range |
|---|---|---|
| Clubs | 0 | 0-12 (AC through KC) |
| Hearts | 1 | 13-25 (AH through KH) |
| Spades | 2 | 26-38 (AS through KS) |
| Diamonds | 3 | 39-51 (AD through KD) |
