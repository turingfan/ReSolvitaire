# PICKUP — multiplicity-encoding branch

**Last updated:** 2026-05-15

## What This Branch Is

Implementing the multiplicity encoding v4 cache system, a new cache policy that enables
suit-symmetry canonicalisation in the flat cache (currently only possible via the slower
LRU cache). The work follows a staged plan with architecture prep first, then from-scratch
implementation, then incremental optimisation.

## Master Plan

`docs/multiplicity-encoding/implementation-plan.md` (this directory)

Stages 0-6, from architecture prep through benchmarking. Only Stage 0 is underway.
The canonical copy lives here; the Knowledge Base copy is a snapshot from when it was written.

## What's Done

**Stage 0.1 — Descriptor interface audit** (committed)
- `docs/multiplicity-encoding/stage0-descriptor-audit.md`
- Catalogued all 24 `if constexpr (Policy::computes_hash)` blocks in game_state.cpp
- Identified 9 operation types and recommended extracting into an engine class

**Stage 0.2 — Extract descriptor engine** (code complete, tests pass, not yet committed)
- Created `src/main/game/flat_descriptor_engine.h`:
  - `descriptor_context` struct (lightweight read-only view of game state)
  - `flat_descriptor_engine<DescStore>` template (holds hash, store, face-up table)
  - `null_descriptor_engine` (no-op stub for LRUPolicy)
- Modified `cache_policy.h`: added `descriptor_engine` typedef to all 4 policies
- Modified `game_state.h`: replaced 3 separate members with single `desc_engine`;
  removed 5 method declarations; added `make_desc_ctx()`
- Modified `game_state.cpp`: delegated all descriptor operations to engine;
  removed 5 old method implementations; added proper `if constexpr` guards

All three test gates pass (release + debug + trace unit tests, level 1 regression
across all 4 variant binaries).

## What's Next

1. **Commit Stage 0.2** on this branch
2. **Stage 1**: From-scratch multiplicity hash (no symmetry) — new policy that
   recomputes hash from board state each move, using predecessor-based descriptors
3. **Stage 2**: Add suit-symmetry canonicalisation to Stage 1
4. **Stages 3-6**: Incremental updates, optimisation, benchmarking

## Key Files

| File | Purpose |
|---|---|
| `src/main/game/flat_descriptor_engine.h` | Descriptor engine (new) |
| `src/main/game/cache_policy.h` | Policy structs with engine typedefs |
| `src/main/game/search-state/game_state.h` | State class using `desc_engine` member |
| `src/main/game/search-state/game_state.cpp` | Move logic delegating to engine |
| `docs/multiplicity-encoding/stage0-descriptor-audit.md` | Stage 0.1 audit |

## Design References

| Document | Location |
|---|---|
| v4 specification | `01-Knowledge-Base/Design-Documents/multiplicity_encoding_v4.tex` |
| Detailed plan | `docs/multiplicity-encoding/implementation-plan.md` |
| Symmetry payload plan | `01-Knowledge-Base/Implementation-Plans/SymmetryPayloadPlan 20260512155752.md` |
