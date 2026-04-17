# Phase 2 Implementation Plan — Template Cache Unification

**Date written:** 2026-04-13
**Based on:** `execution_strategy.md` §Phase 2, reflecting post-Phase-1 state.
**Branch:** `feature/template-cache` from `dev` (NOT from `feature/pile-first-undo`).

---

## Goal

Introduce a single `generic_flat_cache<Policy>` template that subsumes the three existing fast caches:

| Existing class | Cluster size | Payload |
|---|---|---|
| `flat_cache` | 64 B | `compact_state` (32 B × 2) |
| `hash_only_cache` | 16 B | `uint64_t` hash only × 2 |
| `predecessor_flat_cache` | 128 B | 56 B packed predecessor + 8 B guard hash × 2 |

The three originals are **left untouched** in Phase 2 — they remain the reference implementations until Phase 5. The new template is added alongside and wired into `cache_factory.h` behind a compile flag `USE_GENERIC_CACHE`.

## Strategic Note on Accordion

Accordion is currently broken under debug builds (KI-7: `AccordionAgreement` asserts in `assert_payload_consistent()`). We will **still produce and instantiate** the predecessor policy for `generic_flat_cache<PredecessorPolicy>`, because the goal of Phase 2 is mechanical template unification, not correctness repair of accordion. Any Phase 2 test that fails because accordion was already broken is acceptable as long as (a) the failure mode matches the pre-Phase-2 behaviour exactly, and (b) the non-accordion specialisations are bit-identical to their originals. Accordion correctness (including predecessor cache semantics) will be tackled in a later dedicated phase.

---

## Design

### Policy concept

```cpp
// Policy requirements (duck-typed):
//   using payload_type;                      // what game_state provides
//   static constexpr size_t ENTRY_BYTES;     // 8, 32, or 64
//   static constexpr size_t ENTRIES_PER_CLUSTER = 2;
//   static uint64_t   hash_of(const game_state&);
//   static payload_type payload_of(const game_state&);
//   static bool       is_occupied(const entry&);
//   static void       set_occupied(entry&);
//   static bool       matches(const entry&, const payload_type&);
//   static uint8_t    get_depth(const entry&);            // may return 0
//   static void       write(entry&, const payload_type&); // occupancy+depth+payload
//   // Optional — only the predecessor policy uses it:
//   static constexpr bool HAS_HASH_GUARD = false;
```

Three concrete policies:

1. **`CompactStatePolicy`** — wraps `compact_state`, entries are 32 B, cluster = 64 B. Replacement: TwoBig1 depth-preferred.
2. **`HashOnlyPolicy`** — entries are `uint64_t`, cluster = 16 B. Empty sentinel = 0 (with 0→1 normalisation). No depth, simpler TwoBig1.
3. **`PredecessorPolicy`** — entries are 56 B packed payload + the cluster carries a `uint64_t other_hash` guard. Cluster = 128 B. Uses `get_predecessor_zobrist_hash` and `get_predecessor_payload`. This is the only policy where `HAS_HASH_GUARD = true`, and the template's `contains()`/`insert()` branch on that flag via `if constexpr`-style compile-time dispatch (C++14: use `std::enable_if` or tag dispatch).

### File layout (Phase 2 adds only):

```
src/main/game/generic_flat_cache.h            ← new (header-only template)
src/main/game/generic_flat_cache_policies.h   ← new (three policies)
```

Existing files (`flat_cache.*`, `hash_only_cache.*`, `predecessor_flat_cache.*`, `cache_factory.h`) are modified only to:
- Add `#ifdef USE_GENERIC_CACHE` dispatch in `cache_factory.h`.
- Nothing else touches the original implementations.

### Memory layout assertions (mandatory, human-signed-off values)

In `generic_flat_cache.h`:

```cpp
static_assert(sizeof(generic_flat_cache<CompactStatePolicy>::cluster) == 64, ...);
static_assert(sizeof(generic_flat_cache<HashOnlyPolicy>::cluster)     == 16, ...);
static_assert(sizeof(generic_flat_cache<PredecessorPolicy>::cluster)  == 128, ...);
static_assert(alignof(generic_flat_cache<CompactStatePolicy>::cluster) == 64, ...);
static_assert(alignof(generic_flat_cache<PredecessorPolicy>::cluster)  == 128, ...);
```

If any assertion fails the build fails — this is the whole point of the Phase 2 safety net. Do **not** "fix" failures by relaxing the asserts; fix them by restructuring the policy.

### Factory wiring

```cpp
// cache_factory.h
#ifdef USE_GENERIC_CACHE
  #include "generic_flat_cache.h"
  #include "generic_flat_cache_policies.h"
#endif

inline std::unique_ptr<cache_interface> make_cache(...) {
    if (cache_type == "hash-only") {
#ifdef USE_GENERIC_CACHE
        return std::make_unique<generic_flat_cache<HashOnlyPolicy>>(capacity);
#else
        return std::make_unique<hash_only_cache>(capacity);
#endif
    } else if (use_predecessor_cache(rules) && !force_lru) {
#ifdef USE_GENERIC_CACHE
        return std::make_unique<generic_flat_cache<PredecessorPolicy>>(capacity);
#else
        return std::make_unique<predecessor_flat_cache>(capacity);
#endif
    } else if (use_new_cache(rules, suit_sym) && !force_lru) {
#ifdef USE_GENERIC_CACHE
        return std::make_unique<generic_flat_cache<CompactStatePolicy>>(capacity);
#else
        return std::make_unique<flat_cache>(capacity);
#endif
    } else {
        return std::make_unique<lru_cache>(gs, capacity);
    }
}
```

