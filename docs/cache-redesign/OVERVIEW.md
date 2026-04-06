# Cache Redesign: Project Overview

**Branch:** `refactor-caching` (and experimental sub-branches)
**Authors:** Ian Gent & AI Assistants (Claude, Gemini)
**Date completed:** April 2026

This document is a self-contained narrative of the full cache redesign effort on
`ReSolvitaire-caching`. It is intended as the entry point for anyone wanting to understand
what was done, why, and what the current state is.

---

## 1. Background and Motivation

The original transposition table in Solvitaire (`lru_cache`) uses a Boost MultiIndex
container holding full pile contents (`cached_game_state`). Two costs accumulate on every
cache operation:

1. **Representation cost.** Building the LRU key requires iterating all piles.
2. **Pile-ordering cost.** Before any lookup, the solver calls `eval_pile_order()` to
   sort the tableau into a canonical order (so that equivalent states that differ only in
   pile order hash to the same key). For games with 10+ tableau piles this is significant.

The goal of the `refactor-caching` branch was to replace this with a flat, open-addressed
hash table (`flat_cache`) backed by a compact per-card descriptor encoding that is
inherently pile-order invariant — eliminating the sort entirely for eligible games.

The project split into three phases:

- **Phase 1 (M0–M8):** Implement and validate the flat cache on `refactor-caching`.
- **Phase 2, Stream A:** Experiment with a hash-only variant (`hash_only_cache`) that
  stores only the 64-bit Zobrist hash (no payload), giving 4× density.
- **Phase 2, Stream B:** Extend the flat-cache approach to accordion games via a new
  `predecessor_flat_cache` that encodes the card-beneath-each-card relationship.

Streams A and B were developed in parallel on separate branches, then merged and
integrated back to `refactor-caching`.

---

## 2. Phase 1: Flat Cache (M0–M8)

### 2.1 Core Design

The key insight is a **per-card descriptor encoding** (`compact_state`): instead of
storing pile contents, the payload encodes each card's _relationship to its context_ in
4 bits. There are 52 cards × 4 bits = 26 bytes of card data, plus foundation/hole tops
and waste pointer. Total: 32 bytes per state.

| Descriptor | Name | Meaning |
|---|---|---|
| 0 | STARTING | Face-down or in stock/waste/reserve |
| 1 | STARTING_FACE_UP | Revealed, not yet moved, not at pile bottom |
| 2 | ROOT | On a non-legal-build parent (fallback) |
| 3 | IN_CELL | In a free cell |
| 4–7 | PARENT_0–3 | Built on legal parent (by fixed suit ordering) |
| 8 | IN_HOLE | Played to hole |
| 9 | IN_SPACE | At the bottom of a tableau pile (empty space below) |

Because pile indices never appear, two states differing only in tableau pile order produce
identical payloads automatically. The Zobrist hash table is `Z_card[52][16]` — one 64-bit
value per (card, descriptor) pair, ~8 KB total — and is updated incrementally whenever a
descriptor changes. Hash and payload are always consistent.

`flat_cache` is a fixed-size open-addressed hash table of 64-byte clusters (two 32-byte
slots). Insertion uses the **TwoBig1** replacement policy: when both slots are occupied,
the shallower state is evicted, biasing retention towards deeper (harder-to-reach) states.

### 2.2 Milestone Summary

| M | Title | Status |
|---|---|---|
| 0 | Baseline and test infrastructure | ✓ |
| 1 | Abstract `cache_interface`; `use_new_cache()` | ✓ |
| 2 | Descriptor-aligned Zobrist hash and compact payload | ✓ |
| 3 | `flat_cache` implementation | ✓ |
| 4 | Solver wired to new cache | ✓ |
| 5 | Verification and hardening (bugs, sanitizers, assertions) | ✓ |
| 6 | Pile ordering removed for flat-cache games | ✓ |
| 7 | Benchmarking and oracle regeneration | ✓ |
| 8 | Documentation and merge preparation | In progress |

### 2.3 Scope

```cpp
inline bool use_new_cache(const sol_rules& rules, bool suit_symmetry_active = false) {
    return !suit_symmetry_active
        && !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0
        && (rules.stock_size == 0
            || rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);
}
```

Excluded from the flat cache: two-deck games, sequence games, accordion games, and
spider-type stock dealing. Extension to excluded types is planned post-merge.
Suit-symmetry streamliner also falls back to `lru_cache` because the flat cache hashes on
actual card identity, not suit-normalised identity.

### 2.4 Correctness Verification

