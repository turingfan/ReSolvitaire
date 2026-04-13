# Phase 3 Implementation Plan — Conditional Compilation

**Date written:** 2026-04-13
**Based on:** `execution_strategy.md` §Phase 3.
**Branch:** `feature/conditional-compilation` from `dev`, **after Phases 1 and 2 are merged**.
**Precondition:** `phase2_plan.md` success criteria all met; `USE_GENERIC_CACHE=ON` is either the default or universally validated.

---

## Goal

Produce three distinct solver binaries from the same source tree, each stripped of code that its cache-selection does not use:

| Binary | Cache | Zobrist inline update compiled in? | Notes |
|---|---|---|---|
| `solvitaire-flat` | `generic_flat_cache<CompactStatePolicy>` only | Yes | Single-deck, non-accordion, non-spider |
| `solvitaire-hash-only` | `generic_flat_cache<HashOnlyPolicy>` only | Yes (hash only, no payload update) | Explicit opt-in, collision-risking fast mode |
| `solvitaire-lru` | `lru_cache` only | **No** — Zobrist update compiled *out* | Handles two-deck, spider, accordion, suit-symmetry |

The default `solvitaire` binary is **unchanged** — it continues to include all paths and dispatch at runtime via `cache_factory`. Phase 3 is additive: the three new binaries appear alongside, and the default remains the fallback until all variants are validated.

## Strategic Note on Accordion

Accordion routes to the predecessor cache. Per current direction, accordion is deferred, so Phase 3 does **not** produce a dedicated `solvitaire-predecessor` binary. Accordion-rules games compiled in any of the three new binaries should:

- `solvitaire-flat` — refuse at runtime (`use_predecessor_cache(rules) == true` → fatal error: "rules require predecessor cache; use default solvitaire binary").
- `solvitaire-hash-only` — same refusal, or allow only if the user explicitly overrides (TBD; default = refuse).
- `solvitaire-lru` — allow (LRU can handle any game, just slow).

This is consistent with "do not work on accordion in this refactor" while still producing three clean binaries.

---

## Compile Flags

Introduce three mutually-exclusive flags (enforced in CMake):

```cmake
option(SOLVITAIRE_FLAT_ONLY      "Build flat-cache-only variant"      OFF)
option(SOLVITAIRE_HASH_ONLY      "Build hash-only variant"            OFF)
option(SOLVITAIRE_LRU_ONLY       "Build LRU-only variant"             OFF)
```

At most one may be set. When none are set, the default binary is produced. In source:

```cpp
#if defined(SOLVITAIRE_FLAT_ONLY)
    #define SOLVITAIRE_COMPUTES_FLAT_HASH 1
#elif defined(SOLVITAIRE_HASH_ONLY)
    #define SOLVITAIRE_COMPUTES_FLAT_HASH 1
#elif defined(SOLVITAIRE_LRU_ONLY)
    #define SOLVITAIRE_COMPUTES_FLAT_HASH 0
#else
    #define SOLVITAIRE_COMPUTES_FLAT_HASH 1   /* default: compile everything */
#endif
```

All Zobrist inline update calls in `game_state::make_*` / `undo_*` are wrapped:

```cpp
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    update_zobrist_for_regular_move(m, old_desc, new_desc);
#endif
```

This does **not** remove the pile-first undo reconstruction logic from Phase 1 — only the hash *update* call itself. The pile state transitions remain unconditional (they are also needed by `lru_cache` for canonicalisation).

---

## CMake Target Structure

Add three `add_executable` targets alongside the existing `solvitaire`:

```cmake
add_executable(solvitaire-flat     ${SOLVITAIRE_SOURCES})
target_compile_definitions(solvitaire-flat PRIVATE SOLVITAIRE_FLAT_ONLY)

add_executable(solvitaire-hash-only ${SOLVITAIRE_SOURCES})
target_compile_definitions(solvitaire-hash-only PRIVATE SOLVITAIRE_HASH_ONLY)

add_executable(solvitaire-lru       ${SOLVITAIRE_SOURCES})
target_compile_definitions(solvitaire-lru PRIVATE SOLVITAIRE_LRU_ONLY)
```

