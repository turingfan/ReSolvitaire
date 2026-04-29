# Implementation Prompt: Commit 3a — Template `game_state`

**Branch:** `feature/templated-dispatch`
**Prerequisite:** Commits 0, 1, 2 complete. You are on the correct branch.

## What To Read First

1. `CLAUDE.md` in the repo root — build commands, architecture, rules
2. `docs/templated-dispatch/commit3-plan.md` — full plan, especially §1–§4 and the three transformation patterns. Read the ENTIRE plan including the Amendment section at the end.
3. `src/main/game/cache_policy.h` — the four Policy structs from Commit 1
4. `src/main/game/search-state/game_state.h` — the class you are converting
5. `src/main/game/search-state/game_state.cpp` — the main file with all `#ifdef` sites
6. `01-Knowledge-Base/AGENTS.md` — mandatory rules (one commit, stop on bugs, etc.)

**Confirm to the user what you've read and what you plan to do before writing any code.**

## Goal

Convert `game_state` from a single class with runtime boolean flags and preprocessor guards into a template `game_state_impl<Policy>`. Add a `game_state` typedef at the bottom of the header so all external callers compile without change. No solver, cache, or main.cpp changes in this commit.

## Scope — Files That Change

| File | What to do |
|---|---|
| `src/main/game/cache_policy.h` | Add `static constexpr bool skip_pile_ordering` to each Policy: Flat=true, HashOnly=true, Predecessor=false, LRU=false |
| `src/main/game/search-state/game_state.h` | Full template conversion (§1a–1h in the plan) |
| `src/main/game/search-state/game_state.cpp` | Apply all three transformation patterns + explicit instantiations (§2a–2g) |
| `src/main/game/search-state/game_state.legal_moves.cpp` | Method prefix + explicit instantiations (§3) |
| `src/main/game/search-state/game_state.dominance_moves.cpp` | Method prefix + explicit instantiations (§3) |
| `src/main/game/search-state/game_state.pile_order.cpp` | Method prefix + explicit instantiations (§3) |

**Do NOT change any other files.** The `game_state` typedef ensures all callers (solver, caches, tests, main.cpp) compile without modification.

## Transformation Patterns (apply in game_state.cpp)

### Pattern A — Whole-block compile guard → `if constexpr`

```cpp
// BEFORE
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    <hash/payload work>
#endif

// AFTER
if constexpr (Policy::computes_hash) {
    <hash/payload work>
}
```

### Pattern B — Member-name selector → single `desc_store`

```cpp
// BEFORE
#ifdef SOLVITAIRE_HASH_ONLY
    hash_desc.get_descriptor(cid);
#else
    payload.get_descriptor(cid);
#endif

// AFTER
desc_store.get_descriptor(cid);
```

The Commit 2 audit confirmed all five method groups (clear, get/set_descriptor, get/set_foundation, get/set_waste_ptr, get/set_hole_top) have identical signatures on both `compact_state` and `hash_descriptor_store`.

### Pattern C — Runtime flag removal

```cpp
// BEFORE (inside a Pattern-A block)
if (computing_flat_hash) {
    <hash work>
}

// AFTER (inside an if constexpr block that already established computes_hash == true)
<hash work>   // runtime check removed; always true in this path
```

Similarly, `if (computing_flat_payload)` becomes `if constexpr (Policy::computes_payload)`.

## Detailed Steps

### Step 1: `cache_policy.h` — add `skip_pile_ordering`

Add `static constexpr bool skip_pile_ordering` to each Policy struct:
- `FlatPolicy`: `true`
- `HashOnlyPolicy`: `true`
- `PredecessorPolicy`: `false`
- `LRUPolicy`: `false`

### Step 2: `game_state.h` — template conversion

**2a. Remove the derived macro block** (lines ~28-36, the `SOLVITAIRE_COMPUTES_FLAT_HASH` definition). Delete entirely.

**2b. Replace conditional includes:**
```cpp
// DELETE the #ifndef SOLVITAIRE_HASH_ONLY / #else / #endif around compact_state.h and hash_descriptor_store.h
// REPLACE WITH: include both unconditionally
#include "../compact_state.h"
#include "../hash_descriptor_store.h"
#include "../cache_policy.h"
```

**2c. Convert class declaration:**
```cpp
// BEFORE
class game_state {

// AFTER
template <typename Policy>
class game_state_impl {
```

