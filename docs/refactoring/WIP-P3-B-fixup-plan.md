# WIP: P3-B Fixup Plan — Strip Flat-Cache Code from LRU Binary

**Date:** 2026-04-16  
**Branch:** `feature/conditional-compilation`  
**Status:** WIP — plan agreed, ready to implement

---

## Context

The agent work on P3-B through P3-D was evaluated and found to have two bugs:

1. **Predecessor routing inverted** — `solvitaire-flat` was refusing accordion games; `solvitaire-lru` was silently accepting them. Predecessor cache is a flat-array cache and belongs in the flat binary, not LRU. **Fixed on branch** (see §What Is Already On Branch).

2. **P3-A was too narrow** — Only the Zobrist XOR update lines were guarded by `SOLVITAIRE_COMPUTES_FLAT_HASH`. The `payload.set_*()` calls, the `compact_state payload` member, and the flat-cache `.cpp` files all still compile into `solvitaire-lru`. Furthermore the default binary wastes cycles computing hash and payload for LRU-routed games at runtime.

This revealed a deeper architectural issue: see `docs/known-issues.md` §8 and `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`.

---

## What Is Already On Branch (P3-B-fix-1)

Three files changed to fix predecessor routing and begin stripping predecessor code from LRU binary:

**`src/main/game/cache_factory.h`**:
- `SOLVITAIRE_LRU_ONLY` branch: added rejection for `use_predecessor_cache(rules)` — accordion now throws "use solvitaire-flat"
- `SOLVITAIRE_FLAT_ONLY` branch: replaced predecessor rejection with predecessor support — `return std::make_unique<predecessor_flat_cache>(capacity)`
- `#include "predecessor_flat_cache.h"` guarded with `#if !defined(SOLVITAIRE_LRU_ONLY)`

**`src/main/game/predecessor_flat_cache.cpp`**:
- Entire content wrapped with `#if !defined(SOLVITAIRE_LRU_ONLY)`

**`src/main/solver/solver.cpp`**:
- `#include "../game/predecessor_flat_cache.h"` guarded with `#if !defined(SOLVITAIRE_LRU_ONLY)`
- `dynamic_cast<predecessor_flat_cache*>` guarded with `#if !defined(SOLVITAIRE_LRU_ONLY)`

Smoke tests confirmed:
- `solvitaire-lru --type late-binding-solitaire` → error (correct)
- `solvitaire-flat --type late-binding-solitaire` → runs (correct)
- `solvitaire-flat --type gaps-basic-variant` → error (correct)
- `solvitaire-lru --type klondike --force-lru` → runs (correct)

**Not yet committed or pushed** — will be committed as part of the two-commit plan below.

---

## Two-Commit Plan

### How game_state learns its policy booleans

The cache routing decision (which determines `computing_flat_hash` and `computing_flat_payload`) is purely a function of information available before game_state is constructed: `rules`, `force_lru`, `suit_sym`, and `cache_type` string. These are all known at the point where game_state is constructed in `solve_game()`.

A small helper is added to `cache_factory.h` (or `cache_interface.h`) alongside the existing `use_new_cache()` and `use_predecessor_cache()` helpers:

```cpp
inline bool needs_flat_hash(const sol_rules& rules, bool suit_sym,
                             bool force_lru, const std::string& cache_type) {
    if (force_lru) return false;
    return use_predecessor_cache(rules)
        || cache_type == "hash-only"
        || use_new_cache(rules, suit_sym);
}

inline bool needs_flat_payload(const sol_rules& rules, bool suit_sym,
                                bool force_lru, const std::string& cache_type) {
    if (force_lru) return false;
    if (cache_type == "hash-only") return false;   // hash-only: hash without payload
    return use_predecessor_cache(rules) || use_new_cache(rules, suit_sym);
}
```

game_state constructors take these two bools (or take the same parameters and call the helpers). The booleans are set once at construction and never change.

