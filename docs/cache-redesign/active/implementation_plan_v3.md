# ReSolvitaire Cache Refactor: Implementation Plan v3

**Version:** 3.0
**Date:** 27 March 2026
**Authors:** Ian Gent & AI Assistant
**Reference Documents:**
- `specs/main.pdf` — Architecture and Payload Specification
- `specs/descriptor_zobrist.pdf` — Descriptor-Aligned Zobrist Hashing
- `archive/implementation_plan_v1.md` — Original plan (Milestones 0–1 completed)
- `archive/implementation_plan_v2.md` — Previous plan (Milestones 2–4 completed as described there)

---

## Status Summary

| Milestone | Title | Status |
|---|---|---|
| 0 | Baseline and test infrastructure | ✓ COMPLETE |
| 1 | Abstract cache interface | ✓ COMPLETE |
| 2 | Descriptor-aligned Zobrist hash and compact payload | ✓ COMPLETE |
| 3 | Flat cache implementation | ✓ COMPLETE |
| 4 | Wire solver to new cache | ✓ COMPLETE |
| 5 | Verification and hardening | Mostly complete — open issues logged |
| 6 | Remove pile ordering for flat-cache games | Ready to implement |
| 7 | Performance benchmarking and tuning | Not started |
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

These are the **current** descriptor semantics as of v3 (updated from v2 following the
M5 bug fixes on 2026-03-26):

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

**Important changes from v2:**
- ROOT(2) **no longer** means "bottom of pile". It now exclusively means "card on a
  non-legal-build parent" (the fallback from `parent_table::get_descriptor_for_parent`).
- IN_SPACE(9) is new. It means "card at the bottom of a tableau pile / empty space below".
  This was previously conflated with ROOT, causing false positives in games where cards
  are dealt in non-legal-build order (SpanishPatience, FlowerGarden, SeahavenTowers).
- This separation follows the same principle as the STARTING fix: init descriptors must
  match move descriptors for the same position.

**STARTING_FACE_UP transitions:**
- On reveal: STARTING → IN_SPACE (if now at pile bottom) or STARTING_FACE_UP (otherwise)
- On subsequent move of revealed card: STARTING_FACE_UP → ROOT / PARENT_i / IN_CELL / IN_SPACE / etc.
- On undo of reveal: IN_SPACE or STARTING_FACE_UP → STARTING

**IN_SPACE transitions:**
- At init: assigned to every face-up card at the bottom of each tableau pile
- On move to empty pile: assigned to moved card
- On move away: the card now exposed at the pile bottom gets IN_SPACE
- On undo: restored from undo stack

**Key principle:** Init descriptors must match what `determine_destination_descriptor()`
would compute for the same position. Bugs 1 and 2 fixed in M5 were both violations of
this principle.

---

## Milestone 2: Descriptor-Aligned Zobrist Hash and Compact Payload ✓ COMPLETE

**Status:** Complete. Implemented in `game_state.cpp`, `compact_state.h/cpp`,
`zobrist.h/cpp`, `parent_table.h/cpp`.

See `archive/implementation_plan_v2.md` §Milestone 2 for full task breakdown.
See `archive/DEVELOPMENT.md` for per-commit implementation log.

---

## Milestone 3: Flat Cache Implementation ✓ COMPLETE

**Status:** Complete. Committed in `19ea515`.

`flat_cache.h/cpp` — open-addressed cache with 64-byte two-slot clusters (TwoBig1
replacement policy: slot 0 depth-preferred, slot 1 always-replace). Cluster index via
multiply-high from 64-bit Zobrist hash.

---

## Milestone 4: Wire the Solver to the New Cache ✓ COMPLETE

**Status:** Complete. Committed in `8caf454`.

`use_new_cache(rules)` in `cache_interface.h` selects flat vs LRU at construction time.
Returns true for: single deck, no sequences, no accordion, no spider-type stock dealing.

---

## Milestone 5: Verification and Hardening

**Status:** Mostly complete. Known open issues logged; accepted as low risk before M6.

### What was done

**5.1 Dual-cache metamorphic testing framework**
`dual_cache.h/cpp` runs LRU and flat caches in parallel on the same DFS. Divergences
(LRU=HIT/flat=MISS and LRU=MISS/flat=HIT) are counted separately. `dual_cache_test.cpp`
provides agreement tests at multiple game types and seeds.