**Dual-cache metamorphic testing.** `dual_cache` wraps two `cache_interface`
implementations side-by-side, asserting agreement on every insert before eviction occurs.
Nine game types passed with zero pre-eviction mismatches:
FreeCell, BakersGame, EightOff, SpanishPatience, Somerset, FlowerGarden,
FortunesFavor, SeahavenTowers, Klondike.

**Bugs found and fixed during M5:**

1. *STARTING not position-canonical* — init assigned `STARTING` to all face-up tableau
   cards, causing divergence when cards were moved and returned. Fix: init uses
   `determine_destination_descriptor()` to match the incremental path.
2. *ROOT descriptor overloaded* — `ROOT` was used for both "pile bottom" and "non-legal
   parent". Fix: new `IN_SPACE(9)` for pile-bottom cards.
3. *Waste pointer stale on regular moves* — `make_regular_move()` did not update the
   waste pointer for `auto-waste-then-stock` games (FortunesFavor).
4. *Canfield wrapping builds not recognised* — `parent_table` used hard-coded `rank+1`
   with no wrapping; King→Ace→2…Queen builds fell back to ROOT.

**Sanitizer runs:** AddressSanitizer clean. UBSan found one real bug (uninitialized bools
in `sol_rules` constructor); one suppressed RapidJSON false positive.

**Debug assertion:** `assert_payload_consistent()` recomputes the payload from scratch
after every flat-cache insert in debug builds, catching any future divergence between the
incremental and from-scratch paths.

### 2.5 Performance Results (M7)

Benchmarked on Apple Silicon MacBook Pro, 2026-03-28, release build:

| Game | Nodes/sec improvement | Wall-time improvement |
|---|---|---|
| FreeCell | ~2× | ~2× |
| Klondike | ~2× | ~2× |
| SpanishPatience | ~2–3× | ~3× |
| Somerset (10 piles) | ~2–3× | **83×** |

Somerset's 83× wall-time speedup reflects 10 tableau piles where pile-ordering overhead
was highest. The nodes/sec gain (~2–3×) is consistent; the additional wall-time
multiplier comes from removing the `O(k log k)` sort (M6).

All five regression levels (150–160 instances each) pass under outcome-only comparison.
No SOLVED↔UNSOLVABLE flips at any level.

---

## 3. Phase 2, Stream A: Hash-Only Cache

**Branch:** `implement-hash-only-cache`

### 3.1 Motivation

`flat_cache` stores 64 bytes per cluster (two 32-byte payload slots). The per-card
descriptor payload is needed for collision detection: two states with the same Zobrist
hash but different payloads are distinguished correctly. The question was: could we trade
this safety for 4× density by storing only the 64-bit hash?

### 3.2 Implementation

`hash_only_cache` uses 16-byte clusters (two 8-byte hash slots). Insertion uses TwoBig1
replacement with a **hash guard**: a cached copy of the sibling slot's hash is kept in
memory to avoid a second DRAM fetch when checking the second slot. Sentinel normalisation:
hash value 0 is treated as empty; the actual hash 0 is stored as 1 (acceptable — 1/2⁶⁴
collision risk).

Memory footprint: 1.6 GB at 100M capacity vs 6.4 GB for `flat_cache` at the same
capacity. Initialization time: ~85 ms vs ~260 ms.

### 3.3 Benchmark Results

Easy instances (<1s) show large apparent speedup dominated by the 175ms initialization
advantage. The real signal comes from harder instances:

| Difficulty (baseline time) | Instances | Speedup |
|---|---|---|
| < 1s | 19 | 2.15× (init-dominated) |
| 1–5s | 62 | 1.14× |
| 5–10s | 28 | 1.10× |
| > 10s | 5 | 2.23× (inflated by collisions — see below) |

**Raw throughput gain (NPS):** 1,943,092 → 2,214,907 = **+14%**

### 3.4 Hash Collision Evidence

Level 4 node-count analysis (comparing `flat_nodes` vs `hash_nodes` per instance):
112 of 114 instances show near-perfect agreement (ratio ≈ 1.000). Two clear outliers:

| Instance | Flat nodes | Hash-only nodes | Ratio |
|---|---|---|---|
| raglan seed 1202 | 26,850,474 | 4,374,208 | **6.14×** |
| east-haven seed 871030 | 11,474,478 | 3,473,070 | **3.30×** |

The hash-only cache falsely reported states as already visited, causing the solver to
prune large subtrees. Both instances produced correct outcomes coincidentally. In general,
false-positive hash collisions can cause incorrect SOLVED/UNSOLVABLE verdicts.

**Conclusion:** Hash-only is suitable for performance-oriented exploration (benchmarking,
approximate search). It is not suitable for correctness-critical solving without an
additional collision-detection mechanism (e.g., a partial state fingerprint).