---

### Commit 1 — Runtime boolean guards in default binary

**Goal:** Default binary no longer computes hash or payload for LRU-routed games.  
**Scope:** `game_state.h`, `game_state.cpp`, `cache_interface.h` (helpers), `solver.cpp` call sites  
**Variant binaries:** Unchanged — all four still build and behave as before

**`cache_interface.h`** — add `needs_flat_hash()` and `needs_flat_payload()` helpers (see above).

**`game_state.h`**:
- Add `bool computing_flat_hash` and `bool computing_flat_payload` members
- Add them to constructors (derived via helpers before construction, passed in)

**`game_state.cpp`** — guard each site as follows:

*Four `update_*` functions* — combine runtime and existing compile-time guards:
```cpp
// update_card_descriptor example:
void game_state::update_card_descriptor(uint8_t cid, uint8_t new_desc) {
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) {
        uint8_t old_desc = payload.get_descriptor(cid);
        payload.set_descriptor(cid, new_desc);   // must update even for hash-only (KI-9)
        zobrist_hash_value ^= zobrist_hash::card_key(cid, old_desc)
                            ^ zobrist_hash::card_key(cid, new_desc);
    }
#endif
}
```
Same pattern for `update_foundation_in_hash`, `update_waste_ptr_in_hash`, `update_hole_top_in_hash` —
the corresponding `payload.set_*` call is unconditional within the `if (computing_flat_hash)` block.

Note: the `else if (computing_flat_payload)` branch from earlier drafts is dead code since
`computing_flat_payload` is always a strict subset of `computing_flat_hash`.

*Other sites:*
- `zobrist_hash_value = 0` (constructor, line ~158) → `if (computing_flat_hash)`
- `init_payload_and_hash()` call sites (lines 168, 303, 349) → `if (computing_flat_hash)` (not `computing_flat_payload` — payload must be initialised whenever the hash is tracked)
- `init_payload_and_hash()` definition → body guarded by `if (computing_flat_hash)` internally
- `set_payload_depth()` definition → guard body with `if (computing_flat_payload)` (cache-key use only)
- `compute_hash_from_scratch()` → guard body with `if (computing_flat_hash)`
- `recompute_payload_from_scratch()` → guard body with `if (computing_flat_payload)`
- `assert_payload_consistent()` → guard body with `if (computing_flat_payload)`
- Line 474: `old_ht = payload.get_hole_top()` → `if (computing_flat_hash)` (needed for hash XOR)
- Line 1097: `payload.get_descriptor(cid) == compact_state::STARTING` → `if (computing_flat_hash)` (reads descriptor state)

**`solver.cpp`** — guard call sites (not includes, not dynamic_casts — those stay for Commit 2):
- `state.set_payload_depth(...)` → `if (state.computing_flat_payload)`  
- `state.assert_payload_consistent()` → already inside `#ifndef NDEBUG`; add `if (state.computing_flat_payload)` guard

**Test gate before committing:**
```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```
All must pass with no new failures beyond KI-3 and KI-7. Variant binary smoke tests also run to confirm no regression there.

---

### Commit 2 — Compile-time guards for variant binaries

**Goal:** `solvitaire-lru` binary contains no flat-cache code at all; `nm` check clean.  
**Scope:** `game_state.h`, `game_state.cpp`, flat-cache `.cpp` files, `solver.cpp` includes/casts, `cache_factory.h` includes

