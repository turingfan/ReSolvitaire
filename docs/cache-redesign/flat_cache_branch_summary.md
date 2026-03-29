# Flat Cache Branch Summary

**Branch:** `refactor-caching`
**Authors:** Ian Gent & AI Assistant (Claude)
**Date completed:** 2026-03-29
**Target merge:** `mac-dev`

This document summarises the design, implementation, and verification of the flat cache
introduced in the `refactor-caching` branch. It is intended to be self-contained and
suitable for incorporation into the documentation of other branches.

---

## 1. Motivation

The original Solvitaire transposition table (`lru_cache`) uses a Boost MultiIndex
container with a hashed index and a sequenced index for LRU eviction. Its state
representation (`cached_game_state`) is built by iterating piles, making it sensitive
to pile order: before any hash lookup, `eval_pile_order()` sorts the tableau piles into
a canonical order to ensure equivalent states map to the same key.

Two costs accumulate:

1. **Representation cost.** The LRU representation stores full pile contents; computing
   and comparing it is expensive.
2. **Pile-ordering cost.** Sorting tableau piles after every card placement/removal is
   O(k log k) in the number of piles — significant for games with 10+ tableau columns,
   and completely redundant for games that don't need it.

The goal of this branch was to replace the LRU cache with a flat, open-addressed hash
table (`flat_cache`) backed by a compact 32-byte state representation that is inherently
pile-order invariant.

---

## 2. Key Design Decisions

### 2.1 Per-Card Descriptor Encoding (compact_state)

Rather than encoding pile contents, the payload encodes each card's **relationship to
its context** — a 4-bit descriptor per card (52 cards × 4 bits = 26 bytes of card data).
Pile indices never appear in the encoding. Two states that are identical up to tableau
pile permutation therefore produce identical payloads automatically, without any sorting.

| Value | Name | Meaning |
|---|---|---|
| 0 | STARTING | Face-down (or in stock/waste/reserve) |
| 1 | STARTING_FACE_UP | Revealed, not yet moved, not at pile bottom |
| 2 | ROOT | On a non-legal-build parent (fallback) |
| 3 | IN_CELL | In a free cell |
| 4–7 | PARENT_0–3 | Built on legal parent (by fixed suit ordering) |
| 8 | IN_HOLE | Played to hole |
| 9 | IN_SPACE | At the bottom of a tableau pile (empty space below) |

The full 32-byte payload also encodes foundation tops (or hole top), and the waste
pointer. Depth (2 bytes) is stored but excluded from comparison.

The key insight driving this design: stock, waste, and reserve cards are always in their
starting positions and need no per-card encoding beyond the waste pointer. A card's
history does not need to be recoverable from the payload — only that two equivalent game
states produce the same bytes.

### 2.2 Descriptor-Aligned Zobrist Hashing

The Zobrist hash table is `Z_card[52][16]` — one 64-bit random value per (card,
descriptor) pair. Hash and payload are updated together in the same code path whenever a
descriptor changes. Because descriptors never reference pile indices, the hash is also
pile-order invariant.

Total table size: ~8 KB, fitting in L1 cache. This replaces a prior two-layer scheme
involving per-pile hashes and additive combining, which was both larger and more complex.

### 2.3 Flat Open-Addressed Cache

`flat_cache` is a fixed-size open-addressed hash table of 32-byte slots. Insertion uses
the TwoBig1 replacement policy: a cluster of two slots is indexed by the hash; the
shallower of the two incumbents is evicted when both are occupied. This biases retention
towards deeper (harder-to-reach) states.

The cache does not use live bits or separate path tracking. States are inserted on
forward visit. Backtracking does not remove them. This is correct because false negatives
(missing a cache hit) only cause redundant re-exploration; false positives (incorrectly
identifying distinct states as equal) would cause correctness failures.

### 2.4 Pile Ordering Removed for Flat-Cache Games

Because the flat cache is pile-order invariant, `eval_pile_order()` is no longer called
for games using the flat cache (M6). This eliminates the O(k log k) sort on every card
placement/removal. `--force-lru` restores the old behaviour for comparison.

### 2.5 Scope: Which Games Use the Flat Cache

```cpp
inline bool use_new_cache(const sol_rules& rules) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0
        && (rules.stock_size == 0
            || rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);
}
```

Excluded: two-deck games, sequence games, accordion games, and spider-type stock dealing
(which distributes cards across tableau piles, breaking the per-card descriptor model's
pile symmetry assumptions). Extension to excluded types is planned post-merge.

---

## 3. Implementation Overview

Work was structured in eight milestones (M0–M8):

| Milestone | Status | Summary |
|---|---|---|
| M0 | ✓ | Baseline and test infrastructure |
| M1 | ✓ | Abstract `cache_interface`; `use_new_cache()` |
| M2 | ✓ | Descriptor-aligned Zobrist hash and compact payload |
| M3 | ✓ | `flat_cache` implementation |
| M4 | ✓ | Solver wired to new cache |
| M5 | ✓ | Verification and hardening (bugs fixed, sanitizers, debug assertions) |
| M6 | ✓ | Pile ordering removed for flat-cache games |
| M7 | In progress | Benchmarking (done); oracle regeneration (done); tuning (deferred) |
| M8 | Not started | Final cleanup and merge |

Key new files: `cache_interface.h`, `zobrist.h/cpp`, `compact_state.h/cpp`,
`parent_table.h/cpp`, `flat_cache.h/cpp`, `dual_cache.h/cpp`.

See `docs/cache-redesign/active/implementation_plan_v4.md` for the full file list and
detailed milestone notes.

---

## 4. Correctness Verification

### 4.1 Dual-Cache Metamorphic Testing

