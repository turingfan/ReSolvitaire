# PICKUP — multiplicity-encoding branch

**Last updated:** 2026-05-19

## What This Branch Is

Implementing the multiplicity encoding cache system, a new cache policy that enables
suit-symmetry canonicalisation in the flat cache (currently only possible via the slower
LRU cache). The work follows a staged plan with architecture prep first, then from-scratch
implementation, then incremental optimisation.

## Master Plan

`docs/multiplicity-encoding/implementation-plan.md` (this directory)

Stages 0-6, from architecture prep through benchmarking. Stages 0 and 1 are complete.
Stage 2 is partially complete (2A, 2B done; 2C Level 1 unit tests done, Level 2 cross-check outstanding).

## What's Done

**Stage 2A — Suit-Symmetry Canonicalisation** (committed: `3e5554e`)
- `symmetry_mode` enum: `NONE` (52×1), `COLOUR` (26×2), `SUIT_IRRELEVANT` (13×4)
- `determine_symmetry_mode(rules, suit_sym)` in `multiplicity_static_class.h`
- `static_class_structure` with `init(mode)` — class_of[52], class_start, class_members, class_size, n_classes
- Fixpoint canonicalisation in `recompute_from_descriptors()`:
  - Fast path: NONE mode (n_classes==52) skips fixpoint, preserves Stage 1 behaviour
  - Fixpoint loop: sort → assign canonical positions → recompute slots using
    `collapsed_pos()` (Scheme A folded into fixpoint, not a separate pass)
  - Post-fixpoint: write payload, additive Zobrist hash (SUM within class, XOR across classes)
- `descriptor_context` carries `suit_sym` flag

**Stage 2B — `in_space(k)` Pile-Indexed Locatives** (committed: `3e5554e`)
- TABLEAU_PILES games (east-haven, spiderette, will-o-the-wisp) now use
  `MLD_IN_SPACE + pile_idx` for pile bottoms instead of bare `MLD_IN_SPACE`
- `use_multiplicity_cache()` no longer excludes TABLEAU_PILES
- Premature auto-dispatch additions reverted (multiplicity remains opt-in)
- See `stage2b-in-space-k-evaluation.md`

**Bug fix: Face-down locative descriptors** (committed: `b3f4b52`)
- Stage 2B exposed a pre-existing design gap: locative descriptors did not encode
  face-down status. Pile bottoms that are face-down (common in TABLEAU_PILES games
  after stock deals) produced identical payloads/hashes to face-up pile bottoms.
- Diagnosed via trace comparison on spiderette seed 2 (false-positive HIT at op 138).
- Fix: `make_locative(kind, fd)` now carries a face-down flag; `raw_slot()` uses
  reflected encoding `255 - (52 + kind)` for face-down locatives; `zob_for_card()`
  applies NOT trick uniformly to both predecessors and locatives.
- Spec updated to v5.1 (`multiplicity_encoding_v5.tex`).
- All 3 test gates pass; all 3 TABLEAU_PILES games × 3 seeds match LRU exactly.

**Stage 1 — From-scratch multiplicity hash, no symmetry** (committed)
- `MultiplicityPolicy` in `cache_policy.h`; `multiplicity_descriptor.h`,
  `multiplicity_descriptor_store.h`, `multiplicity_descriptor_engine.h`,
  `multiplicity_zobrist.h/cpp` — new files
- `recompute_all()` called after every make_move/undo_move
- CLI opt-in via `--cache-type multiplicity`
- Waste-deal symmetry: stock+waste cards share MLD_IN_STOCK when redeal symmetry holds
- All 3 test gates pass; exact pre-eviction match on states_searched with flat cache

**Stage 0 — Architecture Prep** (committed)
- Stage 0.1: Descriptor interface audit (`stage0-descriptor-audit.md`)
- Stage 0.2: Extracted descriptor logic into `flat_descriptor_engine.h`
- Stage 0.3: Validated PredecessorPolicy compatibility (no changes needed)

**Stage 2C — Unit Tests Level 1** (not yet committed)
- 13 metamorphic + structural tests in `multiplicity_canonicalisation_test.cpp`
- Randomised suit-permutation invariance checks for COLOUR and SUIT_IRRELEVANT modes
  across klondike, free-cell, black-hole, spiderette game profiles
- Structural tests: NONE mode stage 1 preservation, fixpoint convergence, Scheme A
  collapsing, in_space(k) differentiation, bare in_space dedup
- All 13 tests pass

**Bug fixes: Scheme A hash collapsing** (not yet committed, see
`docs/multiplicity-encoding/bug-scheme-a-hash-collapsing.md`)
- Issue 1: `zob_for_card()` used raw `canonical_pos` instead of collapsed slot byte
- Issue 2: Scheme A needed to propagate across classes — fixed by folding Scheme A
  into the fixpoint loop via `collapsed_pos()` helper

## What's Next

**Stage 2C — Level 2 Solvability Cross-Check** (TODO, see `stage2c-testing-plan.md`)
- Solvability cross-check: seeds 1-20 for symmetric games, multiplicity vs LRU
- Asymmetric criterion validation on 0-eviction seeds

**Then Stage 3** — Incremental computation specification document (before implementing
incremental updates). Do not proceed without Ian's approval.

## Key Files

| File | Purpose |
|---|---|
| `src/main/game/multiplicity_descriptor.h` | Per-card descriptor: locative(kind, fd) or predecessor(q, fd) |
| `src/main/game/multiplicity_descriptor_store.h` | 64-byte payload store |
| `src/main/game/multiplicity_descriptor_engine.h` | Core: recompute_all(), 5-phase canonicalisation |
| `src/main/game/multiplicity_zobrist.h/cpp` | Zobrist table Z[class][column] |
| `src/main/game/multiplicity_static_class.h` | Static class structure for symmetry modes |
| `src/main/game/flat_descriptor_engine.h` | Flat descriptor engine + descriptor_context |
| `src/main/game/cache_policy.h` | Policy structs with engine typedefs |
| `src/main/game/cache_interface.h` | use_multiplicity_cache() eligibility |
| `src/main/game/search-state/game_state.h/cpp` | State class, move logic |
| `src/test/unit_tests/multiplicity_canonicalisation_test.cpp` | Stage 2C metamorphic + structural tests |

## Design References

| Document | Location |
|---|---|
| v5.1 specification | `01-Knowledge-Base/Design-Documents/multiplicity_encoding_v5.tex` |
| Detailed plan | `docs/multiplicity-encoding/implementation-plan.md` |
| Stage 2 status | `docs/multiplicity-encoding/stage2-status.md` |
| Stage 2C testing plan | `docs/multiplicity-encoding/stage2c-testing-plan.md` |
| Scheme A bug report | `docs/multiplicity-encoding/bug-scheme-a-hash-collapsing.md` |
