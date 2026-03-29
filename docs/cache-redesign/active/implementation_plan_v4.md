# ReSolvitaire Cache Refactor: Implementation Plan v4

**Version:** 4.0
**Date:** 28 March 2026
**Authors:** Ian Gent & AI Assistant
**Reference Documents:**
- `specs/main.pdf` — Architecture and Payload Specification
- `specs/descriptor_zobrist.pdf` — Descriptor-Aligned Zobrist Hashing
- `archive/implementation_plan_v1.md` — Original plan (Milestones 0–1 completed)
- `archive/implementation_plan_v2.md` — Previous plan (Milestones 2–4 completed)
- `archive/implementation_plan_v3.md` — Previous plan (Milestones 5–6 completed)

---

## Status Summary

| Milestone | Title | Status |
|---|---|---|
| 0 | Baseline and test infrastructure | ✓ COMPLETE |
| 1 | Abstract cache interface | ✓ COMPLETE |
| 2 | Descriptor-aligned Zobrist hash and compact payload | ✓ COMPLETE |
| 3 | Flat cache implementation | ✓ COMPLETE |
| 4 | Wire solver to new cache | ✓ COMPLETE |
| 5 | Verification and hardening | ✓ COMPLETE |
| 6 | Remove pile ordering for flat-cache games | ✓ COMPLETE |
| 7 | Performance benchmarking and tuning | In progress — benchmarks done; oracle regeneration is the blocking task |
| 8 | Documentation and merge preparation | Not started |

---

## Key Design: Descriptor-Aligned Zobrist Hashing

The Zobrist hash is aligned directly with the payload's per-card descriptor values:

- **Zobrist key table:** `Z_card[52][16]` — one random 64-bit value per (card, descriptor)
  pair. Plus small tables for foundation tops, waste pointer, and hole top. Total ~8 KB,
  fits in L1 cache.
- **No per-pile hashes.** Symmetry invariance is automatic — descriptors never reference
  pile indices.
- **Unified update path:** Hash and payload updated together in the same code path.

---

## Descriptor Values

Current descriptor semantics (unchanged from v3):

| Value | Name | Meaning |
|---|---|---|
| 0 | STARTING | Face-down card in original position; also foundation cards (set to 0 on foundation entry) |
| 1 | STARTING_FACE_UP | Originally dealt face-down, now revealed, NOT yet moved AND not at pile bottom |
| 2 | ROOT | Card sitting on a non-legal-build parent (parent_table fallback) |
| 3 | IN_CELL | Card in a free cell |
| 4 | PARENT_0 | Built on first legal parent (by fixed suit ordering) |
| 5 | PARENT_1 | Built on second legal parent |
| 6 | PARENT_2 | Built on third legal parent |
| 7 | PARENT_3 | Built on fourth legal parent |
| 8 | IN_HOLE | Card played to hole (hole games only) |
| 9 | IN_SPACE | Card at the bottom of a tableau pile (nothing below, or only face-down) |
| 10–15 | RESERVED | Unused |

See `archive/implementation_plan_v3.md` for the transition table and IN_SPACE/ROOT semantics history.

---

## Milestone 5: Verification and Hardening ✓ COMPLETE

**Status:** Complete as of 2026-03-28. All open issues from v3 resolved.

### Additional work completed in v4 period (2026-03-28)

**Bug B — canfield-strict false positives (wrapping builds):**
`parent_table::get_parents()` used a hard-coded `rank+1` formula and returned no parents
for Kings. For games with a non-Ace foundation base (canfield, base=King), the wrapping
build sequence (King→Ace→2→…→Queen) was not recognised. Fix: `get_parents()` now accepts
`foundations_base` and `max_rank` parameters and applies `foundation_base_convert` logic
to compute the correct parent rank. Four call sites in `game_state.cpp` updated; five unit
tests added to `zobrist_test.cpp`. Committed `52b8b63`.

**Bug A — fortunes-favor waste pointer stale on regular moves:**
`make_regular_move()` never called `update_waste_ptr_in_hash()` when the source pile was
`waste`. This caused 18,908 LRU-only hits (false negatives) at seed 31646033. Fix: save
old waste pointer before the move, call `update_waste_ptr_in_hash(effective_waste_ptr())`
after, restore in `undo_regular_move()`. Committed `52b8b63`.

