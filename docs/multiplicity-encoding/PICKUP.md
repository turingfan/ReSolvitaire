# PICKUP — multiplicity-encoding branch

**Last updated:** 2026-05-17

## What This Branch Is

Implementing the multiplicity encoding v4 cache system, a new cache policy that enables
suit-symmetry canonicalisation in the flat cache (currently only possible via the slower
LRU cache). The work follows a staged plan with architecture prep first, then from-scratch
implementation, then incremental optimisation.

## Master Plan

`docs/multiplicity-encoding/implementation-plan.md` (this directory)

Stages 0-6, from architecture prep through benchmarking. Stage 0 is complete.
The canonical copy lives here; the Knowledge Base copy is a snapshot from when it was written.

## What's Done

**Stage 0.1 — Descriptor interface audit** (committed)
- `docs/multiplicity-encoding/stage0-descriptor-audit.md`
- Catalogued all 24 `if constexpr (Policy::computes_hash)` blocks in game_state.cpp
- Identified 9 operation types and recommended extracting into an engine class

**Stage 0.2 — Extract descriptor engine** (committed: `00a076a`)
- Created `src/main/game/flat_descriptor_engine.h`:
  - `descriptor_context` struct (lightweight read-only view of game state)
  - `flat_descriptor_engine<DescStore>` template (holds hash, store, face-up table)
  - `null_descriptor_engine` (no-op stub for LRUPolicy)
- Modified `cache_policy.h`: added `descriptor_engine` typedef to all 4 policies
- Modified `game_state.h`: replaced 3 separate members with single `desc_engine`;
  removed 5 method declarations; added `make_desc_ctx()`
- Modified `game_state.cpp`: delegated all descriptor operations to engine;
  removed 5 old method implementations; added proper `if constexpr` guards
- All three test gates pass (release + debug + trace unit tests, level 1 regression
  across all 4 variant binaries)

**Stage 0.3 — Validate PredecessorPolicy compatibility** (complete, no code changes)
- PredecessorPolicy has two parallel systems after Stage 0.2:
  - **System 1 (flat desc_engine):** runs because `computes_hash = true`, but
    `PredecessorClusterPolicy::hash_of()` / `payload_of()` return the
    predecessor-specific hash/payload, so System 1's output is never used for cache ops.
  - **System 2 (predecessor-specific):** `predecessor_array`, `predecessor_zobrist_hash`,
    `pred_payload` — maintained in `make_accordion_move()` / `undo_accordion_move()`.
    This is what the cache actually reads.
- Decision: no `PredecessorDescriptorEngine` needed. PredecessorPolicy is slated for
  replacement by MultiplicityPolicy once it can encode accordion-style predecessor
  relationships. Extracting a new engine for it would be work that gets discarded.
  The dual-system redundancy is an accepted temporary state.
- All existing tests continue to pass.

## What's Next

**Stage 1** — From-scratch multiplicity hash, no symmetry.

Key tasks (see `implementation-plan.md` Stage 1 for detail):
1. `multiplicity_descriptor.h` — new descriptor types (MLD_*, MPD)
2. `multiplicity_descriptor_store.h` — 52-entry store + 64-byte payload buffer
3. `multiplicity_zobrist.h` — Z[52][80] table, seed `0xDEADBEEF12345678`
4. `multiplicity_descriptor_engine.h` — engine with `init()`, `on_card_moved()` etc.,
   dirty-flag lazy recompute strategy
5. `MultiplicityPolicy` in `cache_policy.h` and `MultiplicityClusterPolicy` in
   `generic_flat_cache_policies.h`
6. Dispatch wiring: `--cache-type multiplicity` CLI opt-in
7. Validation: solvability + states-searched must match FlatPolicy exactly
   (pre-eviction; post-eviction divergence acceptable)

After Stage 1, Stage 2 adds suit-symmetry. Do not proceed to Stage 2 without Ian's approval.

## Key Files

| File | Purpose |
|---|---|
| `src/main/game/flat_descriptor_engine.h` | Flat descriptor engine (FlatPolicy, HashOnlyPolicy, PredecessorPolicy) |
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