Each reuses the same source list; no source is duplicated. The default `solvitaire` target is untouched.

The `cache_factory::make_cache` function gets compile-time gates:

```cpp
inline std::unique_ptr<cache_interface> make_cache(...) {
#if defined(SOLVITAIRE_LRU_ONLY)
    return std::make_unique<lru_cache>(gs, capacity);
#elif defined(SOLVITAIRE_FLAT_ONLY)
    if (use_predecessor_cache(rules))
        throw std::runtime_error("flat-only binary: game requires predecessor cache");
    if (!use_new_cache(rules, suit_sym))
        throw std::runtime_error("flat-only binary: game requires LRU cache");
    return std::make_unique<generic_flat_cache<CompactStatePolicy>>(capacity);
#elif defined(SOLVITAIRE_HASH_ONLY)
    if (use_predecessor_cache(rules) || !use_new_cache(rules, suit_sym))
        throw std::runtime_error("hash-only binary: game not eligible");
    return std::make_unique<generic_flat_cache<HashOnlyPolicy>>(capacity);
#else
    // default multi-cache dispatch (unchanged)
    ...
#endif
}
```

---

## Validation Harness

New script: `scripts/compare_binaries.sh`.

```bash
#!/usr/bin/env bash
set -euo pipefail
SEEDS=${1:-"1 2 3 4 5 6 7 8 9 10"}
TYPES="klondike free-cell"
for type in $TYPES; do
    for seed in $SEEDS; do
        default=$(./cmake-build-release/solvitaire              --type "$type" --random "$seed" --json | jq -r .outcome)
        flat=$(./cmake-build-release/solvitaire-flat             --type "$type" --random "$seed" --json | jq -r .outcome)
        hash=$(./cmake-build-release/solvitaire-hash-only        --type "$type" --random "$seed" --json | jq -r .outcome)
        lru=$(./cmake-build-release/solvitaire-lru               --type "$type" --random "$seed" --json | jq -r .outcome)
        if [[ "$default" != "$flat" || "$default" != "$lru" || "$default" != "$hash" ]]; then
            echo "MISMATCH $type seed=$seed default=$default flat=$flat hash=$hash lru=$lru" >&2
            exit 1
        fi
    done
done
```

Outcomes must match exactly. `states_searched` and `backtracks` will differ across variants (by design — hash-only can false-positive, LRU canonicalises differently). Only `outcome` is compared.

## Verifying Zobrist is Actually Stripped

For `solvitaire-lru`, verify the Zobrist update calls are gone:

```bash
nm cmake-build-release/bin/solvitaire-lru | grep -i zobrist | grep -v ' U '
# Expected: zobrist_hash::init still present (used by game_state construction);
#           update_zobrist_for_* functions absent.
```

This is a manual verification step in the PR, not an automated test. Document the exact grep in the plan.

---

## Commits

### Commit P3-A — Add `SOLVITAIRE_COMPUTES_FLAT_HASH` guard macro and wrap Zobrist update calls

Scope: `game_state.cpp` only. Define the macro (defaults to 1 when no variant flag is set), wrap all `update_zobrist_*` call sites. No CMake changes. Default build produces byte-identical behaviour (macro = 1).

### Commit P3-B — Add CMake variant targets and factory dispatch

Scope: `CMakeLists.txt`, `cache_factory.h`. Three new executables, factory compile-time branching. Default binary still unchanged.

### Commit P3-C — Error handling for ineligible rules

Scope: `cache_factory.h`, plus wire the runtime error into `main.cpp` so the CLI emits a clean message (not a crash) when an ineligible game is passed to a variant binary.