**2d. Delete runtime flags:**
Remove `computing_flat_hash` and `computing_flat_payload` member declarations (currently inside `#if SOLVITAIRE_COMPUTES_FLAT_HASH`).

**2e. Public accessor methods — conditional on policy:**
- `get_zobrist_hash()` — keep always-present
- `get_payload()`, `set_payload_depth()`, `compute_hash_from_scratch()` — guard with `static_assert(Policy::computes_payload)` in the body, or use `enable_if`
- `recompute_payload_from_scratch()`, `assert_payload_consistent()` — keep `#ifndef NDEBUG` guard, add `computes_payload` guard

**2f. Private members — descriptor store unification:**
```cpp
// DELETE:
#ifdef SOLVITAIRE_HASH_ONLY
    hash_descriptor_store hash_desc;
#else
    compact_state payload;
#endif

// REPLACE WITH:
typename Policy::descriptor_store_type desc_store;
```

`zobrist_hash_value` and `initially_face_up[52]` remain as always-present members. For LRU they are never written; the compiler eliminates dead stores at -O3.

**2g. `skip_pile_ordering`:**
Change initialization from `use_new_cache(s_rules, suit_sym) && !force_lru` to `Policy::skip_pile_ordering`.

**2h. Remove `force_lru` and `cache_type` from constructor parameters.** They were only used to compute the runtime flags which are now Policy traits. Check the constructor body — these parameters appear ONLY in:
1. `computing_flat_hash = needs_flat_hash(...)` → deleted
2. `computing_flat_payload = needs_flat_payload(...)` → deleted
3. `skip_pile_ordering = use_new_cache(...) && !force_lru` → becomes `Policy::skip_pile_ordering`

Keep `suit_sym` parameter if `skip_pile_ordering` needs it (it doesn't after becoming a Policy trait — remove it too if it's only used for skip_pile_ordering).

**2i. Remove `init_payload_and_hash` and `init_initially_face_up` `#if SOLVITAIRE_COMPUTES_FLAT_HASH` guards.** Keep them as always-declared private methods; their bodies use `if constexpr` to be no-ops for LRUPolicy.

**2j. Typedef at the bottom of the header:**
```cpp
#if defined(SOLVITAIRE_LRU_ONLY)
    using game_state = game_state_impl<LRUPolicy>;
#elif defined(SOLVITAIRE_FLAT_ONLY)
    using game_state = game_state_impl<FlatPolicy>;
#elif defined(SOLVITAIRE_HASH_ONLY)
    using game_state = game_state_impl<HashOnlyPolicy>;
#else
    using game_state = game_state_impl<FlatPolicy>;
#endif
```

### Step 3: `game_state.cpp` — apply all three patterns

**3a. Every `game_state::method_name` → `game_state_impl<Policy>::method_name`.** Add `template <typename Policy>` before every method definition.

**3b. Apply Pattern A** at every `#if SOLVITAIRE_COMPUTES_FLAT_HASH` site. Convert to `if constexpr (Policy::computes_hash)`.

**3c. Apply Pattern B** at every `#ifdef SOLVITAIRE_HASH_ONLY` site. Replace `hash_desc.X()` / `payload.X()` with `desc_store.X()`.

**3d. Apply Pattern C** — remove `if (computing_flat_hash)` guards inside blocks that are already `if constexpr (Policy::computes_hash)`. Remove `if (computing_flat_payload)` → `if constexpr (Policy::computes_payload)`.

**3e. `init_payload_and_hash()`:** The opening `if (!computing_flat_hash) return;` becomes unnecessary (the method is only called from within `if constexpr (Policy::computes_hash)` blocks). Delete the guard.

**3f. `check_face_down_consistent()` (debug):** The inner `#if SOLVITAIRE_COMPUTES_FLAT_HASH` block uses `payload.get_descriptor()`. Apply Pattern A (`if constexpr (Policy::computes_hash)`) and Pattern B (`desc_store.get_descriptor()`).

**3g. `set_payload_depth()` and `compute_hash_from_scratch()`:** Currently behind `#if SOLVITAIRE_COMPUTES_FLAT_HASH && !defined(SOLVITAIRE_HASH_ONLY)`. Replace with `if constexpr (Policy::computes_payload)` in the body.

