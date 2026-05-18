# Stage 2 Status Report

**Last updated:** 2026-05-18
**Branch:** multiplicity-encoding

---

## Summary

Stage 2 is **partially complete**. The suit-symmetry canonicalisation (Stage 2A) and
pile-indexed locatives for TABLEAU_PILES games (Stage 2B) are both implemented and
all test gates pass. The outstanding work is Stage 2C: unit tests directly validating
the suit-symmetry canonicalisation algorithm.

---

## Stage 2A — Suit-Symmetry Canonicalisation (COMPLETE)

Implemented in a prior session. See `multiplicity_descriptor_engine.h` and
`multiplicity_static_class.h`.

**What is implemented:**
- `symmetry_mode` enum: `NONE` (52 classes × 1), `COLOUR` (26 × 2), `SUIT_IRRELEVANT` (13 × 4)
- `determine_symmetry_mode(const sol_rules&, bool suit_sym) → symmetry_mode`
- `static_class_structure` with `init(mode)` — populates `class_of[52]`, `class_start[52]`,
  `class_members[52]`, `class_size`, `n_classes`
- 5-phase fixpoint canonicalisation in `recompute_from_descriptors()`:
  - Fast path: when `classes.n_classes == 52` (NONE mode), skip fixpoint entirely;
    preserves Stage 1 behaviour exactly
  - Phase 0: initialise canonical positions from class structure
  - Phase 1: compute initial slot bytes
  - Phase 2: fixpoint — sort each class by slot byte, reassign canonical positions,
    recompute predecessor slots; repeat until stable (max 12 iterations)
  - Phase 3: Scheme A predecessor collapsing
  - Phase 4: write payload to `multiplicity_descriptor_store`
  - Phase 5: additive Zobrist hash (addition within each static class, XOR across classes)
- `descriptor_context` carries `suit_sym` flag from `game_state.cpp`

---

## Stage 2B — `in_space(k)` Pile-Indexed Locatives (COMPLETE)

**Implemented 2026-05-18.** See `stage2b-in-space-k-evaluation.md` for full checklist
and test results.

### What was done

**`multiplicity_descriptor_engine.h` — `recompute_all()`:**

The tableau loop now determines pile symmetry once and tracks a `pile_idx` counter.
For TABLEAU_PILES games (`stock_deal_t == TABLEAU_PILES`), pile bottoms receive
`MLD_IN_SPACE + pile_idx` (kinds 6..12 for a 7-pile game). For all other games,
bare `MLD_IN_SPACE` (kind 6) is used as before. The `pile_idx` counter is incremented
alongside the loop, not derived from `tab_ref` (which is a piles-array index, not a
positional index).

**`multiplicity_descriptor.h`:**

Updated comment on `MLD_IN_SPACE` to document the pile-indexed variant. Updated
`MLD_COUNT` comment to clarify it counts base kinds only (not pile-indexed variants).

**`cache_interface.h` — `use_multiplicity_cache()`:**

Removed the `TABLEAU_PILES` exclusion. The multiplicity cache now accepts Spider-type
games (east-haven, spiderette, will-o-the-wisp). `use_new_cache()` retains its
TABLEAU_PILES exclusion — FlatPolicy still cannot handle these games.

**`game_state.cpp`:**

Updated comments on the two asserts in `make_stock_to_all_tableau_move` and
`undo_stock_to_all_tableau_move` (lines ~895 and ~915). Comment changed from
"TABLEAU_PILES games always use LRU cache" to "TABLEAU_PILES: not eligible for flat
cache" — more accurate now that multiplicity also handles these games.

**`main.cpp`, `benchmark.cpp`, `solvability_calc.cpp`:**

Reverted the premature auto-dispatch additions (`suit_sym && use_multiplicity_cache(rules)`
→ MultiplicityPolicy). These were causing 46 level-1 regression failures (node-count
mismatches on `streamliner: both` instances). Multiplicity remains opt-in via
`--cache-type multiplicity` only; auto-dispatch is a separate decision.

### Test results

All 3 test gates pass. TABLEAU_PILES solvability matches LRU (seed 1 for all three
games). Klondike `states_searched` is identical between multiplicity and auto for
seeds 1-3, confirming pile-symmetric games are unaffected.

Full results in `stage2b-in-space-k-evaluation.md`.

---

## Stage 2C — Unit Tests (TODO)

Direct validation of the suit-symmetry canonicalisation algorithm is not yet
implemented. See `stage2c-testing-plan.md` for the agreed test design.

**Required tests (summary):**
- Construct a game state and a suit-permuted copy; assert equal `hash_value` and
  equal payload (`memcmp`) under `suit_sym = true`
- Confirm fast path (NONE mode) preserves Stage 1 behaviour
- Confirm non-equivalent states do not collide

---

## Files Changed on This Branch

| File | Status |
|---|---|
| `src/main/game/multiplicity_static_class.h` | New — Stage 2A |
| `src/main/game/multiplicity_descriptor_engine.h` | Modified — Stage 2A + 2B |
| `src/main/game/flat_descriptor_engine.h` | Modified — Stage 2A (suit_sym in ctx) |
| `src/main/game/search-state/game_state.cpp` | Modified — Stage 2A + 2B (comment fix) |
| `src/main/game/cache_interface.h` | Modified — Stage 2A + 2B |
| `src/main/game/multiplicity_descriptor.h` | Modified — Stage 2B (comment update) |
| `src/main/main.cpp` | Dispatch additions reverted (Stage 2B) |
| `src/main/evaluation/solvability_calc.cpp` | Dispatch additions reverted (Stage 2B) |
| `src/main/evaluation/benchmark.cpp` | Dispatch additions reverted (Stage 2B) |
