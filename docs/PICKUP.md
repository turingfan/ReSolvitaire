# Pickup: feature/templated-dispatch

**Last updated:** 2026-04-28
**Branch:** `feature/templated-dispatch` (from `dev` at `1dd0882`)

## What This Branch Does

Replaces the dual mechanism (runtime booleans + preprocessor `#ifdef` guards) for cache policy selection in `game_state` with a single C++ template approach: `game_state_impl<Policy>`. Also templates the solver and cache layer to eliminate all virtual dispatch from the DFS hot path (Option B2).

Design document: `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`
Full plan: `docs/templated-dispatch/commit3-plan.md` (includes 2026-04-28 amendment)

## Commits Done

### Commit 0: Test infrastructure (01fae5b + 1000bd0)
- Generated LRU node-count oracles for Level 1-3 (`tests/oracles/level{1,2,3}_lru.json`)
- Added `--enforce-node-counts` flag to `scripts/regression_runner.py`
- Updated all Level 1-3 CTest targets to enforce node counts
- Regenerated stale Level 2-3 default and hash-only oracles with current binary
- All Level 1-3 regression tests pass: 12/12 (default, flat, hash-only, LRU)

### Commit 1: Add `cache_policy.h` dispatch traits (8737539)
- Created `src/main/game/cache_policy.h` with four dispatch policy tag structs: `FlatPolicy`, `HashOnlyPolicy`, `PredecessorPolicy`, `LRUPolicy`
- Each provides `static constexpr bool computes_hash / computes_payload` and `descriptor_store_type` typedef
- Renamed cluster storage policies in `generic_flat_cache_policies.h` to avoid name collision: `HashOnlyPolicy` → `HashOnlyClusterPolicy`, `PredecessorPolicy` → `PredecessorClusterPolicy`; updated all 4 callsite files
- Build clean (all variants); unit tests 2/2; Level 1 regression 4/4

### Commit 2: API audit — no code changes needed (a6c719d)
- Audited `compact_state` vs `hash_descriptor_store` interfaces
- All five descriptor-store method groups already have identical signatures
- The single asymmetry (`set_depth`) is correctly gated by `computes_payload`
- Branch proceeds directly to Commit 3a
- Audit doc: `docs/templated-dispatch/commit2-api-audit.md`

## Next Code Commits

### Commit 3a: Template `game_state` (the core transformation)
Convert `game_state` to `game_state_impl<Policy>` template. Apply three
transformation patterns to game_state.cpp: (A) `#if SOLVITAIRE_COMPUTES_FLAT_HASH`
→ `if constexpr (Policy::computes_hash)`, (B) `#ifdef SOLVITAIRE_HASH_ONLY`
member-name selector → single `desc_store`, (C) runtime flag removal. Add
`skip_pile_ordering` trait to cache_policy.h. Add `game_state` typedef for
backward compatibility. No solver or cache changes.

**Full plan:** `docs/templated-dispatch/commit3-plan.md` §1–§4 and §Commit 3a.

### Commit 3b: Solver + cache layer — zero-overhead dispatch
Template solver on Policy, holding `Policy::cache_type` directly (no virtual
`cache_interface`). Add dispatch switch in main.cpp. Template cache method
signatures. Update solvability_calc and benchmark with dispatch switches.

**Full plan:** `docs/templated-dispatch/commit3-plan.md` §Amendment: Commit 3b.

## Key Decisions

- C++ standard: can use C++17 (`if constexpr`)
- Conditional members: save ~90 bytes for LRU (use EBO) — deferred
- Solver: template on Policy (Option B2 — zero virtual dispatch)
- Cache layer: `Policy::cache_type` = `generic_flat_cache<*>` for flat variants, `lru_cache` for LRU
- Old concrete caches (`flat_cache`, `hash_only_cache`, `predecessor_flat_cache`) kept for dual_cache parity tests only
- `cache_interface` retained for test infrastructure, not used in solver
- `solve_game()` return type: Option C (reporting inside dispatch switch)
- `skip_pile_ordering`: Flat=true, HashOnly=true, Predecessor=false (conservative), LRU=false
- All `#ifdef` guards eliminated by end of phase
- Testing: node-count oracles enforce identical traversal for Level 1-3
- Workflow: Opus plans each commit, writes prompts for lower model, verifies

## Testing Gate

Each commit must pass:
```bash
./build.sh --release --unit-tests --variants
cd cmake-build-release && ctest -R unit_tests --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```