---

## 4. Phase 2, Stream B: Predecessor Cache for Accordion

**Branch:** `implement-accordion-predecessor`

### 4.1 Motivation

Accordion games were excluded from `flat_cache` because the per-card descriptor encoding
does not capture the identity of the card _beneath_ each card. In accordion, a move
removes a card from one pile onto another, and whether a subsequent move is legal depends
on what is now on top of (and beneath) each pile. Two states may have the same descriptor
for every card but different neighbourhood relationships — requiring a new encoding.

### 4.2 Predecessor State Encoding

Each card tracks its **predecessor**: the card directly beneath it in the pile (or a
sentinel if it is at the pile bottom). This is an 8-bit value per card. Combined with the
Zobrist hash (XOR of predecessor-pair values), this gives a 64-byte cluster with a
128-bit state payload and a hash guard.

Two additional Zobrist tables: `Z_predecessor[52][53]` (one entry per card × card-or-sentinel),
and `Z_no_predecessor[52]` (for pile-bottom cards). Updates are incremental: a move
involving positions i and j touches at most 4 predecessor relationships (the moved card,
its old successor, its new successor, and the displaced card). The `undo_move` path
maintains a per-frame undo stack.

The cache is routed by `use_predecessor_cache(rules)`:

```cpp
inline bool use_predecessor_cache(const sol_rules& rules) {
    return rules.accordion_size > 0;
}
```

### 4.3 Implementation Bugs Fixed

Three algorithmic bugs were found and fixed during development:

1. *Broken chain initialisation* — `init_predecessor_state` did not advance the
   `prev_top_cid` pointer in its loop. Every card was recorded as having `PILE_0` as
   predecessor. Fix: advance the tracker correctly.
2. *Incorrect neighbour update on 1-left moves* — `make_accordion_move` updated the
   predecessor of the card to the right of the moved card unconditionally, even for 1-left
   moves where no gap forms on the right. Fix: only update the right-hand neighbour when
   a gap is created.
3. *Hash guard tracking off-by-one* — the `other_hash` value in `predecessor_flat_cache`
   stored the wrong slot's hash. Fix: store the sibling hash correctly.

A fourth issue was found in the `game_state` seed constructor: it used an early return
`if (rules.tableau_pile_count == 0) return;` that exited before `init_predecessor_state()`
was reached for accordion games (which have no tableau piles). Fix: wrap the tableau
dealing loop in `if (rules.tableau_pile_count > 0)` instead of using an early return.

### 4.4 Correctness Verification

The `PredecessorDualCacheTest.AccordionAgreement` test runs 10 accordion seeds through
`dual_cache` with `predecessor_flat_cache` as primary and `lru_cache` as reference.
Result: 0 pre-eviction mismatches across all seeds. Regression levels 1–3 all pass.

---

## 5. Integration: Merge and Cache Factory

### 5.1 Merge History

After both streams were independently validated:

1. **Stream A → Stream B**: `implement-hash-only-cache` merged into
   `implement-accordion-predecessor`. Five files had conflicts, all involving the
   cache-type dispatch (`predecessor_flat_cache.h` vs `hash_only_cache.h`). Resolution:
   include both headers; use a 4-way selection order.

2. **Stream A+B → `refactor-caching`**: Clean merge (no conflicts). All 54 unit tests
   pass on the merged branch.

### 5.2 Cache Factory

The 4-way cache selection logic was previously duplicated across four call sites:
`main.cpp::solve_game()`, `solvability_calc.cpp::solve_seed()`,
`benchmark.cpp::run()`, and `benchmark.cpp::run_json()`.

`src/main/game/cache_factory.h` centralises this into a single inline function:

```cpp
std::unique_ptr<cache_interface> make_cache(
        const sol_rules& rules,
        const game_state& gs,
        uint64_t capacity,
        const std::string& cache_type = "auto",
        bool force_lru = false,
        bool suit_sym = false);
```

Selection order:

1. `cache_type == "hash-only"` → `hash_only_cache` (explicit opt-in; collision risk)
2. `use_predecessor_cache(rules) && !force_lru` → `predecessor_flat_cache`
3. `use_new_cache(rules, suit_sym) && !force_lru` → `flat_cache`
4. fallback → `lru_cache`

### 5.3 Dual-Cache Parameterisation

`dual_cache` was refactored (during Stream A) from hardcoded `lru_cache + flat_cache` to
accept any two `unique_ptr<cache_interface>` pair. This was then ported to Stream B,
enabling the predecessor agreement test to use the same infrastructure:

