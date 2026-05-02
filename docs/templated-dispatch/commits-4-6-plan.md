# Plan: Commits 4-6 — Complete Phase A

**Date:** 2026-05-02
**Branch:** `feature/templated-dispatch`
**Status:** Approved
**Predecessor:** `docs/templated-dispatch/ImplementationPlan.md` (Commits 0-3)

## Context

Commits 0-3b are complete. Commit 3b pulled forward all of original Commit 4 (solver templating, dispatch switches, `dynamic_cast` removal). The original 8-commit plan (0-7) is being revised to reflect what was actually done and what remains.

This plan covers the remaining work: rescuing skipped tests, removing dead code, removing legacy cache implementations, conditional member cleanup for LRU, and final docs.

## What's already eliminated vs what stays

**Eliminated (done in 3a/3b):**
- `SOLVITAIRE_COMPUTES_FLAT_HASH` — policy logic now in the type system via `if constexpr (Policy::computes_hash)`
- Runtime booleans `computing_flat_hash`, `computing_flat_payload`
- `using_flat_cache` and `dynamic_cast` chain in solver
- `force_lru` and `cache_type` constructor parameters (obsolete)

**Stays (correct to keep):**
- `SOLVITAIRE_FLAT_ONLY`, `SOLVITAIRE_HASH_ONLY`, `SOLVITAIRE_LRU_ONLY` — standard build-variant mechanism controlling which template instantiations compile. Not legacy policy logic.

## Dead storage analysis (LRU)

The original plan noted ~90 bytes of dead storage for `game_state_impl<LRUPolicy>`. The template conversion already solved most of this: `desc_store` uses `empty_descriptor_store` (zero-sized struct) for LRU. Remaining guaranteed-dead member:

- `zobrist_hash_value` (`uint64_t`, 8 bytes) — always dead for LRU (`computes_hash = false`)

The predecessor members (~180 bytes) are runtime-conditional on `rules.accordion_size > 0`, not policy-conditional, so template EBO cannot help there.

Approach: use `[[no_unique_address]]` with a conditional wrapper (C++17 has limited support; may need a small helper template), or accept the 8-byte overhead if the complexity isn't justified. Decision to be made during implementation.

---

## Commit 4: Rescue skipped tests + remove dead code

**Rescue 2 tests in `src/test/unit_tests/solver_cache_selection_test.cpp`:**
- `BlackHoleUsesNewCache` (line 18): Rewrite to use `solver_impl<FlatPolicy>` with `generic_flat_cache<CompactStatePolicy>` directly. Verify BlackHole seed 1 solves.
- `SolverWithFlatCacheProducesSameOutcome` (line 42): Rewrite to run `solver_impl<FlatPolicy>` twice per seed (seeds 1-3), compare outcomes for determinism.
- Both: Remove `#if 0` / `GTEST_SKIP()` wrappers.
- Leave `UseCacheSelectionFunction` (line 66) untouched — already active.

**Remove dead production code:**
- Delete `cache_factory.h` (`src/main/game/cache_factory.h`) — `make_cache()` has zero callers.
- Remove `needs_flat_hash()` and `needs_flat_payload()` from `cache_interface.h` (lines 57-75) — zero callers.

**Conditional member for LRU:**
- Remove dead `zobrist_hash_value` storage from `game_state_impl<LRUPolicy>` using a conditional member pattern (e.g., `std::conditional_t<Policy::computes_hash, uint64_t, empty_type>` or similar).

**Verification:** Build + unit tests (2/2) + Level 1 regression (4/4).

---

## Commit 5: Remove legacy cache code

**Delete legacy cache implementations (zero production callers, fully superseded by `generic_flat_cache<Policy>`):**
- `src/main/game/flat_cache.h` + `flat_cache.cpp`
- `src/main/game/hash_only_cache.h` + `hash_only_cache.cpp`
- `src/main/game/predecessor_flat_cache.h` + `predecessor_flat_cache.cpp`
- Remove from `CMakeLists.txt` source lists (lines 69-77)

**Delete dual_cache infrastructure:**
- `src/main/game/dual_cache.h` — includes `flat_cache.h`, all consumers are `#if 0`. Will be replaced by search trace infrastructure post-Phase A.

**Delete legacy cache test files (fully covered by `generic_flat_cache_test.cpp`):**
- `src/test/unit_tests/flat_cache_test.cpp` — all 7 tests mirrored by GenericCompactStateCacheTest suite
- `src/test/unit_tests/hash_only_cache_test.cpp` — tests 1-8 mirrored by GenericHashOnlyCacheTest; test 9 (DualCacheAgreement) already disabled

