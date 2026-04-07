# Prompt for Gemini: Implement Milestone 4 — Wire Solver to New Cache

## Context

You previously implemented Milestone 3 (flat_cache). Now you need to **wire the flat_cache into the solver** so it's actually used for supported game types.

**Read these files before starting:**
1. `docs/cache-redesign/milestone4_detailed_plan.md` — **Your primary reference. Follow it precisely.**
2. `CLAUDE.md` — Build commands, testing instructions
3. `src/main/solver/solver.h` — Current solver header (note the LRU coupling)
4. `src/main/solver/solver.cpp` — Current DFS loop (note `dynamic_cast`, `insert_with_iterator`, `set_non_live`)

## Summary of What You're Doing

The solver currently hard-codes `lru_cache` usage via `dynamic_cast` and LRU-specific iterator APIs. You need to:

1. **Add a `bool using_flat_cache` flag** to the solver, set via `dynamic_cast<flat_cache*>` check in constructor
2. **Branch the DFS insert logic** — flat_cache uses `cache.insert(state)` (returns bool); lru_cache keeps existing `insert_with_iterator` + iterator storage
3. **Skip live-bit logic for flat_cache** — don't store iterators, don't pass them to `revert_to_last_node_with_children()`. The existing default parameter (`= boost::none`) means **no changes needed to `revert_to_last_node_with_children()`** — just don't pass it an iterator when using flat_cache.
4. **Set depth in payload** before flat_cache insertion — add `game_state::set_payload_depth(uint16_t)` method
5. **Cache construction sites** — at 4 call sites (`main.cpp`, `solvability_calc.cpp`, `benchmark.cpp` ×2), use `use_new_cache(rules)` to choose `flat_cache` vs `lru_cache` via `std::unique_ptr<cache_interface>`
6. **Unit tests** — verify both cache types produce same solvability outcomes

## Files to Modify

| File | Change |
|---|---|
| `src/main/solver/solver.h` | Add `bool using_flat_cache` private member |
| `src/main/solver/solver.cpp` | Add include, set flag in constructor, branch DFS insert logic |
| `src/main/game/search-state/game_state.h` | Add `void set_payload_depth(uint16_t)` declaration |
| `src/main/game/search-state/game_state.cpp` | Add `set_payload_depth` implementation |
| `src/main/main.cpp` | Cache factory with `unique_ptr` in `solve_game()` |
| `src/main/evaluation/solvability_calc.cpp` | Cache factory with `unique_ptr` in `solve_seed()` |
| `src/main/evaluation/benchmark.cpp` | Cache factory with `unique_ptr` at both call sites |
| `CMakeLists.txt` | Add new test file to `sources_test` |

## New File to Create

| File | Purpose |
|---|---|
| `src/test/unit_tests/solver_cache_selection_test.cpp` | 3 tests verifying cache selection and solver correctness |

## Key Technical Details

- `use_new_cache(rules)` already exists in `cache_interface.h` — returns true for single-deck, no sequences, no accordion, no spider-type stock dealing
- `flat_cache` constructor takes `uint64_t max_entries` (not a `game_state` reference like `lru_cache`)
- `lru_cache` constructor takes `(const game_state&, uint64_t)` — it needs the game_state for its hasher
- Cap depth at `UINT16_MAX` before setting: `static_cast<uint16_t>(std::min(res.depth, (uint64_t)UINT16_MAX))`
- The `compact_state::matches()` comparison ignores depth (bytes 1-2), so setting depth won't affect duplicate detection
- Keep the `try/catch (std::runtime_error)` around the cache insert — it catches MEM_LIMIT from lru_cache. For flat_cache this won't throw, but keeping it is harmless and simpler than branching.

## How to Build and Test

```bash
# Build everything including unit tests
./build.sh --release --unit-tests

# Run all unit tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure

# Run Level 1 regression (critical — outcomes must match oracle)
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

## What NOT to Do

- Do NOT modify `flat_cache.h` or `flat_cache.cpp`
- Do NOT modify `revert_to_last_node_with_children()` — the existing default parameter handles it
- Do NOT remove the `lru_cache` code — it's still needed for Spider, Accordion, two-deck games
- Do NOT remove pile ordering — that's Milestone 6
- Do NOT change any existing test files
- Do NOT add features beyond what's specified

## Completion Criteria

1. All existing tests pass (unit + integration + Level 1 regression)
2. New solver_cache_selection tests pass
3. FreeCell/Klondike/BlackHole use `flat_cache`; Spider and spider-deal games use `lru_cache`
4. Solvability outcomes match baseline (Level 1 oracle)
5. Code compiles cleanly with `-Wall -Wextra -Werror`
6. Commit your work with descriptive commit messages

When done, summarise what you implemented, confirm all tests pass, and note any solvability differences observed.