```cpp
dual_cache cache(
    std::make_unique<predecessor_flat_cache>(cap),
    std::make_unique<lru_cache>(gs, cap),
    "predecessor_flat", "lru"
);
```

---

## 6. Development Model

This project used an AI-assisted development model throughout. The primary human
contributor (Ian Gent) provided the architectural insights (listed in
`docs/cache-redesign/active/human_contributions.md`) and made all final decisions. AI
assistants (Claude, Gemini) implemented, debugged, tested, and documented.

Parallel-stream work in Phase 2 was handled by separate AI instances on separate git
branches. Coordination (deciding what to merge, resolving conflicts, evaluating quality
of AI-generated work) was handled by Claude in the main conversation.

Key lessons from the parallel-stream workflow:

- **Code drift is real.** Without careful coordination, streams diverged (e.g., both
  added `dual_cache` parameterisation independently in different styles). The merge
  required choosing one version and discarding the other.
- **Dead code accumulates.** `predecessor_dual_cache.h` was created by Gemini as a
  separate class wrapping `dual_cache`, despite the test file already using `dual_cache`
  directly. It was deleted twice.
- **Verbose debug output needs discipline.** Gemini added unconditional full-state dumps
  on every mismatch in `dual_cache.h`. These were removed before merge.

---

## 7. Known Issues and Deferred Work

| Issue | Status | Notes |
|---|---|---|
| `run_json()` missing `force_lru` | Open | `benchmark.cpp::run_json()` always uses the specialised cache; `--force-lru` has no effect for JSON-benchmarked accordion games. `make_cache` is already in place; adding the parameter is a one-liner. |
| `--benchmark` passthrough args | Parked | `run_benchmark.py` has no mechanism to forward arbitrary solver flags (e.g., `--cache-type`) without modifying the script. TODO added in script docstring. |
| Hash collision detection for `hash_only_cache` | Deferred | Adding a partial state fingerprint (e.g., 16 bits of payload) would make the cache collision-safe at modest cost. |
| Flat cache extension to excluded game types | Deferred | Two-deck, sequence, spider-stock, suit-symmetry games are planned for a follow-on branch. |
| Performance tuning | Deferred | Replacement policy comparison, nibble accessor profiling, possible 64-bit hash reduction. |

---

## 8. File Inventory (new or significantly modified)

| File | Purpose |
|---|---|
| `src/main/game/cache_interface.h` | Abstract interface; `use_new_cache()`; `use_predecessor_cache()` |
| `src/main/game/cache_factory.h` | `make_cache()` — single cache selection point |
| `src/main/game/zobrist.h/cpp` | Descriptor-aligned Zobrist key tables |
| `src/main/game/compact_state.h/cpp` | 32-byte flat-cache payload |
| `src/main/game/parent_table.h/cpp` | Parent-card lookup (PARENT_0–3 descriptors) |
| `src/main/game/flat_cache.h/cpp` | Flat open-addressed cache (64-byte clusters, TwoBig1) |
| `src/main/game/hash_only_cache.h/cpp` | Hash-only cache (16-byte clusters, 4× density) |
| `src/main/game/predecessor_state.h/cpp` | Per-card predecessor encoding for accordion |
| `src/main/game/predecessor_flat_cache.h/cpp` | Predecessor cache (128-byte clusters, hash guard) |
| `src/main/game/dual_cache.h` | Parameterised metamorphic testing wrapper |
| `src/test/unit_tests/dual_cache_test.cpp` | Flat vs LRU agreement tests (9 game types) |
| `src/test/unit_tests/hash_only_cache_test.cpp` | Hash-only unit tests + dual-cache agreement |
| `src/test/unit_tests/predecessor_dual_cache_test.cpp` | Predecessor vs LRU agreement test |
| `src/test/unit_tests/mismatch_diagnostic.cpp` | Diagnostic tool for investigating mismatches |
| `scripts/regression_runner.py` | Regression harness (outcome-only policy, `--regenerate`) |
| `scripts/run_benchmark.py` | Python benchmark orchestration (CSV output, R summary) |
| `scripts/analyze_level4.py` | Level 4 node-count ratio analysis (collision detection) |
| `docs/cache-redesign/flat_cache_branch_summary.md` | Self-contained flat cache summary (merge candidate) |
| `docs/stream-a-hash-only/STREAMA_REPORT.md` | Stream A final report |
| `docs/stream-a-hash-only/level4_benchmark_report.md` | Level 4 hard-instance analysis |
| `docs/stream-b-accordion-predecessor/KNOWN_ISSUES.md` | Stream B known issues log |
| `docs/regression_suite_guide.md` | Regression suite documentation (merge candidate) |
