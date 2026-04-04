# Implementation Plan: Hash-Only Streamliner (A) and Accordion Predecessor Cache (B)

**Date:** 2026-04-04
**Branch:** `refactor-caching` (planning); implementation from `dev`
**Status:** Plan — ready for implementation
**Origin:** Human contributions #23, #24, #28; proposal evaluation

---

## Overview

Two independent workstreams, implementable in parallel on separate branches from `dev`:

- **Stream A (`implement-hash-only-cache`):** Replace the 32-byte payload in flat_cache
  with an 8-byte Zobrist hash. Validates the hash-only streamliner concept (#28) using
  the existing flat cache infrastructure. Measures false-positive rate and speedup.

- **Stream B (`implement-accordion-predecessor`):** Implement the predecessor encoding
  (#24) for accordion games. First real test of the unified predecessor model.

Both merge independently into `dev`; then sync `dev` back into `refactor-caching`.

---

## Stream A: Hash-Only Flat Cache

### Goal

A new cache class `hash_only_cache` that stores 8-byte Zobrist hashes with no payload.
Used via `--cache-type hash-only` CLI flag. Dual-cache testing compares it against the
existing LRU cache to measure false-positive rate.

### Why Start Here

- Minimal new code — reuses existing Zobrist hash infrastructure
- No new encoding or game-specific logic needed
- Immediately testable on all flat-cache-eligible games
- Measures both speedups: density (4× over current 32-byte) and payload elimination
- Validates the core premise of contribution #28 before building more complex encodings

### Implementation Steps

#### A1. New `hash_only_cache` class

**Files:** `src/main/game/hash_only_cache.h`, `src/main/game/hash_only_cache.cpp`

```cpp
class hash_only_cache : public cache_interface {
public:
    // 2-way cluster, same structure as flat_cache but with 8-byte hash entries
    // instead of 32-byte compact_state entries. 16 bytes per cluster.
    // Multiple clusters share a 64-byte cache line but are independently indexed.
    struct cluster {
        uint64_t hashes[2];  // 2 × 8 bytes = 16 bytes; 0 = empty slot
    };

    explicit hash_only_cache(uint64_t max_entries);

    bool insert(const game_state& gs) override;
    bool contains(const game_state& gs) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t get_states_removed_from_cache() const override;
    uint64_t bucket_count() const override;

private:
    uint64_t cluster_index(uint64_t hash) const;
    std::vector<cluster> clusters;
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};
```

Key design decisions:
- **2-way TwoBig1, same as existing flat_cache.** The density gain comes from 8-byte
  entries (4× more clusters than the 32-byte flat_cache for the same memory), not from
  higher associativity. More clusters = better hash distribution. Same proven replacement
  policy. No depth field available in an 8-byte entry, so TwoBig1 simplifies to:
  slot 0 = always-replace with preference for the entry that has been there longer
  (approximating depth preference), slot 1 = always-replace.
- **Empty sentinel:** hash value 0 means empty. If a game state hashes to 0, store 1
  instead (one bit of discrimination lost, negligible impact).
- **No payload access:** `insert()` and `contains()` call only `gs.get_zobrist_hash()`,
  never `gs.get_payload()`. This is the key performance win — no payload construction.

#### A2. CLI integration

**File:** `src/main/input-output/input/command_line_helper.h/cpp`

Add `--cache-type` option with values: `auto` (default, current behaviour), `hash-only`.

**Files:** `src/main/main.cpp`, `src/main/evaluation/solvability_calc.cpp`,
`src/main/evaluation/benchmark.cpp`

Where cache is constructed, add:
```cpp
if (cache_type == "hash-only") {
    cache_ptr = std::make_unique<hash_only_cache>(cache_capacity);
} else if (use_new_cache(rules, suit_sym) && !force_lru) {
    cache_ptr = std::make_unique<flat_cache>(cache_capacity);
} else {
    cache_ptr = std::make_unique<lru_cache>(gs, cache_capacity);
}
```

#### A3. Parameterised dual cache for validation

**File:** `src/main/game/dual_cache.h`

Generalise `dual_cache` to accept two `cache_interface` pointers rather than hardcoding
`lru_cache` + `flat_cache`:

```cpp
class dual_cache : public cache_interface {
public:
    dual_cache(std::unique_ptr<cache_interface> primary,
               std::unique_ptr<cache_interface> reference,
               const std::string& primary_name = "primary",
               const std::string& reference_name = "reference");
    // ... rest unchanged
private:
    std::unique_ptr<cache_interface> primary_;
    std::unique_ptr<cache_interface> reference_;
    std::string primary_name_;
    std::string reference_name_;
};
```

This allows testing any pair: hash-only vs flat, hash-only vs LRU, flat vs LRU, etc.

**Note:** The current `dual_cache` constructor takes `const game_state& gs` for
`lru_cache` construction. The parameterised version receives pre-constructed caches,
so this dependency moves to the caller. Existing unit tests must be updated.

#### A4. Unit tests

**File:** `src/test/unit_tests/hash_only_cache_test.cpp`

- Basic insert/contains/eviction for hash_only_cache
- **Primary dual-cache test: hash-only vs flat** on Klondike seeds, checking agreement
  pre-eviction. These use the same Zobrist hash for both bucket selection and matching,
  so they should agree perfectly pre-eviction. Any disagreement indicates a bug.
- Secondary: hash-only vs LRU for cross-validation (different hashing, so expect some
  divergence from hash collisions — informational, not a correctness gate)

#### A5. Benchmarking

Use the existing `run_benchmark.py` framework:
```bash
# Baseline (current flat cache)
python scripts/run_benchmark.py --game-type klondike --seeds 1-1000 \
    --timeout 60000 --output baseline.csv

# Hash-only
python scripts/run_benchmark.py --game-type klondike --seeds 1-1000 \
    --timeout 60000 --solver-args "--cache-type hash-only" --output hash_only.csv
```

Metrics to compare:
- `solve_time_ms` — expect improvement from no payload computation
- `states_searched` — expect improvement from higher density (fewer evictions)
- `states_removed_from_cache` — expect fewer evictions (4× density)
- `outcome` — expect identical for solvable games; track any unsolvable divergences
  (these would be the false positives we expect to be astronomically rare)

### Complexity Assessment

- **hash_only_cache.h/cpp:** ~80 lines. Straightforward — simpler than flat_cache.
- **CLI integration:** ~20 lines across 3 files. Mechanical.
- **Dual cache parameterisation:** ~40 lines refactor + test updates. Medium — the
  current `lru_cache` constructor dependency needs careful handling.
- **Tests:** ~100 lines. Follows existing patterns.

**Total:** ~240 lines of new/modified code. **Sonnet-appropriate** — well-defined scope,
follows existing patterns, no algorithmic novelty.

---

## Stream B: Accordion Predecessor Cache

### Goal

A new cache class `predecessor_cache` implementing the predecessor encoding (#24) for
accordion games. Validates the encoding, the predecessor-based Zobrist hashing, and the
O(1) update mechanics on the simplest possible game type.

### Why Start Here

- Accordion was the original inspiration for predecessor encoding (#23)
- Simplest game type: no foundations, no cells, no face-down cards, no stock
- Only one zone: tableau (the accordion row)
- Moves are merges: exactly 4 predecessor updates per move
- Small deck subset: typically 52 cards in a single row
- Existing test infrastructure: accordion preset, JSON instances available

### Implementation Steps

#### B1. Predecessor payload class

**File:** `src/main/game/predecessor_state.h`

```cpp
struct predecessor_state {
    uint8_t data[64];   // 64-byte cache-line-aligned entry

    // Byte 0: occupied flag
    // Byte 1: depth (packed to 1 byte, max 255)
    // Bytes 2-53: predecessor array (52 × 8 bits)
    //   Value 0-51: predecessor is card with that ID
    //   Value 52 (FINAL): card is in foundation/hole/buried
    //   Value 53 (IN_CELL): card is in a cell
    //   Value 54 (IN_STOCK): card is in stock
    //   Value 55 (IN_WASTE): card is in waste
    //   Value 56 (IN_RESERVE): card is in reserve
    //   Value 57 (STARTING): card has not moved from starting position
    //   Values 58-109 (PILE_0..PILE_51): bottom of tableau pile N
    //   Values 110-255: spare
    // Bytes 54-63: spare (future use: foundation state, waste ptr, etc.)

    enum zone_marker : uint8_t {
        FINAL       = 52,
        IN_CELL     = 53,
        IN_STOCK    = 54,
        IN_WASTE    = 55,
        IN_RESERVE  = 56,
        STARTING    = 57,
        PILE_0      = 58,
        // PILE_N = 58 + N
    };

    void clear();
    void set_occupied(bool occ);
    bool is_occupied() const;
    void set_depth(uint8_t d);
    uint8_t get_depth() const;

    void set_predecessor(uint8_t card_id, uint8_t pred_value);
    uint8_t get_predecessor(uint8_t card_id) const;

    // Compare payloads (bytes 2-53, excluding occupied and depth)
    bool matches(const predecessor_state& other) const;
};
```

#### B2. Predecessor-based Zobrist hash

**File:** `src/main/game/search-state/game_state.h/cpp`

New members alongside existing Zobrist infrastructure:
```cpp
// Predecessor Zobrist table: Z_pred[card_id][predecessor_value]
static uint64_t Z_pred[52][110];
static bool Z_pred_initialised;

// Current predecessor array (mirrors predecessor_state for incremental updates)
uint8_t predecessor[52];

// Predecessor-based Zobrist hash (maintained incrementally)
uint64_t predecessor_zobrist_hash;
```

Initialisation: fill `Z_pred` with random 64-bit values (same PRNG approach as
existing `zobrist_` tables in game_state.cpp).

Update on accordion merge (card A merges onto card B's position):
```cpp
void update_predecessor_for_accordion_merge(
    uint8_t moved_card,       // card that moved
    uint8_t buried_card,      // card that got buried
    uint8_t right_of_moved,   // card that was to moved card's right (closes gap)
    uint8_t right_of_buried   // card that was to buried card's right (new neighbour)
) {
    // 1. Buried card → FINAL (no longer visible)
    update_predecessor(buried_card, predecessor_state::FINAL);
    // 2. Moved card gets buried card's old predecessor
    update_predecessor(moved_card, old_pred_of_buried);
    // 3. Right-of-moved gets moved card's old predecessor (closing the gap)
    update_predecessor(right_of_moved, old_pred_of_moved);
    // 4. Right-of-buried gets moved card as predecessor
    update_predecessor(right_of_buried, moved_card);
}

void update_predecessor(uint8_t card_id, uint8_t new_pred) {
    uint8_t old_pred = predecessor[card_id];
    predecessor_zobrist_hash ^= Z_pred[card_id][old_pred];
    predecessor_zobrist_hash ^= Z_pred[card_id][new_pred];
    predecessor[card_id] = new_pred;
}
```

#### B3. Predecessor flat cache class

**File:** `src/main/game/predecessor_flat_cache.h/cpp`

Same structure as `flat_cache` but with `predecessor_state` entries and 2-way
128-byte clusters with hash guard (#25). Each cluster spans two cache lines:

- **Cache line 1 (bytes 0–63):** Entry 0 payload (56 bytes) + Entry 1 Zobrist hash (8 bytes)
- **Cache line 2 (bytes 64–127):** Entry 1 payload (56 bytes) + Entry 0 Zobrist hash (8 bytes)

On probe, the CPU loads cache line 1. If Entry 0 doesn't match, it compares the
probe hash against the stored Entry 1 hash (already in L1, zero cost). Only if
that matches (~1/2^64 false positive rate) does it fetch cache line 2 for full
payload verification. This gives 2-way associativity with virtually no extra
memory access cost over 1-way.

```cpp
class predecessor_flat_cache : public cache_interface {
public:
    struct alignas(64) cache_line {
        uint8_t payload[56];    // predecessor_state payload (excluding occupied/depth from header)
        uint64_t other_hash;    // Zobrist hash of the OTHER entry in this cluster
    };
    struct alignas(128) cluster {
        cache_line lines[2];    // 2 × 64 bytes = 128 bytes
    };
    // ... same interface as flat_cache
};
```

Replacement policy: TwoBig1, same as existing flat_cache — slot 0 is
depth-preferred, slot 1 is always-replace. Proven approach, and the 2-way
design means the same eviction logic applies directly.

**Rationale for 2-way over 1-way:** The hash guard (#25) was designed precisely
for this — it eliminates the second cache line fetch for non-matching probes,
so 2-way 128-byte clusters have the memory access profile of 1-way 64-byte
clusters but with 2× the entries and depth-preferred retention.

#### B4. Wire into game_state for accordion games

**File:** `src/main/game/search-state/game_state.cpp`

In the accordion move-execution code, add predecessor array and Zobrist hash updates.
The existing `do_move()` / `undo_move()` for accordion must be augmented:

- `do_move()`: update 4 predecessors (as above), maintaining Zobrist incrementally
- `undo_move()`: reverse the 4 predecessor updates (same XOR logic, idempotent)

**File:** `src/main/game/cache_interface.h`

Extend `use_new_cache()` or add `use_predecessor_cache()`:
```cpp
inline bool use_predecessor_cache(const sol_rules& rules) {
    return rules.accordion_size > 0;
}
```

**Files:** `src/main/main.cpp`, `solvability_calc.cpp`, `benchmark.cpp`

Add routing:
```cpp
if (use_predecessor_cache(rules)) {
    cache_ptr = std::make_unique<predecessor_flat_cache>(cache_capacity);
} else if (cache_type == "hash-only") {
    // ... etc
```

#### B5. Unit tests

**File:** `src/test/unit_tests/predecessor_cache_test.cpp`

- predecessor_state: set/get/matches for various card layouts
- Zobrist: verify incremental hash matches full recomputation after random move sequences
- Dual-cache: predecessor_flat_cache vs LRU on accordion seeds, agreement pre-eviction
- Full solver run: accordion games produce correct results with predecessor cache

#### B6. Integration tests

Use existing accordion test seeds. Verify outcomes match LRU cache results.

### Complexity Assessment

- **predecessor_state.h/cpp:** ~100 lines. Straightforward data structure.
- **Zobrist table + update logic:** ~80 lines in game_state. Follows existing patterns
  but requires understanding of accordion move mechanics.
- **predecessor_flat_cache.h/cpp:** ~120 lines. Follows flat_cache pattern with 2-way
  TwoBig1 clusters; hash guard adds ~15 lines for storing/checking the cross-entry hash.
- **game_state wiring:** ~60 lines. Must correctly identify the 4 cards affected per
  accordion merge. Requires careful reading of existing accordion move code.
- **Cache routing:** ~15 lines across 3 files. Mechanical.
- **Tests:** ~150 lines. Critical — dual-cache validation is the key deliverable.

**Total:** ~500 lines of new/modified code. **Mixed Sonnet/Opus** — the cache class and
routing are Sonnet-appropriate; the Zobrist integration with accordion move semantics
benefits from Opus for getting the 4-card update logic right on the first pass.

---

## Parallel Execution Plan

### Branch Setup

```bash
# Stream A
git checkout dev
git checkout -b implement-hash-only-cache

# Stream B (independent)
git checkout dev
git checkout -b implement-accordion-predecessor
```

### Agent Assignment

| Task | Agent | Rationale |
|---|---|---|
| **A1** hash_only_cache class | Sonnet | Simple data structure, follows flat_cache pattern |
| **A2** CLI integration | Sonnet | Mechanical wiring |
| **A3** Dual cache parameterisation | Sonnet | Refactor with clear spec, but needs care with existing test updates |
| **A4** Unit tests | Sonnet | Follows existing test patterns |
| **A5** Benchmarking | Manual | Run benchmarks, analyse results |
| **B1** predecessor_state class | Sonnet | Straightforward data structure |
| **B2** Zobrist hash + accordion wiring | **Opus** | Must correctly identify the 4-card update per merge; interacts with existing accordion move semantics in game_state.cpp |
| **B3** predecessor_flat_cache class | Sonnet | Follows flat_cache pattern; 128-byte cluster with hash guard adds modest complexity |
| **B4** game_state + cache routing | **Opus** | Wiring predecessor updates into do_move/undo_move; correctness-critical |
| **B5-B6** Tests | Sonnet (after B2/B4) | Test patterns are established; correctness verified by dual cache |

### Ordering Within Each Stream

**Stream A:** A1 → A2 → A3 → A4 → build & test → A5
(Fully sequential — each step depends on the previous. Simple enough to be one session.)

**Stream B:** B1 → B2+B3 in parallel → B4 → B5+B6 → build & test
(B2 and B3 are independent; B4 needs both; B5/B6 need B4.)

### Merge Strategy

1. Stream A merges to `dev` first (simpler, faster to validate)
2. Stream B merges to `dev` (may need trivial rebase if A touches cache routing)
3. Sync `dev` back into `refactor-caching`

---

## Success Criteria

### Stream A

- [ ] `hash_only_cache` passes all unit tests
- [ ] Dual-cache (hash-only vs flat) shows zero pre-eviction mismatches on Level 1
      regression seeds (same Zobrist hash → must agree perfectly)
- [ ] Benchmark shows measurable speedup (expect 10–30% from payload elimination +
      density improvement combined)
- [ ] Zero outcome divergences on 1000 Klondike seeds (confirms false positive rate
      is negligible)

### Stream B

- [ ] `predecessor_state` correctly encodes/decodes accordion states
- [ ] Incremental Zobrist hash matches full recomputation for all moves
- [ ] Dual-cache (predecessor vs LRU) shows zero pre-eviction mismatches on accordion
      test seeds
- [ ] All accordion integration tests produce correct outcomes
- [ ] Accordion games now route to flat predecessor cache instead of LRU

---

## Future Work (after A + B merge)

- **Extend predecessor encoding to tableau-dealing games** (east-haven, spiderette,
  will-o-the-wisp) — builds directly on B's infrastructure
- **Hash-only mode for predecessor cache** — combines A's hash-only concept with B's
  predecessor Zobrist hash
- **Gaps games** — predecessor encoding with grid-position markers
- **Suit symmetry** — sorted-class-tuple canonicalization (hardest; needs formal
  verification first)