**5.2 Descriptor bug fixes (2026-03-26)**

Two interconnected bugs found and fixed. See `session_notes_2026-03-26.md` and
`bug_report_dual_cache_mismatches.md` / `bug_report_root_descriptor_false_positives.md`
for full details.

*Bug 1 — STARTING not position-canonical (false negatives):*
`init_payload_and_hash()` assigned STARTING(0) to all face-up tableau cards, but
`determine_destination_descriptor()` computed positional descriptors (ROOT, PARENT_x,
IN_SPACE). A card moved away and returned to its original position got a different
descriptor than at init. Fix: init now computes full positional descriptors.
Affected: FreeCell, BakersGame, Somerset.

*Bug 2 — ROOT overloaded (false positives):*
ROOT(2) was used for: (a) bottom of pile at init, (b) card moved to empty pile,
(c) card on non-legal-build parent. Cases (a)/(b) and (c) are fundamentally different:
case (c) has a face-up card below it. Fix: IN_SPACE(9) for cases (a)/(b); ROOT(2)
exclusively for case (c).
Affected: SpanishPatience, FlowerGarden, SeahavenTowers.

*Bug 3 — Waste pointer symmetry (preventive):*
Added `effective_waste_ptr()` matching LRU's `waste_deal_symmetry` condition
(`stock_redeal && waste_size % deal_count == 0`).

**5.3 Revealed card handling**
When a face-down card is revealed: bottom of pile (pile size == 1 after reveal) →
IN_SPACE(9). Not bottom → STARTING_FACE_UP(1).

**5.4 Level 1 regression**
118/150 pass. 32 failures all explained by suit symmetry (streamliner "both" or inherent
hole-game symmetry) — node counts differ but outcomes are correct. Accepted.

**5.5 Level 2 regression**
107/160 pass. 53 failures: 51 explained by suit symmetry. 2 confirmed bugs — see below.

### Open issues (logged, accepted before M6)

See `bug_report_level2_regression_mismatches.md` for diagnostics.

**Issue A — fortunes-favor false negatives (18,908 at seed 31646033):**
`make_regular_move()` does not update the waste pointer when moving FROM waste. The
`auto-waste-then-stock` spaces policy auto-plays waste cards to empty tableau spaces via
regular moves; the waste pile shrinks but payload byte 5 stays stale. Fix direction:
detect `m.from == waste` in `make_regular_move()` and call
`update_waste_ptr_in_hash(effective_waste_ptr())`.

**Issue B — canfield-strict false positives (87 at seed 4000100):**
Different board states produce identical payloads. Board difference: KS in different
piles, with AD/AH swapping between ROOT and IN_SPACE. Root cause not yet fully
identified — needs further investigation.

**Issue C — `recompute_payload_from_scratch()` (parked):**
Cannot distinguish ROOT from IN_SPACE without move history. The debug recomputation
assertion is not wired up. Deferred to a later milestone.

**Issue D — Sanitizer runs (deferred):**
AddressSanitizer and UBSanitizer runs not yet completed.

---

## Milestone 6: Remove Pile Ordering for Flat-Cache Games

**Status:** Ready to implement. See `milestone6_detailed_plan.md` for full detail.

### Goal

Eliminate `eval_pile_order()` sorting in `place_card`/`take_card` for games using the
flat cache. The descriptor-based encoding is inherently pile-order independent, so the
LRU's pile canonicalization is unnecessary overhead.

### Key tasks

**6.1 Add `--force-lru` CLI flag**
Runtime option to force LRU cache even for games that would use the flat cache. Used
for metamorphic testing (compare same instance under both caches). Does NOT affect
pile ordering — that is a game-type property.

In `command_line_helper.h/cpp`: add `bool force_lru_cache = false` and `--force-lru` option.
In `solver.cpp` / `solvability_calc.cpp`: change cache construction to:
```cpp
if (use_new_cache(rules) && !options.force_lru_cache) { /* flat */ } else { /* lru */ }
```

