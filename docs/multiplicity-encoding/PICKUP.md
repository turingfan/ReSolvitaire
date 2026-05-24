# PICKUP — multiplicity-encoding branch

**Last updated:** 2026-05-24

## What This Branch Is

Implementing the multiplicity encoding cache system, a new cache policy that enables
suit-symmetry canonicalisation in the flat cache (currently only possible via the slower
LRU cache). The work follows a staged plan with architecture prep first, then from-scratch
implementation, then incremental optimisation.

## Master Plan

`docs/multiplicity-encoding/implementation-plan.md` (this directory)

Stages 0-6, from architecture prep through benchmarking. Stages 0-2 complete.
Stage 3 (incremental computation spec) complete. Stage 4-5 (incremental implementation)
complete. Ready for Stage 5.3 solvability cross-check.

## What's Done

**Stage 4 — Incremental Updates, NONE Mode** (committed: this session)
- Auxiliary data structures: `children[104]`, `class_sum[52]`, `old_slot_save[104]`,
  `changed_mask_lo/hi` added to `multiplicity_descriptor_engine`
- `mult_desc_at()` helper on `game_state_impl` — computes multiplicity descriptor
  for a card at a specific pile position (used by incremental move handlers)
- `incremental_update_none()` — O(k) fast path for NONE mode (no cascade):
  updates children[], descriptors[], slot bytes, class_sum[], hash, and payload
- `rebuild_auxiliary()` — rebuilds children[] and class_sum[] from current state;
  called at end of `recompute_from_descriptors()`
- Wired into `make_move`/`undo_move` with per-move-type change identification:
  - regular: moved card + optional hole top + optional reveal
  - built_group: bottom card of group + optional reveal
  - stock_k_plus / stock_to_all_tableau: fallback to `recompute_all()`
- `verify_against_scratch()` — debug-mode oracle that saves all engine state,
  runs from-scratch recompute, asserts hash+payload match, restores state.
  Fires on every make_move/undo_move in debug builds.
- Bug fix: face-down init timing in seed constructor — `recompute_all()` now runs
  after face-up turning for `computes_multiplicity_descriptor` policies
- 10 unit tests in `multiplicity_incremental_test.cpp`

**Stage 5 — Incremental Updates, With Cascade** (committed: this session)
- `incremental_update()` — full BFS cascade for COLOUR/SUIT_IRRELEVANT modes:
  Step 0 (descriptor update + children[]) → Step 1 (BFS cascade with dirty_classes
  bitmask, class re-sort, canonical_pos reassignment, child propagation) →
  Step 2 (post-cascade payload + Zobrist hash rebuild for affected classes)
- Uses `__builtin_ctzll` for efficient bitmask iteration
- Wired into make_move/undo_move: `incremental_update_none()` for NONE mode,
  `incremental_update()` for symmetry modes
- 8 cascade unit tests: sort reorder, dynamic class merge/split, multi-level cascade,
  face-down reveal, 4-way SUIT_IRRELEVANT, sequence of moves, symmetric permutation
- All 18 incremental tests pass; all 3 test gates pass

**Stage 2C — Testing** (committed: `efdeb42`)
- Level 1: 13 metamorphic + structural unit tests in `multiplicity_canonicalisation_test.cpp`
- Level 2: Solvability cross-check — zero correctness mismatches across all game types
- Level 3: Trace spot-check — suit-symmetry reduces unique states by 57-93%

**Stage 2A — Suit-Symmetry Canonicalisation** (committed: `3e5554e`)
- Fixpoint canonicalisation with integrated Scheme A collapsing

**Stage 2B — `in_space(k)` Pile-Indexed Locatives** (committed: `3e5554e`)

**Stage 1 — From-scratch multiplicity hash, no symmetry** (committed)

**Stage 0 — Architecture Prep** (committed)

**Bug fixes:** Face-down locatives (v5.1), Scheme A hash collapsing, hash-guard optimisation

## What's Next

**Stage 5.3** — Solvability cross-check for incremental updates (extremely important).
Verify that incremental multiplicity cache produces same verdicts as from-scratch and LRU:
- klondike (COLOUR) seeds 1-20 with suit-symmetry
- free-cell (SI) seeds 1-20 with suit-symmetry
- black-hole (SI) seeds 1-20
- east-haven, spiderette, will-o-the-wisp (TABLEAU_PILES) seeds 1-10
- No-symmetry regression: klondike seeds 1-20 multiplicity vs auto

**Stage 5.4** — Performance benchmark (incremental vs from-scratch vs LRU)

**Stage 6** — Auto-dispatch (multiplicity cache becomes default for eligible games)

## Key Files

| File | Purpose |
|---|---|
| `src/main/game/multiplicity_descriptor.h` | Per-card descriptor: locative(kind, fd) or predecessor(q, fd) |
| `src/main/game/multiplicity_descriptor_store.h` | 64-byte payload store |
| `src/main/game/multiplicity_descriptor_engine.h` | Core: recompute_all(), fixpoint, incremental_update(), cascade |
| `src/main/game/multiplicity_zobrist.h/cpp` | Zobrist table Z[class][column] |
| `src/main/game/multiplicity_static_class.h` | Static class structure for symmetry modes |
| `src/main/game/flat_descriptor_engine.h` | Flat descriptor engine + descriptor_context |
| `src/main/game/generic_flat_cache_policies.h` | Cluster policies |
| `src/main/game/cache_policy.h` | Policy structs with engine typedefs |
| `src/main/game/cache_interface.h` | use_multiplicity_cache() eligibility |
| `src/main/game/search-state/game_state.h/cpp` | State class, move logic, mult_desc_at() |
| `src/test/unit_tests/multiplicity_canonicalisation_test.cpp` | Stage 2C tests (13) |
| `src/test/unit_tests/multiplicity_incremental_test.cpp` | Stage 4+5 tests (18) |

## Design References

| Document | Location |
|---|---|
| v5.1 specification | `01-Knowledge-Base/Design-Documents/multiplicity_encoding_v5.tex` |
| Detailed plan | `docs/multiplicity-encoding/implementation-plan.md` |
| Stage 3 incremental spec | `docs/multiplicity-encoding/stage3-incremental-update-proposal.md` |
| Stage 4-5 implementation plan | `docs/multiplicity-encoding/stage4-5-implementation-plan.md` |
| Stage 2C testing plan | `docs/multiplicity-encoding/stage2c-testing-plan.md` |
| Scheme A bug report | `docs/multiplicity-encoding/bug-scheme-a-hash-collapsing.md` |
