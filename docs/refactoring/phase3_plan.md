# Phase 3 Implementation Plan — Conditional Compilation

**Date written:** 2026-04-13
**Revised:** 2026-04-14 (--force-lru semantics clarified)
**Based on:** `execution_strategy.md` §Phase 3.
**Branch:** `feature/conditional-compilation` from `dev`, **after Phases 1 and 2 are merged**.
**Precondition:** `phase2_plan.md` success criteria all met; `USE_GENERIC_CACHE=ON` is either the default or universally validated.

---

## Goal

Produce three distinct solver binaries from the same source tree, each stripped of code that its cache-selection does not use:

| Binary | Cache | Zobrist inline update compiled in? | Notes |
|---|---|---|---|
| `solvitaire-flat` | `generic_flat_cache<CompactStatePolicy>` only | Yes | Single-deck, non-accordion, non-spider; rejects `--force-lru` |
| `solvitaire-hash-only` | `generic_flat_cache<HashOnlyPolicy>` only | Yes (hash only, no payload update) | Explicit opt-in, collision-risking fast mode; rejects `--force-lru` |
| `solvitaire-lru` | `lru_cache` only | **No** — Zobrist update compiled *out* | See §LRU binary semantics below |

The default `solvitaire` binary is **unchanged** — it continues to include all paths and dispatch at runtime via `cache_factory`. Phase 3 is additive: the three new binaries appear alongside, and the default remains the fallback until all variants are validated.

---

## LRU Binary Semantics (`solvitaire-lru`)

`solvitaire-lru` is the benchmarking counterpart to `solvitaire-flat`. Its routing rules:

| Game type | `--force-lru` absent | `--force-lru` present |
|---|---|---|
| Already LRU in standard binary (2-deck, spider, suit-symmetry, accordion) | **Runs** — these are LRU games, no conflict | **Runs** — `--force-lru` is redundant but not an error |
| Flat-cache-eligible (`use_new_cache == true`) | **Fails** — explicit opt-in required | **Runs** with `lru_cache` — the benchmarking path |

Rationale: preventing accidental use on flat-cache games without conscious intent. The `--force-lru` flag is an explicit acknowledgment that "I know this game normally uses a flat cache; I want LRU for benchmarking."

This is implemented in the `SOLVITAIRE_LRU_ONLY` factory branch:
```cpp
if (use_new_cache(rules, suit_sym) && !force_lru)
    throw std::runtime_error(
        "lru-only binary: game is flat-cache-eligible; "
        "pass --force-lru to run under lru_cache for benchmarking");
return std::make_unique<lru_cache>(gs, capacity);
```

## `--force-lru` Rejection in Other Variant Binaries

`solvitaire-flat` and `solvitaire-hash-only` reject `--force-lru` unconditionally — it is meaningless in a binary that compiles out the LRU path entirely:

```cpp
// In SOLVITAIRE_FLAT_ONLY and SOLVITAIRE_HASH_ONLY branches:
if (force_lru)
    throw std::runtime_error("this binary does not support --force-lru");
```

The standard `solvitaire` binary's `--force-lru` behaviour is unchanged.

---

## Strategic Note on Accordion

Accordion routes to the predecessor cache. Per current direction, accordion is deferred, so Phase 3 does **not** produce a dedicated `solvitaire-predecessor` binary. Accordion-rules games compiled in the three new binaries:

- `solvitaire-flat` — refuses at runtime: "rules require predecessor cache; use default solvitaire binary"
- `solvitaire-hash-only` — same refusal
- `solvitaire-lru` — runs (LRU can handle any game, just slower)