**Dual-cache test infrastructure (`force_lru=true`):**
The dual-cache metamorphic tests constructed `game_state` without `force_lru=true`, so
`skip_pile_ordering=true` for flat-cache games (M6 behaviour). Swapped single-card empty
tableau piles produced different LRU hashes but identical flat-cache payloads, causing
spurious flat-only hits. Fix: all three `dual_cache_test.cpp` helpers and both
`mismatch_diagnostic.cpp` helpers now pass `force_lru=true`. Flat-only hits → 0.
Committed `52b8b63`.

**Sanitizer runs:**
- ASan: clean — no memory errors. (`BlackHoleUsesNewCache` timeout is a test performance
  artefact of 10k-slot cache under debug builds, not a real error.)
- UBSan: one real bug found — `sol_rules::sol_rules()` was missing `foundations_removable`,
  `stock_redeal`, and `sequence_fixed_suit` from its initializer list. Fixed with `false`
  defaults. RapidJSON `stack.h` null+offset pattern suppressed in `ubsan_runtime_suppressions.txt`.
  Committed `cb9d26d`.

**`recompute_payload_from_scratch()` + `assert_payload_consistent()`:**
Four bugs fixed in the existing debug-only recompute function:
1. Waste pointer used raw `piles[waste].size()` instead of `effective_waste_ptr()`.
2. Bottom-of-pile face-up card was assigned ROOT instead of IN_SPACE.
3. Cell detection used a fragile pile-ref range check against `original_cells` only;
   cards moved into non-pre-filled cells got STARTING instead of IN_CELL.
4. Tableau detection used the same fragile range check; fixed by iterating
   `original_tableau_piles` explicitly.

New `assert_payload_consistent()` wraps the recompute with an assertion that the
scratch-built payload matches the incrementally-maintained one. Skips for
`face_up_policy::TOP_CARDS` games (STARTING_FACE_UP is ambiguous from board inspection
alone). Called from `solver.cpp` on every flat_cache insert in debug builds.
Committed `3d5f66d`.

### Parked items (deliberate)

- `recompute_payload_from_scratch()` for face-down games — `STARTING_FACE_UP` cannot be
  distinguished from ROOT/PARENT_i without move history. Assertion skipped for these games.
- Stale design docs (`implementation_plan_v1.md`, `implementation_plan_v2.md`) — not updated.
- Regression oracle regeneration — oracles are stale because M6 changed DFS traversal order
  (see below).

---

## Milestone 6: Remove Pile Ordering for Flat-Cache Games ✓ COMPLETE

**Status:** Complete. Committed in `f1112f2` (Phase 1) and `a7f3744` (Phase 2).

### What was done

**Phase 1 (`f1112f2`):**
- `skip_pile_ordering` flag added to `game_state`, set to `use_new_cache(rules) && !force_lru`.
  `eval_pile_order()` calls in `place_card`/`take_card` are guarded by this flag.
  Flat-cache games no longer sort tableau piles; LRU games are unaffected.
- `--force-lru` CLI flag added. Forces LRU cache even for flat-cache games, with pile
  ordering restored (`force_lru=true` sets `skip_pile_ordering=false`).
- CMakeLists.txt: added `unit_tests_full` CTest target including DualCacheTest and
  MismatchDiagnostic for end-of-milestone verification.

**Phase 2 (`a7f3744`):**
- Fixed `--force-lru` pile ordering: the Phase 1 implementation set `skip_pile_ordering`
  based only on `use_new_cache(rules)`, causing `--force-lru` runs to also skip pile
  ordering. Corrected to `use_new_cache(rules) && !force_lru`.
- `force_lru` threaded through all `game_state` constructors (public and private) and
  `benchmark::run`.
- Oracle sanity check: exact node-count agreement confirmed on 4 seeds.

### Regression impact

M6 changes `skip_pile_ordering=true` for flat-cache games, altering the DFS traversal
order and therefore `states_searched` counts. **All level 1–3 regression oracles are
stale** — outcomes are still correct but node counts differ. Oracles must be regenerated
before merge. This is expected and accepted.

---

## Milestone 7: Performance Benchmarking and Tuning

**Status:** Benchmarking infrastructure and first results complete (`a7f3744`). Performance
tuning deferred post-merge. Regression oracle regeneration is the sole blocking task.

### What has been done

**Benchmarking scripts** (`scripts/`):
- `metamorphic_test.sh` — outcome-only comparison of flat vs `--force-lru` across
  11 game types × 5 seeds. Verifies correctness without oracle files.
- `benchmark_speedup.sh` — wall-time and nodes/sec comparison of flat vs `--force-lru`
  on same instances.
- `benchmark_baseline.sh` — flat-cache throughput baseline; saves timestamped JSON to
  `docs/cache-redesign/benchmarks/`.