**Convert predecessor test file (has unique predecessor-semantic tests not in generic suite):**
- `src/test/unit_tests/predecessor_cache_test.cpp` — change `predecessor_flat_cache` to `generic_flat_cache<PredecessorClusterPolicy>`. Unique tests worth keeping:
  - Tests 1-2: AccordionUsesPredecessorCache, FreeCellDoesNotUsePredecessorCache (game_state behavior)
  - Tests 7-8: UndoRestoresToCachedState, PredecessorHashChangesAfterMove (predecessor state semantics)
  - Test 10: MultipleMovesAndUndos (game_state + cache integration)

**Delete disabled dual-cache test files (all `#if 0`, depend on deleted `dual_cache.h`):**
- `src/test/unit_tests/dual_cache_test.cpp`
- `src/test/unit_tests/generic_flat_dual_cache_test.cpp`
- `src/test/unit_tests/predecessor_dual_cache_test.cpp`
- `src/test/unit_tests/mismatch_analyzer.cpp`
- `src/test/unit_tests/mismatch_diagnostic.cpp`

**Update `CMakeLists.txt`:** Remove all deleted source and test files.

**Verification:** Build + unit tests + Level 1 regression (4/4).

---

## Commit 6: Final verification + docs

- Full Level 1-3 regression against oracles (12/12 with node counts enforced)
- Container build + test (`./scripts/container-build.sh --test`)
- Update `docs/proposals/PROPOSAL-templated-game-state-dispatch.md` — status to "Implemented"
- Update `docs/known-issues.md`:
  - Verify KI-8 closure wording
  - Update KI-18 to note dual-cache tests deleted (not just disabled), to be replaced by search trace
- Update CLAUDE.md architecture section (solver is `solver_impl<Policy>`, legacy caches removed)
- Update `docs/PICKUP.md` — mark Phase A as complete
- Update `01-Knowledge-Base/AI-Pickup.md` — reflect Phase A completion, set next work

**Verification:** Level 1-3 regression (12/12), container build.

---

## Deferred to post-Phase A

### Search trace infrastructure (replaces dual-cache testing, KI-18)
- Add instrumented search trace to `solver_impl<Policy>`: moves made (shared notation from `move.h` — from/to/count/type), cache insert/contains results (hit/miss), eviction events
- NOT hash values — hashing can legitimately change; move sequences are the invariant
- Run two solves with different policies, diff the traces
- Replaces the deleted `dual_cache` metamorphic testing approach
- Also useful for debugging, performance analysis, regression diagnosis

---

## Key files

| File | Commit | Action |
|------|--------|--------|
| `src/test/unit_tests/solver_cache_selection_test.cpp` | 4 | Rewrite 2 tests |
| `src/main/game/cache_factory.h` | 4 | Delete |
| `src/main/game/cache_interface.h` | 4 | Remove 2 dead functions |
| `src/main/game/search-state/game_state.h` | 4 | Conditional member for zobrist_hash_value |
| `src/main/game/flat_cache.h` + `.cpp` | 5 | Delete |
| `src/main/game/hash_only_cache.h` + `.cpp` | 5 | Delete |
| `src/main/game/predecessor_flat_cache.h` + `.cpp` | 5 | Delete |
| `src/main/game/dual_cache.h` | 5 | Delete |
| `src/test/unit_tests/flat_cache_test.cpp` | 5 | Delete |
| `src/test/unit_tests/hash_only_cache_test.cpp` | 5 | Delete |
| `src/test/unit_tests/predecessor_cache_test.cpp` | 5 | Convert to generic |
| `src/test/unit_tests/dual_cache_test.cpp` | 5 | Delete |
| `src/test/unit_tests/generic_flat_dual_cache_test.cpp` | 5 | Delete |
| `src/test/unit_tests/predecessor_dual_cache_test.cpp` | 5 | Delete |
| `src/test/unit_tests/mismatch_analyzer.cpp` | 5 | Delete |
| `src/test/unit_tests/mismatch_diagnostic.cpp` | 5 | Delete |
| `CMakeLists.txt` | 5 | Remove deleted files |
| Various docs | 6 | Update for Phase A completion |

## Verification

After each commit:
```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

Final (Commit 6):
```bash
cd cmake-build-release && ctest -R "regression_level[1-3]" --output-on-failure
./scripts/container-build.sh --test
```
