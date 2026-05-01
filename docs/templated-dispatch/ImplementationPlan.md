# Plan: Templated `game_state` Dispatch (Phase A, KI-8)

## Context

`game_state` currently uses two mechanisms to avoid dead hash/payload work:
- **Runtime booleans** (`computing_flat_hash`, `computing_flat_payload`) checked at ~20 sites
- **Preprocessor guards** (`#ifdef SOLVITAIRE_HASH_ONLY`, `SOLVITAIRE_LRU_ONLY`, etc.) for variant binaries

Both encode the same 4-way policy decision. Replace both with a single template mechanism: `game_state_impl<Policy>`. This makes cache policy a first-class type-system concept, enabling Phase B to add new policies mechanically.

Design document: `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`
Branch: `feature/templated-dispatch`

## Design Decisions (from Ian, 2026-04-28)

- **C++ standard:** Not locked to C++14; can use C++17 if needed
- **Conditional members:** Save the ~90 bytes for LRU — use EBO or conditional members, don't include dead storage
- **Solver:** Option A — template `solver` on game_state type (zero virtual dispatch)
- **Return type:** Extract result + init_state into a non-templated struct
- **Conditional compilation:** All `#ifdef` guards (`SOLVITAIRE_HASH_ONLY`, `SOLVITAIRE_LRU_ONLY`, `SOLVITAIRE_FLAT_ONLY`, `SOLVITAIRE_COMPUTES_FLAT_HASH`) eliminated by end of phase
- **Testing:** Create node-count oracles for Level 1-3 before changes; post-implementation must be identical
- **Workflow:** Opus plans each commit, writes prompt for lower-level model to implement, then verifies

---

## Commit Sequence

### Commit 0: Generate pre-refactoring node-count oracles

**Before any code changes.** Run existing solver against Level 1-3 regression suites and capture full node-count oracles. These become the ground truth. Post-refactoring results must be byte-identical.

- Run Level 1-3 for default binary (all cache types exercised)
- Run Level 1-3 for each variant binary (flat, hash-only, lru)
- Store oracles in `tests/oracles/` with clear naming
- Verify existing oracles match (sanity check)

### Commit 1: Add policy structs (pure addition)

Create `src/main/game/cache_policy.h` with four policy structs:

```
| Policy            | computes_hash | computes_payload | descriptor_store_type     |
|-------------------|---------------|------------------|---------------------------|
| FlatPolicy        | true          | true             | compact_state             |
| HashOnlyPolicy    | true          | false            | hash_descriptor_store     |
| PredecessorPolicy | true          | true             | compact_state             |
| LRUPolicy         | false         | false            | (empty)                   |
```

Each struct provides `static constexpr bool` traits. No existing code changes.

### Commit 2: Unify descriptor store API

`compact_state` and `hash_descriptor_store` have similar but not identical APIs for `get_descriptor()`/`set_descriptor()`, foundation, waste_ptr, hole_top access. Make them present a uniform interface so template code can use either without `#ifdef`.

This may involve adding methods to one or both types, or creating a thin adapter. Pure API addition — no behavior change.

### Commit 3: Templatize `game_state`

The big one:
- Rename `class game_state` to `template<typename Policy> class game_state_impl`
- Use EBO or conditional base class to exclude hash/payload members for LRU
- Replace `if (computing_flat_hash)` with `if (Policy::computes_hash)` (compiler eliminates dead branches)
- Replace `#ifdef SOLVITAIRE_HASH_ONLY` storage selection with `Policy::descriptor_store_type`
- Add `using game_state = game_state_impl<FlatPolicy>;` default typedef (preserves all downstream code)
- Remove `computing_flat_hash`, `computing_flat_payload` booleans
- Remove `cache_type` string and `force_lru` from constructors (policy is template param)
- Explicit instantiations at bottom of each `.cpp` file
- `game_state.legal_moves.cpp`, `game_state.dominance_moves.cpp`, `game_state.pile_order.cpp` — just add instantiation declarations (no policy-dependent code in them)

### Commit 4: Templatize solver + main dispatch

- `template<typename GameState> class solver_impl`
- Extract non-templated `solve_result` struct for return from `solve_game()`
- Remove `using_flat_cache` bool and `dynamic_cast` chain from solver constructor
- Policy-based dispatch: solver knows at compile time whether it's using flat cache
- `solve_game()` in main.cpp: dispatch switch selects policy, calls `run_solve<Policy>(...)`
- Same pattern in `benchmark.cpp`

### Commit 5: Update tests

- Initializer-list constructor tests: use default `FlatPolicy` typedef (no change needed)
- `hash_only_cache_test.cpp`: use `game_state_impl<HashOnlyPolicy>`
- `dual_cache_test.cpp`: use `FlatPolicy` (computes everything — matches KI-15 behavior)
- Integration tests via `test_helper`: use default typedef

### Commit 6: Remove conditional compilation

- Remove `SOLVITAIRE_COMPUTES_FLAT_HASH` macro
- Remove `SOLVITAIRE_HASH_ONLY`, `SOLVITAIRE_LRU_ONLY`, `SOLVITAIRE_FLAT_ONLY` compile definitions
- Variant binaries become simple: each `main.cpp` (or a wrapper) hardcodes the policy typedef
- Remove `needs_flat_hash()` / `needs_flat_payload()` from `cache_interface.h`
- Simplify `cache_factory.h` — policy already encodes cache selection
- Update `CMakeLists.txt` — remove compile definitions from variant targets

### Commit 7: Final verification + docs

- Full Level 1-3 regression against pre-refactoring oracles (must match)
- Container build + test for Linux
- Update `docs/known-issues.md` — close KI-8
- Update `docs/proposals/PROPOSAL-templated-game-state-dispatch.md` — mark as implemented
- Update CLAUDE.md architecture section

---

## Key Files Modified

| File | Commits |
|------|---------|
| `src/main/game/cache_policy.h` (new) | 1 |
| `src/main/game/compact_state.h` | 2 |
| `src/main/game/hash_descriptor_store.h` | 2 |
| `src/main/game/search-state/game_state.h` | 3, 6 |
| `src/main/game/search-state/game_state.cpp` | 3, 6 |
| `src/main/game/search-state/game_state.legal_moves.cpp` | 3 |
| `src/main/game/search-state/game_state.dominance_moves.cpp` | 3 |
| `src/main/game/search-state/game_state.pile_order.cpp` | 3 |
| `src/main/solver/solver.h` | 4 |
| `src/main/solver/solver.cpp` | 4, 6 |
| `src/main/main.cpp` | 4 |
| `src/main/evaluation/benchmark.cpp` | 4 |
| `src/main/game/cache_factory.h` | 6 |
| `src/main/game/cache_interface.h` | 6 |
| `CMakeLists.txt` | 6 |
| Test files | 5 |

## Files NOT Modified (no hash/payload references)
- `game_state.legal_moves.cpp` (only instantiation declarations added)
- `game_state.dominance_moves.cpp` (only instantiation declarations added)
- `game_state.pile_order.cpp` (only instantiation declarations added)

---

## Verification

Each commit must pass:
- Build: `./build.sh --release --unit-tests --variants`
- Unit tests: `ctest -R unit_tests --output-on-failure`
- Level 1 regression: `ctest -R regression_level1 --output-on-failure`

Final verification (Commit 7):
- Level 1-3 regression against pre-refactoring oracles — node counts must be identical
- Container build: `./scripts/container-build.sh --test --variants`