**First benchmark results** (`docs/cache-redesign/benchmarks/`, 2026-03-28):
- **2–3× nodes/sec improvement** across all flat-cache games.
- **Somerset: 83× wall-time speedup** (10-pile game where pile-ordering overhead is
  highest).
- Results saved in `speedup_comparison_20260328.json` and `throughput_baseline_20260328.json`.

### Remaining task (blocking)

**Regenerate regression oracles** (Levels 1–5) to establish post-M6 baselines and update
the regression comparison logic. Current oracles are stale due to M6's `skip_pile_ordering`
changing DFS traversal order. No outcome errors are expected — only `states_searched` counts
differ.

#### Regression comparison policy (post-M6)

M6 removes pile ordering for flat-cache games, making `states_searched` sensitive to
traversal order but not affecting correctness. The oracle comparison policy is therefore:

| Oracle outcome | New run outcome | Verdict |
|---|---|---|
| SOLVED | SOLVED | **PASS** |
| UNSOLVABLE | UNSOLVABLE | **PASS** |
| SOLVED | UNSOLVABLE | **HARD FAIL** — correctness bug |
| UNSOLVABLE | SOLVED | **HARD FAIL** — correctness bug |
| Any | TIMEOUT | **SOFT PASS** — timing/traversal regression, acceptable |
| TIMEOUT | SOLVED/UNSOLVABLE | **PASS** — improvement |
| TIMEOUT | TIMEOUT | **PASS** |

**`states_searched` is recorded in oracles for reference** but is not enforced during
comparison. Traversal order is sensitive to cache implementation changes (pile ordering
removal in M6, future refactors), making node counts non-reproducible across refactors.
Counts serve as a baseline for future profiling and tuning work.

Note: there are no TIMEOUT entries in the current oracle set at Levels 1–3.

### Deferred (post-merge)

- **Performance tuning:** replacement policy (TwoBig1 vs always-replace vs depth-only),
  nibble accessor overhead, `memcmp` cost, 64-bit hash reduction. These are minor
  optimisations not required for correctness or delivery.
- **Flat cache extension:** extending `use_new_cache()` to currently excluded game types
  (spider-type stock dealing, two-deck, accordion, sequences). Planned for a subsequent
  phase after merge into `mac-dev`.

---

## Milestone 8: Documentation and Merge Preparation

**Status:** Not started. Prerequisite: M7 regression oracles regenerated and suite passing.

### Tasks

1. Confirm all levels pass under the new outcome-based comparison policy.
2. Remove dead code; clean up TODOs and debug prints.
3. Update CLAUDE.md and any stale design documents.
4. Organise commits for clean merge into `mac-dev`.
5. Create PR with summary of design, correctness verification, and benchmark results.

---

## Appendix A: Files Created or Modified

### New files (M2–M6)

| File | Milestone | Purpose |
|---|---|---|
| `src/main/game/cache_interface.h` | M1 | Abstract cache interface + `use_new_cache()` |
| `src/main/game/zobrist.h/cpp` | M2 | Descriptor-aligned Zobrist key tables |
| `src/main/game/compact_state.h/cpp` | M2 | 32-byte payload struct |
| `src/main/game/parent_table.h/cpp` | M2 | Parent card lookup (PARENT_0–3 descriptors) |
| `src/main/game/flat_cache.h/cpp` | M3 | Flat open-addressed cache |
| `src/main/game/dual_cache.h/cpp` | M5 | Dual-cache for metamorphic testing |
| `src/test/unit_tests/dual_cache_test.cpp` | M5 | Agreement tests |
| `src/test/unit_tests/mismatch_diagnostic.cpp` | M5 | Detailed mismatch diagnosis tool |
| `ubsan_ignorelist.txt` | M5 | UBSan compile-time ignorelist (RapidJSON) |
| `ubsan_runtime_suppressions.txt` | M5 | UBSan runtime suppressions (RapidJSON) |
| `scripts/metamorphic_test.sh` | M6/M7 | Outcome comparison: flat vs --force-lru |
| `scripts/benchmark_speedup.sh` | M7 | Speedup measurement: flat vs --force-lru |
| `scripts/benchmark_baseline.sh` | M7 | Flat cache throughput baseline |
| `docs/cache-redesign/benchmarks/` | M7 | Benchmark result archives |

### Modified files (M2–M6)