### Commit P3-D — `compare_binaries.sh` validation harness

Scope: new script, new `ctest` wrapper target `compare_binaries`. Runs on 10 klondike + 10 free-cell seeds.

### Commit P3-E — Regression Level 1 for each variant binary

Extend the CTest regression definitions so `regression_level1_flat`, `regression_level1_lru`, `regression_level1_hash_only` targets exist and run the appropriate subset of Level 1 games. (Hash-only is run only on games where false-positive collisions have been verified to not affect outcome correctness; document the allow-list explicitly.)

---

## Success Criteria

Phase 3 is complete when **all** of the following hold:

1. ✅ Default `solvitaire` binary: `ctest -R unit_tests` and `ctest -R regression_level1` pass, zero regressions vs. pre-Phase-3 `dev`.
2. ✅ `solvitaire-flat`, `solvitaire-hash-only`, `solvitaire-lru` all build without warnings under `-Werror`.
3. ✅ `compare_binaries.sh` passes on 10 klondike + 10 free-cell seeds (outcomes match across all four binaries).
4. ✅ `nm` on `solvitaire-lru` shows `update_zobrist_*` symbols absent (only `zobrist_hash::init` remains).
5. ✅ `regression_level1_flat` passes.
6. ✅ `regression_level1_lru` passes (including 2-deck and spider games that were previously LRU anyway).
7. ✅ `regression_level1_hash_only` passes on the documented allow-list.
8. ✅ Ineligible game + variant binary combinations emit a clean error message, not a crash or silent fallback.
9. ✅ Level 2 regression on default `solvitaire` still passes.

## Explicit Non-Goals

- No accordion-only binary.
- No performance benchmarking (deferred).
- No deletion of the default multi-cache dispatch path (deferred to Phase 5).
- No change to `generic_flat_cache.h` (delivered in Phase 2).
- No change to `cache_interface.h` virtual methods.
- No change to regression levels 3–5 (manual run only if user requests).

## Rollback

- Each commit is independently revertable.
- Deleting the branch leaves `dev` untouched (default build never depended on the variant flags).
- The `SOLVITAIRE_COMPUTES_FLAT_HASH` macro defaults to 1, so P3-A is a no-op on the default build.

## Risk Register

| Risk | Detection | Mitigation |
|---|---|---|
| `#if SOLVITAIRE_COMPUTES_FLAT_HASH` guards wrong scope (e.g. wraps pile update instead of hash update) | `compare_binaries.sh` mismatch | Keep guards narrow: one call site at a time; no guard wraps >3 lines |
| `solvitaire-lru` still computes Zobrist via indirect include | `nm` check | P3 commits P3-A and P3-B split precisely so this is reviewable |
| Ineligible game crashes instead of erroring cleanly | Unit test with ineligible rules + variant binary | Commit P3-C adds explicit error path |
| Hash-only binary reports wrong outcome due to collision | `compare_binaries.sh` | Restrict hash-only to klondike + free-cell initially; expand allow-list only after validation |
| Default binary accidentally affected by variant flag leaks | Level 1 regression on default build | Mandatory per Success Criterion 1 |
| Variant binary pulls in wrong cache at link time | `nm` + unit test that instantiates `make_cache` for ineligible rules | P3-C |

---

## Dependencies Between Phases

```
Phase 1 (pile-first undo)  ─┐
                            ├──►  merged to dev  ──►  Phase 3
Phase 2 (template cache)  ──┘
```

Phase 3 must not start until both Phase 1 and Phase 2 are in `dev`, because:

- Phase 3's `SOLVITAIRE_COMPUTES_FLAT_HASH=0` path relies on Phase 1 having eliminated the old undo stack (otherwise LRU-only binary would still carry dead code around the stack).
- Phase 3's variant binaries use `generic_flat_cache<Policy>` from Phase 2.

Per strategy doc: "Phase 3 only starts after both 1 and 2 are in `dev`."