This is consistent with "do not work on accordion in this refactor."

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
inline std::unique_ptr<cache_interface> make_cache(
        const sol_rules& rules, const game_state& gs,
        uint64_t capacity, const std::string& cache_type,
        bool force_lru, bool suit_sym) {
#if defined(SOLVITAIRE_LRU_ONLY)
    // Games already routing to LRU in the standard binary run without restriction.
    // Flat-cache-eligible games require --force-lru as an explicit benchmarking opt-in.
    if (use_new_cache(rules, suit_sym) && !force_lru)
        throw std::runtime_error(
            "lru-only binary: game is flat-cache-eligible; "
            "pass --force-lru to run under lru_cache for benchmarking");
    return std::make_unique<lru_cache>(gs, capacity);
#elif defined(SOLVITAIRE_FLAT_ONLY)
    if (force_lru)
        throw std::runtime_error("flat-only binary: --force-lru is not supported");
    if (use_predecessor_cache(rules))
        throw std::runtime_error("flat-only binary: game requires predecessor cache; use default solvitaire binary");
    if (!use_new_cache(rules, suit_sym))
        throw std::runtime_error("flat-only binary: game requires LRU cache; use default solvitaire binary");
    return std::make_unique<generic_flat_cache<CompactStatePolicy>>(capacity);
#elif defined(SOLVITAIRE_HASH_ONLY)
    if (force_lru)
        throw std::runtime_error("hash-only binary: --force-lru is not supported");
    if (use_predecessor_cache(rules) || !use_new_cache(rules, suit_sym))
        throw std::runtime_error("hash-only binary: game not eligible for hash-only cache");
    return std::make_unique<generic_flat_cache<HashOnlyPolicy>>(capacity);
#else
    // default multi-cache dispatch (unchanged)
    if (cache_type == "hash-only") {
#ifdef USE_GENERIC_CACHE
        return std::make_unique<generic_flat_cache<HashOnlyPolicy>>(capacity);
#else
        return std::make_unique<hash_only_cache>(capacity);
#endif
    } else if (use_predecessor_cache(rules) && !force_lru) {
        ...
    }
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
        default=$(./cmake-build-release/bin/solvitaire           --type "$type" --random "$seed" --json | jq -r .outcome)
        flat=$(./cmake-build-release/bin/solvitaire-flat          --type "$type" --random "$seed" --json | jq -r .outcome)
        hash=$(./cmake-build-release/bin/solvitaire-hash-only     --type "$type" --random "$seed" --json | jq -r .outcome)
        lru=$(./cmake-build-release/bin/solvitaire-lru            --type "$type" --random "$seed" --json --force-lru | jq -r .outcome)
        if [[ "$default" != "$flat" || "$default" != "$lru" || "$default" != "$hash" ]]; then
            echo "MISMATCH $type seed=$seed default=$default flat=$flat hash=$hash lru=$lru" >&2
            exit 1
        fi
    done
done
echo "All outcomes match."
```

Note: `solvitaire-lru` is invoked with `--force-lru` for the flat-cache-eligible game types in the comparison set. `states_searched` and `backtracks` will differ across variants (by design — hash-only can false-positive, LRU canonicalises differently). Only `outcome` is compared.

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

Scope: `CMakeLists.txt`, `cache_factory.h`. Three new executables with compile-time routing including the `--force-lru` validation logic described above. Default binary still unchanged.

### Commit P3-C — Error handling for ineligible rules

Scope: `cache_factory.h` (already written in P3-B), plus wire the runtime error into `main.cpp` so the CLI emits a clean message (not a crash) when an ineligible game or flag combination is passed to a variant binary. Covers:
- `solvitaire-flat` / `solvitaire-hash-only` with `--force-lru`
- `solvitaire-flat` with accordion or LRU-only game
- `solvitaire-hash-only` with ineligible game
- `solvitaire-lru` with flat-cache-eligible game and no `--force-lru`

### Commit P3-D — `compare_binaries.sh` validation harness

Scope: new script, new `ctest` wrapper target `compare_binaries`. Runs on 10 klondike + 10 free-cell seeds, calling `solvitaire-lru` with `--force-lru` for those game types.

### Commit P3-E — Regression Level 1 for each variant binary

Extend the CTest regression definitions so `regression_level1_flat`, `regression_level1_lru`, `regression_level1_hash_only` targets exist and run the appropriate subset of Level 1 games.

- `regression_level1_flat`: flat-cache-eligible games from Level 1
- `regression_level1_lru`: two sets: (a) LRU-native games (no flag), (b) flat-cache-eligible games with `--force-lru`
- `regression_level1_hash_only`: games where false-positive collisions have been verified to not affect outcome correctness; document the allow-list explicitly

---

## Success Criteria

Phase 3 is complete when **all** of the following hold:

1. ✅ Default `solvitaire` binary: `ctest -R unit_tests` and `ctest -R regression_level1` pass, zero regressions vs. pre-Phase-3 `dev`.
2. ✅ `solvitaire-flat`, `solvitaire-hash-only`, `solvitaire-lru` all build without warnings under `-Werror`.
3. ✅ `compare_binaries.sh` passes on 10 klondike + 10 free-cell seeds (outcomes match across all four binaries; `solvitaire-lru` called with `--force-lru`).
4. ✅ `nm` on `solvitaire-lru` shows `update_zobrist_*` symbols absent (only `zobrist_hash::init` remains).
5. ✅ `regression_level1_flat` passes.
6. ✅ `regression_level1_lru` passes on both LRU-native games and `--force-lru` flat-cache games.
7. ✅ `regression_level1_hash_only` passes on the documented allow-list.
8. ✅ All ineligible flag+game combinations emit a clean error message (not crash or silent fallback):
   - `solvitaire-flat --force-lru` → error
   - `solvitaire-hash-only --force-lru` → error
   - `solvitaire-lru` + flat-cache game + no `--force-lru` → error
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
| `solvitaire-lru` run on flat-cache game without `--force-lru` silently succeeds instead of erroring | Unit test: construct factory with flat-eligible rules + no force_lru | Covered by P3-C error-path tests |
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