**`game_state.h`** — wrap with `#if SOLVITAIRE_COMPUTES_FLAT_HASH`:
- `get_zobrist_hash()`, `get_payload()`, `set_payload_depth()`, `compute_hash_from_scratch()` (lines 83–86)
- `recompute_payload_from_scratch()`, `assert_payload_consistent()` (lines 95–96)
- `uint64_t zobrist_hash_value`, `compact_state payload`, `init_payload_and_hash()` (lines 192–197)
- `bool computing_flat_hash`, `bool computing_flat_payload` members — also guarded (in LRU_ONLY build these don't exist; the code that uses them is also guarded)

Note: `#include "../compact_state.h"` stays unconditional — enum constants (`compact_state::STARTING` etc.) appear at ~20 call sites as arguments to `update_card_descriptor()`. Those calls compile fine in LRU_ONLY since the function body is entirely inside `#if SOLVITAIRE_COMPUTES_FLAT_HASH`.

**`game_state.cpp`** — the `#if SOLVITAIRE_COMPUTES_FLAT_HASH` guards from Commit 1 already surround all hash/payload code. Verify each site is correctly enclosed; no additional wrapping needed beyond what Commit 1 established.

**Flat-cache `.cpp` files** — wrap entire content with `#if !defined(SOLVITAIRE_LRU_ONLY)`:
- `flat_cache.cpp`
- `hash_only_cache.cpp`
- `compact_state.cpp` — check first: if it only contains payload encoding logic, guard it; if it has enum/constant definitions used elsewhere, leave unconditional
- `parent_table.cpp` — check first: if flat-cache-only, guard it

**`solver.cpp`** — guard with `#if !defined(SOLVITAIRE_LRU_ONLY)`:
- `#include "../game/flat_cache.h"`
- `#include "../game/hash_only_cache.h"`
- `#include "../game/generic_flat_cache.h"`
- `#include "../game/dual_cache.h"` (verify usage first)
- `dynamic_cast<flat_cache*>` check
- `dynamic_cast<hash_only_cache*>` check
- `dynamic_cast<generic_flat_cache_base*>` check

**`cache_factory.h`** — guard with `#if !defined(SOLVITAIRE_LRU_ONLY)`:
- `#include "flat_cache.h"`
- `#include "hash_only_cache.h"`

**Test gate before committing:**
```bash
# 1. All four variant binaries build without warnings
cd cmake-build-release && make -j4 solvitaire-flat solvitaire-hash-only solvitaire-lru

# 2. nm check — no flat-cache symbols in LRU binary
nm cmake-build-release/bin/solvitaire-lru | grep -v ' U ' | c++filt \
    | grep -E "flat_cache|hash_only|compact_state|payload" || echo "clean"

# 3. Default binary regression still clean
ctest -R unit_tests --output-on-failure
ctest -R regression_level1 --output-on-failure

# 4. Variant binary smoke tests
bin/solvitaire-lru --type late-binding-solitaire --random 1    # → error
bin/solvitaire-flat --type late-binding-solitaire --random 1   # → runs
bin/solvitaire-flat --type gaps-basic-variant --random 1       # → error
bin/solvitaire-lru --type klondike --force-lru --random 1      # → runs
```

---

## Notes on `computing_flat_hash` vs `SOLVITAIRE_COMPUTES_FLAT_HASH`

The macro and the boolean serve different purposes and coexist:

| | Macro `SOLVITAIRE_COMPUTES_FLAT_HASH` | Bool `computing_flat_hash` |
|---|---|---|
| When evaluated | Compile time | Runtime (once, at construction) |
| Applies to | Variant binaries only | Default binary only |
| Effect | Removes code from binary entirely | Skips execution of code that is present |
| Set by | CMake `target_compile_definitions` | Constructor, via `needs_flat_hash()` helper |

In the default binary `SOLVITAIRE_COMPUTES_FLAT_HASH = 1` always, so the `#if` guards are no-ops and the runtime booleans do all the work. In the variant binaries the booleans don't exist in the struct (guarded out) and the `#if` does all the work.

---

## Known Unknowns (check before Commit 2)

- Does `compact_state.cpp` contain anything used outside the flat-cache path?
- Does `parent_table.cpp` contain anything used outside the flat-cache path?
- Does `dual_cache.h`/`dual_cache.cpp` need guarding in solver.cpp?