**6.2 Add `skip_pile_ordering` flag to `game_state`**
Set from `use_new_cache(rules)` in constructors. Guard `eval_pile_order` calls in
`place_card`/`take_card`:
```cpp
if (!skip_pile_ordering && (rules.stock_size == 0 || ...)) {
    eval_pile_order(pr, is_place);
}
```

**6.3 Verify `tableau_piles` iteration sites**
With ordering skipped, `tableau_piles` stays in deal order. LRU (unreachable for
flat-cache games normally) loses canonicalization when called with `--force-lru` —
acceptable for testing.

### Testing approach (metamorphic, not oracle-based)

Run same instance twice — once with flat cache, once with `--force-lru` — compare
results. No oracle files are updated.

**Full agreement** (outcome AND node counts must match): free-cell, bakers-game,
somerset, seahaven-towers, spanish-patience, flower-garden.

**Outcome-only agreement** (solvability only): klondike, hole games (inherent suit
symmetry), fortunes-favor, canfield-strict (M5 open issues).

The dual cache infrastructure (`dual_cache`, `mismatch_diagnostic`) is retained as a
debug tool but is not part of M6 validation.

### Files to modify

| File | Change |
|---|---|
| `src/main/input-output/input/command_line_helper.h/cpp` | Add `--force-lru` flag |
| `src/main/solver/solver.cpp` | Check flag in cache construction |
| `src/main/evaluation/solvability_calc.cpp` | Same |
| `src/main/game/search-state/game_state.h` | Add `skip_pile_ordering` member |
| `src/main/game/search-state/game_state.cpp` | Set flag; guard `eval_pile_order` calls |

---

## Milestone 7: Performance Benchmarking and Tuning

**Status:** Not started. Prerequisite: M6 complete.

**Goal:** Measure speedup from M6 and decide whether flat cache extension to more game
types is worthwhile.

If flat cache is measurably faster than LRU, proceed to extending coverage to currently
excluded game types (spider-type dealing, two-deck, accordion, sequences). The `--force-lru`
flag from M6 and the dual cache infrastructure will be the primary tools for that work.

### Tasks

1. Comprehensive benchmarks: wall-clock time, states/second, peak memory, cache hit rate,
   eviction count. Compare flat vs LRU (via `--force-lru`) on same instances.
2. Tune replacement policy: test always-replace vs TwoBig1 vs depth-only.
3. Profile hotspots: nibble access, memcmp, hash computation.
4. Consider large-page allocation for the flat array.

---

## Milestone 8: Documentation and Merge Preparation

**Status:** Not started. Prerequisite: M7 complete.

### Tasks

1. Remove dead code, clean up TODOs.
2. Add comment blocks referencing design documents.
3. Organise commits for clean merge.
4. Create PR with benchmark results.

---

## Appendix A: Files Created or Modified

### New files (M2–M5)
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

### Modified files (M2–M5)
| File | Milestone | Change |
|---|---|---|
| `src/main/game/search-state/game_state.h` | M2, M5 | `zobrist_hash_value`, `payload`, `effective_waste_ptr()` |
| `src/main/game/search-state/game_state.cpp` | M2, M5 | Hash+payload updates; IN_SPACE/ROOT fix; waste symmetry |
| `src/main/game/compact_state.h` | M5 | Added IN_SPACE(9) to descriptor enum |
| `src/main/solver/solver.h/cpp` | M1, M4 | Cache interface; flat/LRU factory |
| `src/main/game/global_cache.h/cpp` | M1 | lru_cache implements cache_interface |
| `src/main/main.cpp` | M2, M4 | Zobrist init; cache factory |
| `src/main/evaluation/solvability_calc.cpp` | M4 | Cache factory |
| `CMakeLists.txt` | M5 | Added mismatch_diagnostic.cpp to test sources |

### Files to modify (M6)
| File | Change |
|---|---|
| `src/main/input-output/input/command_line_helper.h/cpp` | `--force-lru` flag |
| `src/main/solver/solver.cpp` | Respect `force_lru_cache` in cache construction |
| `src/main/evaluation/solvability_calc.cpp` | Same |
| `src/main/game/search-state/game_state.h` | `skip_pile_ordering` member |
| `src/main/game/search-state/game_state.cpp` | Set flag; guard `eval_pile_order` calls |

---

## Appendix B: Complete Descriptor Reference (v3)

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