**3h. Static member definitions:** `Z_pred[52][110]` and `Z_pred_initialised` are static. Use template syntax:
```cpp
template <typename Policy> uint64_t game_state_impl<Policy>::Z_pred[52][110];
template <typename Policy> bool     game_state_impl<Policy>::Z_pred_initialised = false;
```

Check for any other static members that need the same treatment.

**3i. Explicit instantiations at the bottom:**
```cpp
#include "../cache_policy.h"

#if defined(SOLVITAIRE_LRU_ONLY)
template class game_state_impl<LRUPolicy>;

#elif defined(SOLVITAIRE_FLAT_ONLY)
template class game_state_impl<FlatPolicy>;
template class game_state_impl<PredecessorPolicy>;

#elif defined(SOLVITAIRE_HASH_ONLY)
template class game_state_impl<HashOnlyPolicy>;

#else   // default binary — all four policies
template class game_state_impl<FlatPolicy>;
template class game_state_impl<HashOnlyPolicy>;
template class game_state_impl<PredecessorPolicy>;
template class game_state_impl<LRUPolicy>;
#endif
```

### Step 4: Split `.cpp` files

For each of `game_state.legal_moves.cpp`, `game_state.dominance_moves.cpp`, `game_state.pile_order.cpp`:

1. Add `template <typename Policy>` before every method definition
2. Change `game_state::` → `game_state_impl<Policy>::`
3. Add the same explicit instantiation block at the bottom (identical to §3i)

These files contain NO hash/payload logic — verify by grepping for `SOLVITAIRE_COMPUTES_FLAT_HASH`, `SOLVITAIRE_HASH_ONLY`, `computing_flat_hash`, `computing_flat_payload`, `payload.`, `hash_desc.`. All should return zero matches.

### Step 5: Verification grep

After all edits, grep the changed files for any remaining references to the old names. ALL of these must return zero matches in the changed files:

- `computing_flat_hash` (deleted runtime flag)
- `computing_flat_payload` (deleted runtime flag)
- `\bhash_desc\b` (replaced by `desc_store`)
- `\bpayload\b` in game_state.cpp (replaced by `desc_store` — note: `payload` may still appear in comments, in `get_payload()` method name, and in `computing_flat_payload` which should be deleted)
- `SOLVITAIRE_COMPUTES_FLAT_HASH` (deleted macro)
- `#ifdef SOLVITAIRE_HASH_ONLY` in game_state.cpp (all replaced by `if constexpr` or `desc_store`)

Note: `SOLVITAIRE_HASH_ONLY`, `SOLVITAIRE_LRU_ONLY`, `SOLVITAIRE_FLAT_ONLY` will still appear in the explicit instantiation blocks and the typedef — that's correct.

## Build and Test

After all changes:

```bash
./build.sh --release --unit-tests --variants
cd cmake-build-release && ctest -R unit_tests --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

ALL must pass. If any test fails, STOP and report the failure. Do not debug.

## What NOT To Do

- Do NOT change solver.h, solver.cpp, or main.cpp
- Do NOT change any cache files (cache_interface.h, cache_factory.h, flat_cache.*, etc.)
- Do NOT change test files — they must compile without modification via the `game_state` typedef
- Do NOT add features, refactor surrounding code, or make improvements beyond the specified transformation
- Do NOT investigate or fix test failures — report them
- Do NOT guess at domain semantics — ask if unsure

## Commit Message

When all tests pass, commit with:

```
refactor: convert game_state to template game_state_impl<Policy>

Convert game_state to game_state_impl<Policy> template parameterized on
cache dispatch policy (FlatPolicy, HashOnlyPolicy, PredecessorPolicy,
LRUPolicy). Replace all #ifdef SOLVITAIRE_HASH_ONLY member-name selectors
with unified desc_store member. Replace all #if SOLVITAIRE_COMPUTES_FLAT_HASH
compile guards with if constexpr (Policy::computes_hash). Remove runtime
computing_flat_hash/computing_flat_payload flags. Add skip_pile_ordering
trait to policy structs. game_state typedef preserves API for all callers.

Files changed: cache_policy.h, game_state.h, game_state.cpp,
game_state.legal_moves.cpp, game_state.dominance_moves.cpp,
game_state.pile_order.cpp.

All variant binaries build clean. Unit tests pass. Level 1 regression
4/4 with node counts enforced.
```
