# PICKUP — multiplicity-encoding branch

**Last updated:** 2026-05-24

## What This Branch Is

Implementing the multiplicity encoding cache system, a new cache policy that enables
suit-symmetry canonicalisation in the flat cache (currently only possible via the slower
LRU cache). The work follows a staged plan with architecture prep first, then from-scratch
implementation, then incremental optimisation.

## Master Plan

`docs/multiplicity-encoding/implementation-plan.md` (this directory)

Stages 0-5 complete (including solvability cross-check and trace agreement tests).
Stage 5.4 (performance benchmark) and Stage 6 (auto-dispatch) remain.

## What's Done

**Stage 5.3 — Solvability Cross-Check** (this session)
- 110 comparisons, 0 mismatches across all game types and symmetry modes
- Results documented in `docs/multiplicity-encoding/stage5-solvability-results.md`
- Tested: klondike COLOUR (20), free-cell SI (20), black-hole SI (20),
  east-haven/spiderette/will-o-the-wisp TABLEAU_PILES (30), klondike NONE regression (20)

**Multiplicity vs Flat Trace Agreement Tests** (this session)
- 8 CTest targets (`trace_mult_vs_flat_*`) comparing `solvitaire-flat-trace` vs
  `solvitaire-trace --cache-type multiplicity` using `compare_traces.py --until-evict`
- Games: klondike, free-cell, black-hole, canfield, spanish-patience, bakers-game,
  flower-garden, somerset
- All 8 pass

**STRACE_EVICT Bug Fix** (this session)
- `generic_flat_cache.h` was missing `STRACE_EVICT()` in its `do_replacement` overloads
- Evictions were counted but not traced, making `--until-evict` unreliable for flat-cache
  comparisons
- Fixed by adding `STRACE_EVICT()` after every `++eviction_count` (5 paths across 3
  replacement strategies)
- Known issue #22: trace regression reference binaries need rebuild after this fix

**Stage 4 — Incremental Updates, NONE Mode** (committed: `05f45a8`)
- O(k) fast path via `incremental_update_none()`
- Auxiliary data: `children[104]`, `class_sum[52]`, `old_slot_save[104]`, changed_mask
- `verify_against_scratch()` debug oracle, 10 unit tests

**Stage 5 — Incremental Updates, With Cascade** (committed: `05f45a8`)
- `incremental_update()` — full BFS cascade for COLOUR/SUIT_IRRELEVANT modes
- 8 cascade unit tests, all 3 test gates pass

**Stages 0-2C** — Architecture prep, from-scratch hash, suit-symmetry canonicalisation,
testing (all committed on earlier commits)

## What's Next

**Stage 5.4** — Performance benchmark (incremental vs from-scratch vs LRU)

**Stage 6** — Auto-dispatch (multiplicity cache becomes default for eligible games)

**Reference binaries** — Need rebuild after STRACE_EVICT fix is on dev (known issue #22)

## Key Files

| File | Purpose |
|---|---|
| `src/main/game/multiplicity_descriptor.h` | Per-card descriptor: locative(kind, fd) or predecessor(q, fd) |
| `src/main/game/multiplicity_descriptor_store.h` | 64-byte payload store |
| `src/main/game/multiplicity_descriptor_engine.h` | Core: recompute_all(), fixpoint, incremental_update(), cascade |
| `src/main/game/multiplicity_zobrist.h/cpp` | Zobrist table Z[class][column] |
| `src/main/game/multiplicity_static_class.h` | Static class structure for symmetry modes |
| `src/main/game/flat_descriptor_engine.h` | Flat descriptor engine + descriptor_context |
| `src/main/game/generic_flat_cache.h` | Generic flat cache (includes STRACE_EVICT fix) |
| `src/main/game/generic_flat_cache_policies.h` | Cluster policies |
| `src/main/game/cache_policy.h` | Policy structs with engine typedefs |
| `src/main/game/cache_interface.h` | use_multiplicity_cache() eligibility |
| `src/main/game/search-state/game_state.h/cpp` | State class, move logic, mult_desc_at() |
| `src/test/unit_tests/multiplicity_canonicalisation_test.cpp` | Stage 2C tests (13) |
| `src/test/unit_tests/multiplicity_incremental_test.cpp` | Stage 4+5 tests (18) |
| `docs/multiplicity-encoding/stage5-solvability-results.md` | Solvability cross-check results |

## Design References

| Document | Location |
|---|---|
| v5.1 specification | `01-Knowledge-Base/Design-Documents/multiplicity_encoding_v5.tex` |
| Detailed plan | `docs/multiplicity-encoding/implementation-plan.md` |
| Stage 3 incremental spec | `docs/multiplicity-encoding/stage3-incremental-update-proposal.md` |
| Stage 4-5 implementation plan | `docs/multiplicity-encoding/stage4-5-implementation-plan.md` |
| Stage 2C testing plan | `docs/multiplicity-encoding/stage2c-testing-plan.md` |
| Scheme A bug report | `docs/multiplicity-encoding/bug-scheme-a-hash-collapsing.md` |