### CMake

Add an option:

```cmake
option(USE_GENERIC_CACHE "Route cache_factory through generic_flat_cache<Policy>" OFF)
if(USE_GENERIC_CACHE)
    add_compile_definitions(USE_GENERIC_CACHE)
endif()
```

Default OFF so `dev` builds are unchanged.

---

## Commits

### Commit P2-A — Add `generic_flat_cache.h` + policies + static_asserts

Scope: new files only. No wiring. Builds must succeed with `-DUSE_GENERIC_CACHE=ON` even though nothing calls the template.

Includes all three policies and the three cluster-size static_asserts. No behavioural change to anything in the repo.

### Commit P2-B — Unit tests for each specialisation

New file `src/test/unit_tests/generic_flat_cache_test.cpp`. For each specialisation:

- insert/contains round-trip on a handful of states
- duplicate insert returns false
- different states are distinct
- eviction count rises when both slots taken
- cluster size matches original (runtime sizeof check as redundant safety)

The `PredecessorPolicy` tests use accordion `game_state`s and are expected to fail *only in the same ways* the existing `predecessor_flat_cache` tests fail (e.g. none, for insert/contains — the KI-7 crash is in `assert_payload_consistent`, not in cache code). If a test fails that does *not* have a matching failure on the original, stop and report.

### Commit P2-C — Wire `cache_factory.h` behind `USE_GENERIC_CACHE`

Add the `#ifdef` dispatch. Default build unchanged.

### Commit P2-D — DualCache parity harness for `CompactStatePolicy` and `HashOnlyPolicy`

Extend `predecessor_dual_cache_test.cpp` or add a new `generic_flat_dual_cache_test.cpp`. For each of the two non-accordion policies:

- Pair `generic_flat_cache<Policy>` (primary) with the original (reference) in a `dual_cache`.
- Run the solver on 5 klondike seeds and 5 free-cell seeds with cap = 100 000.
- `get_lru_only_hits()` and `get_flat_only_hits()` must both be zero.

**PredecessorPolicy is NOT tested in this commit** — it would re-trigger KI-7. A follow-up test marked `DISABLED_PredecessorParity` is added with a comment explaining why.

### Commit P2-E — Regression levels 1+2 under `USE_GENERIC_CACHE=ON`

Build release with `-DUSE_GENERIC_CACHE=ON`, run:

```bash
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure
ctest -R regression_level2 --output-on-failure
```

All must pass. Any failure that does not have a matching failure under the default build is a blocker.

---

## Success Criteria

Phase 2 is complete and mergeable when **all** of the following hold:

1. ✅ All three `static_assert`s on cluster sizes compile.
2. ✅ Unit tests for `generic_flat_cache_test.cpp` pass for all three specialisations (the predecessor specialisation tests only the hash-insert path, not accordion solver runs).
3. ✅ DualCache parity test for `CompactStatePolicy` shows zero divergence across 10 seeds.
4. ✅ DualCache parity test for `HashOnlyPolicy` shows zero divergence across 10 seeds (allowing for hash-only false positives, which appear as `flat_only_hits` — see note below).
5. ✅ With `-DUSE_GENERIC_CACHE=ON`: unit tests pass (modulo the same KI-3/KI-7 pre-existing failures), Level 1 passes, Level 2 passes.
6. ✅ With `-DUSE_GENERIC_CACHE=OFF` (default): all tests behave identically to current `dev`.
7. ✅ Three original cache source files (`flat_cache.cpp`, `hash_only_cache.cpp`, `predecessor_flat_cache.cpp`) are byte-identical to their pre-Phase-2 versions.

**Note on hash-only parity:** `hash_only_cache` can report HIT where `flat_cache` reports MISS (false positive by design). For the HashOnly dual test, pair `generic_flat_cache<HashOnlyPolicy>` against the **original `hash_only_cache`** (not against `flat_cache`); parity here means "bit-identical hit/miss decisions versus the original hash_only_cache," which is a tighter and more meaningful check.

## Explicit Non-Goals

- No change to `compact_state`, `predecessor_state`, or `zobrist.h`.
- No change to `game_state` hash/payload accessors.
- No deletion of original cache files (deferred to Phase 5).
- No accordion correctness work (KI-7 stays deferred).
- No memory-use verification (per user direction 2026-04-13).
- No performance benchmarking — Phase 2 is about equivalence, not speed.

## Rollback

Delete the branch. Original caches are untouched; default build is untouched; no tag or dev commit is affected.

## Risk Register

| Risk | Detection | Mitigation |
|---|---|---|
| Template padding silently changes cluster size | `static_assert` fails at compile | Use `alignas(N)` explicitly per policy; verify with `offsetof` in tests |
| `PredecessorPolicy` hash-guard optimisation lost | Benchmark hash-miss rate (Phase 2 success does not require this, but note for Phase 5) | Keep `HAS_HASH_GUARD` branch explicit in template |
| C++14 constraint prevents clean `if constexpr` | Compile error | Use tag dispatch or SFINAE — documented in `generic_flat_cache.h` header comment |
| DualCache parity fails for `CompactStatePolicy` | Commit P2-D tests | Stop and report; do not patch the template to match — investigate divergence |
| Predecessor test re-triggers KI-7 | Test output | Deliberately skip solver-based predecessor parity in P2-D (noted above) |
