# Metamorphic Cache Testing

**Date:** 2026-03-23
**Context:** The caching refactoring (Milestones 2–8) gives us a window where two independent cache implementations coexist. This document describes how to exploit that for rigorous correctness testing.

---

## The Insight

For all games where `use_new_cache(rules)` returns true (single-deck, no sequences, no accordion), we have two fully independent cache pipelines:

| | **lru_cache** | **flat_cache** |
|---|---|---|
| State encoding | `cached_game_state`: variable-length vector of cards with pile dividers | `compact_state`: 32-byte fixed struct with 4-bit per-card descriptors |
| Hash function | `hasher`: card-level hash combining | `zobrist_hash`: descriptor-aligned Zobrist XOR |
| Equality check | `operator==` on card vectors | `matches()` on bytes 3–31 |
| Data structure | Boost MultiIndex (hash + LRU sequence) | Open-addressed two-slot clusters |

The DFS solver visits states in a **deterministic order** (same game, same seed, same rules → same sequence of `insert()` and `contains()` calls). Both caches see the identical stream of game states. Therefore:

> **Up to the first eviction in either cache, every `insert()` and `contains()` call must return the same boolean result in both caches.**

If the caches are large enough that no evictions occur during the entire solve, then they must agree on **every operation for the entire run**.

---

## Why This Is Powerful

### 1. Cross-validates two independent state representations

A bug in `compact_state` encoding (e.g., a descriptor not updated on a particular move type) would cause the flat cache to miss a state that the LRU cache hits, or vice versa. This is a much stronger test than checking either cache in isolation, because the two encodings were designed independently and share no code.

### 2. Cross-validates two independent hash functions

If the Zobrist hash has a collision where the old hasher doesn't (or vice versa), the caches would disagree on whether a state is "new" at the same point in the search. Pre-eviction, hash collisions don't affect correctness (both caches store exact states, not just hashes), but any encoding bug that makes two different states look identical in one representation but not the other would surface as a disagreement.

### 3. Catches subtle incremental-update bugs

The Zobrist hash and compact_state payload are incrementally maintained through `make_move`/`undo_move`. If any move type fails to update them correctly, the disagreement will surface as a hit/miss mismatch between the two caches. This is especially valuable for rare move types (e.g., stock recycles, cell-to-cell transfers) that may not be covered by simple unit tests.

---

## The Metamorphic Relations

### Relation 1: Pre-eviction agreement (strong)

Given the same game instance and solver configuration:
- Run DFS with both caches receiving the same stream of operations
- Until either cache performs its first eviction, every `insert()` must return the same bool and every `contains()` must return the same bool
- **Violation indicates a bug** in state encoding, hash function, or equality comparison

### Relation 2: Outcome agreement (weak)

Given the same game instance:
- The solvability outcome (SOLVED/UNSOLVABLE) must be identical regardless of which cache is used
- This holds even after evictions, because the solver is complete (it explores the full state space; eviction just means some states are re-explored)
- **Violation indicates a bug** in the cache or solver integration
- Note: TIMEOUT results may legitimately differ (different eviction patterns → different re-exploration → different timing)

### Relation 3: Zero-eviction full agreement (strongest)

If cache capacity is large enough that neither cache evicts:
- Every `insert()` and `contains()` call must agree for the **entire** run
- `size()` must be identical at every point
- The total number of unique states searched must be identical
- **This is a complete cross-validation of both pipelines**

---

## Implementation: Dual-Cache Wrapper

The cleanest implementation is a `dual_cache` class that wraps both caches and asserts agreement:

```cpp
#include "cache_interface.h"
#include "global_cache.h"
#include "flat_cache.h"
#include <cassert>
#include <iostream>

class dual_cache : public cache_interface {
public:
    dual_cache(const game_state& gs, uint64_t capacity)
        : lru(gs, capacity)
        , flat(capacity)
        , ops(0)
        , first_eviction_op(0)
        , eviction_occurred(false)
    {}

    bool insert(const game_state& gs) override {
        bool lru_result = lru.insert(gs);
        bool flat_result = flat.insert(gs);
        ops++;

        if (!eviction_occurred) {
            // Check if either cache just evicted
            if (lru.get_states_removed_from_cache() > 0 ||
                flat.get_states_removed_from_cache() > 0) {
                eviction_occurred = true;
                first_eviction_op = ops;
            }
        }

        if (!eviction_occurred) {
            // Pre-eviction: results MUST agree
            if (lru_result != flat_result) {
                std::cerr << "METAMORPHIC FAILURE at op " << ops
                          << ": insert() disagrees. LRU=" << lru_result
                          << " flat=" << flat_result << std::endl;
                assert(false && "Metamorphic cache test failed: insert disagreement");
            }
        }

        // Return the flat_cache result (or lru — they agree pre-eviction)
        return flat_result;
    }

    bool contains(const game_state& gs) const override {
        bool lru_result = lru.contains(gs);
        bool flat_result = flat.contains(gs);

        if (!eviction_occurred) {
            if (lru_result != flat_result) {
                std::cerr << "METAMORPHIC FAILURE at op " << ops
                          << ": contains() disagrees. LRU=" << lru_result
                          << " flat=" << flat_result << std::endl;
                assert(false && "Metamorphic cache test failed: contains disagreement");
            }
        }

        return flat_result;
    }

    void clear() override {
        lru.clear();
        flat.clear();
        ops = 0;
        eviction_occurred = false;
    }

    uint64_t size() const override {
        if (!eviction_occurred) {
            assert(lru.size() == flat.size() &&
                   "Metamorphic cache test failed: size disagreement");
        }
        return flat.size();
    }

    uint64_t get_states_removed_from_cache() const override {
        return flat.get_states_removed_from_cache();
    }

    uint64_t bucket_count() const override {
        return flat.bucket_count();
    }

    // Diagnostic accessors
    uint64_t get_ops() const { return ops; }
    uint64_t get_first_eviction_op() const { return first_eviction_op; }
    bool had_eviction() const { return eviction_occurred; }

private:
    lru_cache lru;
    flat_cache flat;
    uint64_t ops;
    uint64_t first_eviction_op;
    bool eviction_occurred;
};
```

### Usage in Testing

```cpp
// Large capacity → no evictions → full agreement for entire solve
game_state gs(rules, seed, streamliner_opts);
dual_cache cache(gs, 10000000);  // 10M entries — no eviction expected
solver sol(gs, cache);
auto result = sol.run(std::chrono::milliseconds(60000));
// If we get here without assertion failure, both caches agreed completely
```

### Caveat: Live-bit interaction

The `dual_cache` wrapper uses the polymorphic `cache_interface`, so the solver's LRU-specific code path (`insert_with_iterator`, `set_non_live`) would NOT be exercised. The solver would treat `dual_cache` like a flat_cache (no iterators, no live bits). This is fine for testing the **cache encoding and lookup** correctness, but does not test the LRU live-bit mechanism itself.

To also test the LRU path, you could run each cache separately and compare results afterward (Relation 2), or instrument `dual_cache` to also expose `insert_with_iterator` — but the simpler approach is sufficient for the encoding cross-validation, which is the main value.

---

## Test Strategy

### Phase 1: Large-cache full agreement (Milestone 5)

For each single-deck game type (FreeCell, Klondike, Black Hole, Spanish Patience, etc.), run 50+ seeds with a large cache (no evictions). Assert full pre-eviction agreement on every operation. This is the strongest test and should catch any encoding bugs.

### Phase 2: Small-cache outcome agreement (Milestone 5)

For the same games, run with small caches (force evictions). Verify solvability outcomes match between the two caches. This validates that eviction doesn't break solver completeness.

### Phase 3: Move-type coverage analysis (Milestone 5)

Identify which move types exercise which descriptor transitions. Ensure the test seeds collectively cover all move types for each game. The metamorphic test is only as strong as the move types exercised.

### Phase 4: Regression integration (Milestone 7)

Consider adding a `--dual-cache` debug flag that enables `dual_cache` for regression runs. This would give continuous metamorphic testing as part of the existing regression infrastructure.

---

## When This Window Closes

The dual-cache testing opportunity exists as long as both `lru_cache` and `flat_cache` coexist. Per the implementation plan, `lru_cache` stays for multi-deck/sequence/accordion games even after the refactoring is complete. So for single-deck games, we can keep the `dual_cache` test infrastructure indefinitely — it's always available for regression testing.

If `lru_cache` were ever fully removed, we would lose the cross-validation property but could preserve the test expectations (recorded hit/miss sequences) as golden files.