| File | Milestones | Change |
|---|---|---|
| `src/main/game/search-state/game_state.h` | M2, M5, M6 | Zobrist/payload; `assert_payload_consistent()`; `skip_pile_ordering`; `force_lru` constructors |
| `src/main/game/search-state/game_state.cpp` | M2, M5, M6 | Hash+payload updates; all bug fixes; `recompute_payload_from_scratch()` rewrite |
| `src/main/game/sol_rules.cpp` | M5 | Default-initialise `foundations_removable`, `stock_redeal`, `sequence_fixed_suit` |
| `src/main/game/compact_state.h` | M5 | Added IN_SPACE(9) to descriptor enum |
| `src/main/game/parent_table.h/cpp` | M5 | Wrapping builds via `foundations_base`/`max_rank` parameters |
| `src/main/solver/solver.h/cpp` | M1, M4, M5, M6 | Cache interface; flat/LRU factory; `assert_payload_consistent()` call |
| `src/main/game/global_cache.h/cpp` | M1 | `lru_cache` implements `cache_interface` |
| `src/main/main.cpp` | M2, M4, M6 | Zobrist init; cache factory; `--force-lru` wiring |
| `src/main/evaluation/solvability_calc.cpp` | M4, M6 | Cache factory; `--force-lru` |
| `src/main/evaluation/benchmark.cpp/h` | M6 | `force_lru` threading |
| `src/main/input-output/input/command_line_helper.h/cpp` | M6 | `--force-lru` flag |
| `src/test/unit_tests/zobrist_test.cpp` | M5 | Wrapping parent_table tests (5 new) |
| `src/test/unit_tests/dual_cache_test.cpp` | M5, M6 | `force_lru=true` in all helpers |
| `CMakeLists.txt` | M5, M6 | `unit_tests_full` target; stale file cleanup |

---

## Appendix B: Complete Descriptor Reference

| Value | Name | Transitions from | Transitions to |
|---|---|---|---|
| 0 | STARTING | (initial, face-down or stock/waste/reserve) | STARTING_FACE_UP or IN_SPACE (on reveal); ROOT, PARENT_i, IN_CELL, IN_HOLE, IN_SPACE (on move); 0 on foundation entry |
| 1 | STARTING_FACE_UP | STARTING (on reveal, not pile bottom) | ROOT, PARENT_i, IN_CELL, IN_HOLE, IN_SPACE (on move); STARTING (via undo reveal) |
| 2 | ROOT | STARTING, STARTING_FACE_UP, PARENT_i, IN_CELL, IN_SPACE | PARENT_i, IN_CELL, IN_HOLE, IN_SPACE; prior descriptor (via undo) |
| 3 | IN_CELL | STARTING, STARTING_FACE_UP, ROOT, PARENT_i, IN_SPACE | ROOT, PARENT_i, IN_HOLE, IN_SPACE; prior descriptor (via undo) |
| 4–7 | PARENT_0–3 | STARTING, STARTING_FACE_UP, ROOT, IN_CELL, IN_SPACE | ROOT, IN_CELL, IN_HOLE, IN_SPACE; prior descriptor (via undo) |
| 8 | IN_HOLE | Any | (undo only) |
| 9 | IN_SPACE | STARTING (at init for pile-bottom cards); any (on move to empty pile or on reveal at pile bottom) | ROOT, PARENT_i, IN_CELL, IN_HOLE; prior descriptor (via undo) |
| 10–15 | RESERVED | — | — |

---

## Appendix C: Payload Byte Layout

```
Byte 0:     Occupied flag (0 = empty slot, nonzero = occupied)
Bytes 1-2:  Depth (16-bit unsigned, excluded from comparison)
Byte 3:     Foundation Clubs (low nibble) | Foundation Hearts (high nibble)
            OR: Hole-top card ID (low 6 bits) for hole games
Byte 4:     Foundation Spades (low nibble) | Foundation Diamonds (high nibble)
            OR: zero for hole games
Byte 5:     Waste pointer (0–63; 0 if no stock/waste; 0 when waste_deal_symmetry holds)
Bytes 6–31: Card descriptors (52 × 4-bit nibbles)
            Byte 6:  card 0 (low nibble) | card 1 (high nibble)
            ...
            Byte 31: card 50 (low nibble) | card 51 (high nibble)
```

**Comparison:** `memcmp(payload.data + 3, other.data + 3, 29)` — bytes 0–2 excluded.

**waste_deal_symmetry:** Waste pointer is stored as 0 when
`rules.stock_redeal && piles[waste].size() % rules.stock_deal_count == 0`.
This matches the LRU cache's `waste_deal_symmetry` condition.