`dual_cache` wraps a flat cache and an LRU cache side-by-side. On every insert it
checks both caches agree. Pre-eviction agreement is guaranteed by construction (both
caches are pile-order invariant). Post-eviction divergences are acceptable.

Nine game types are tested: FreeCell, BakersGame, EightOff, SpanishPatience, Somerset,
FlowerGarden, FortunesFavor, SeahavenTowers, Klondike. All pass with zero
pre-eviction mismatches.

The `mismatch_diagnostic` tool provides detailed per-card descriptor diffs at the first
point of divergence, and was instrumental in locating several of the bugs below.

### 4.2 Bugs Found and Fixed

Four correctness bugs were found during M5 verification:

1. **STARTING(0) not position-canonical** — init assigned `STARTING` to all face-up
   tableau cards; moves computed `PARENT_x`. Cards moved away and returned diverged.
   Fix: init computes positional descriptors matching `determine_destination_descriptor()`.

2. **ROOT descriptor overloaded** — `ROOT` meant both "at pile bottom" and "on a
   non-legal-build parent." False positives resulted. Fix: new `IN_SPACE(9)` descriptor
   for pile-bottom cards.

3. **Waste pointer stale on regular moves** — `make_regular_move()` did not update the
   waste pointer when the source pile was `waste` (FortunesFavor `auto-waste-then-stock`).

4. **Canfield wrapping builds not recognised** — `parent_table::get_parents()` used
   hard-coded `rank + 1` with no wrapping. King-base games (e.g., canfield-strict) use
   King→Ace→2→…→Queen build order; Kings had no parents, causing ROOT fallback for
   legally built cards.

Full details: `docs/resolved-bugs/`.

### 4.3 Sanitizer Runs

- **AddressSanitizer:** Clean. No memory errors.
- **UndefinedBehaviorSanitizer:** One real bug found and fixed — `sol_rules::sol_rules()`
  missing `foundations_removable`, `stock_redeal`, `sequence_fixed_suit` from its
  initializer list (uninitialized bools, detected in unit tests). A RapidJSON
  null-pointer-offset pattern is suppressed as a known third-party false positive.

### 4.4 Debug Assertion: Payload Consistency

`assert_payload_consistent()` (debug builds only) recomputes the entire payload from
scratch after every flat-cache insert and asserts it matches the incrementally-maintained
one. This catches any future divergence between `make_move`/`undo_move` and the initial
`init_payload_and_hash()`. Skipped for `face_up_policy::TOP_CARDS` games where
`STARTING_FACE_UP` is ambiguous without move history.

---

## 5. Performance Results

Benchmarked on 2026-03-28 (Apple Silicon MacBook Pro, release build).
Scripts in `scripts/benchmark_speedup.sh` and `benchmark_baseline.sh`.
Results archived in `docs/cache-redesign/benchmarks/`.

| Game | Nodes/sec improvement | Wall-time improvement |
|---|---|---|
| FreeCell | ~2× | ~2× |
| Klondike | ~2× | ~2× |
| SpanishPatience | ~2–3× | ~3× |
| Somerset (10 piles) | ~2–3× | **83×** |

Somerset's 83× wall-time speedup reflects that it has 10 tableau piles, where the
pile-ordering overhead was highest. The nodes/sec gain (2–3×) is consistent across
all flat-cache games; the additional wall-time multiplier comes from pile-ordering removal.

---

## 6. Regression Test Results

All five regression levels pass under outcome-only comparison (see
`docs/regression_suite_guide.md` for the full comparison policy):

| Level | Instances | Result | Time |
|---|---|---|---|
| 1 | 150 | 100% pass | ~3.5 min |
| 2 | 160 | 100% pass | ~1.5 min |
| 3 | 160 | 100% pass | ~2.5 min |
| 4 | 160 | 100% pass | ~15 min |
| 5 | 160 | 100% pass | ~1.25 hr |

No OUTCOME FLIP failures (SOLVED↔UNSOLVABLE) at any level. Some instances produce
TIMEOUT/SOFT-PASS due to traversal order changes (pile ordering removal alters DFS
order for some games); these are accepted.

---

## 7. Deferred Work

The following items are explicitly out of scope for this delivery and planned for
subsequent work:

- **Performance tuning:** Replacement policy comparison (TwoBig1 vs always-replace vs
  depth-only), nibble accessor profiling, 64-bit hash reduction consideration.
- **Flat cache extension:** Extending `use_new_cache()` to two-deck games, spider-type
  stock dealing, sequences, and accordion. Planned as a follow-on branch after merge.
- **Spanish Patience traversal:** Some solvable instances explore many more nodes
  without pile ordering. A lightweight move-ordering heuristic could help; deferred.

---

## 8. Files Added or Modified

See `docs/cache-redesign/active/implementation_plan_v4.md` Appendix A for the complete
list. Key additions:

| File | Purpose |
|---|---|
| `src/main/game/cache_interface.h` | Abstract interface + `use_new_cache()` |
| `src/main/game/zobrist.h/cpp` | Descriptor-aligned Zobrist key tables |
| `src/main/game/compact_state.h/cpp` | 32-byte payload |
| `src/main/game/parent_table.h/cpp` | Parent card lookup (PARENT_0–3 descriptors) |
| `src/main/game/flat_cache.h/cpp` | Flat open-addressed cache |
| `src/main/game/dual_cache.h/cpp` | Dual-cache for metamorphic testing |
| `scripts/regression_runner.py` | Regression runner (outcome-only policy, `--regenerate` mode) |
| `docs/regression_suite_guide.md` | Regression suite documentation (merge candidate) |
| `docs/resolved-bugs/` | Three detailed bug reports (merge candidates) |
