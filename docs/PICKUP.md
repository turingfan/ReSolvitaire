# Pickup: feature/templated-dispatch

**Last updated:** 2026-04-28
**Branch:** `feature/templated-dispatch` (from `dev` at `1dd0882`)

## What This Branch Does

Replaces the dual mechanism (runtime booleans + preprocessor `#ifdef` guards) for cache policy selection in `game_state` with a single C++ template approach: `game_state_impl<Policy>`.

Design document: `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`
Full plan: `/Users/ipg/.claude/plans/keen-crafting-treehouse.md`

## Commits Done

### Commit 0: Test infrastructure (01fae5b)
- Generated LRU node-count oracles for Level 1-3 (`tests/oracles/level{1,2,3}_lru.json`)
- Added `--enforce-node-counts` flag to `scripts/regression_runner.py`
- Updated all Level 1-3 CTest targets to enforce node counts
- All Level 1 regression tests pass (default, flat, hash-only, LRU)

## Immediate Next Step: Regenerate Level 2-3 oracles

Level 2 regression with `--enforce-node-counts` failed for default, flat, and hash-only targets — existing oracles were generated with a pre-refactoring binary. LRU oracles pass (just generated). Need to regenerate:
- `tests/oracles/level2.json` — `--regenerate` with default binary
- `tests/oracles/level2_hash_only.json` — `--regenerate --cache-type hash-only`
- `tests/oracles/level3.json` — same pattern
- `tests/oracles/level3_hash_only.json` — same pattern

Then verify all Level 1-3 pass. Amend Commit 0 or make a follow-up commit.

## Next Code Commit

### Commit 1: Add policy structs (pure addition)
Create `src/main/game/cache_policy.h` with four policy structs (FlatPolicy, HashOnlyPolicy, PredecessorPolicy, LRUPolicy). Each provides `static constexpr bool computes_hash` and `computes_payload`, plus a `descriptor_store_type` typedef. No existing code changes.

## Key Decisions

- C++ standard: not locked to C++14; can use C++17
- Conditional members: save ~90 bytes for LRU (use EBO)
- Solver: template on game_state type (Option A)
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
