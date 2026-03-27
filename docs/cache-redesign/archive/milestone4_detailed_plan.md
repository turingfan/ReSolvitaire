# Milestone 4: Wire Solver to New Cache — Detailed Plan

**Status:** Ready to implement
**Prerequisite:** Milestone 3 (complete) — `flat_cache` exists and passes unit tests, but is not connected to the solver.
**Branch:** `refactor-caching`

---

## Goal

Connect the `flat_cache` to the solver for supported game types. This is the first milestone where solver **behaviour changes** — games that qualify for the new cache will use `flat_cache` instead of `lru_cache`.

The existing `lru_cache` uses a "live-bit" mechanism: the solver stores iterators into the cache and marks states as non-live when backtracking. The `flat_cache` has no equivalent mechanism — it uses simple `insert()`/`contains()` without iterators. The main challenge is making the solver work with both cache types.

## Architecture Context

### Current Solver–Cache Coupling (What Must Change)

The solver (`solver.h/cpp`) is tightly coupled to `lru_cache` in four ways:

1. **`node::cache_state`** (solver.h line 45):
   ```cpp
   boost::optional<lru_cache::item_list::iterator> cache_state;
   ```
   Every search node stores an iterator into the LRU cache.

2. **`dfs()` insert call** (solver.cpp lines 122-124):
   ```cpp
   auto& lru_cache_ref = dynamic_cast<lru_cache&>(cache);
   pair<lru_cache::item_list::iterator, bool> insert_res = lru_cache_ref.insert_with_iterator(state);
   current_node->cache_state = insert_res.first;
   ```
   Uses `dynamic_cast` and LRU-specific `insert_with_iterator()`.

3. **`revert_to_last_node_with_children()`** (solver.cpp lines 176-184):
   ```cpp
   bool solver::revert_to_last_node_with_children(optional<lru_cache::item_list::iterator> cur_state) {
       if (cur_state) {
           auto& lru_cache_ref = dynamic_cast<lru_cache&>(cache);
           lru_cache_ref.set_non_live(*cur_state);
       }
       ...
       optional<lru_cache::item_list::iterator> p_state = prev(current_node)->cache_state;
       ...
       return revert_to_last_node_with_children(p_state);
   }
   ```
   Uses `dynamic_cast`, `set_non_live()`, and passes iterators during recursive backtracking.

4. **Cache construction sites** (3 locations):
   - `main.cpp` line 255: `lru_cache cache(gs, cache_capacity);`
   - `solvability_calc.cpp` line 175: `lru_cache cache(gs, cache_capacity);`
   - `benchmark.cpp` lines 72 and 258: `lru_cache cache(gs, cache_capacity);`

### What Stays the Same

- `cache_interface&` reference in solver — this is already correct
- `cache_interface::insert()` returns `bool` — works for both cache types
- `cache_interface::contains()`, `clear()`, `size()`, etc. — all polymorphic
- All game logic, move generation, dominance moves — untouched

---

## Task 4.1: Add `using_flat_cache` Flag to Solver

Add a `bool using_flat_cache` private member to `solver`. Set it in the constructor based on the cache type:

```cpp
// In solver constructor, after existing initialisation:
using_flat_cache = (dynamic_cast<flat_cache*>(&cache) != nullptr);
```

This flag determines which code path to use in `dfs()` and `revert_to_last_node_with_children()`.

**Required include:** Add `#include "../game/flat_cache.h"` to `solver.cpp`.

---

## Task 4.2: Branch the DFS Insert Logic

In `solver::dfs()`, replace the current LRU-specific insert block (lines 120-142) with a branching structure:

```cpp
// Caches the current state
bool is_new_state;

if (using_flat_cache) {
    // flat_cache path: simple insert, no iterator, no live bits
    is_new_state = cache.insert(state);
} else {
    // lru_cache path: insert with iterator for live-bit tracking
    auto& lru_cache_ref = dynamic_cast<lru_cache&>(cache);
    auto insert_res = lru_cache_ref.insert_with_iterator(state);
    current_node->cache_state = insert_res.first;
    is_new_state = insert_res.second;
}

if (is_new_state) {
    vector<move> next_moves = state.get_legal_moves(current_node->mv);
    if (next_moves.empty()) {
        if (using_flat_cache) {
            states_exhausted = revert_to_last_node_with_children();
        } else {
            states_exhausted = revert_to_last_node_with_children(
                current_node->cache_state);
        }
    } else {
        current_node->child_moves = std::move(next_moves);
    }
} else {
    res.unique_states_searched--;
    states_exhausted = revert_to_last_node_with_children();
}
```

**Key difference:** When `using_flat_cache` is true:
- Use `cache.insert(state)` (polymorphic, returns bool)
- Do NOT store any iterator in `cache_state`
- Do NOT pass iterator to `revert_to_last_node_with_children` (always pass no argument)

When `using_flat_cache` is false:
- Keep existing behaviour exactly as-is

---

## Task 4.3: Modify `revert_to_last_node_with_children()`

The existing function signature takes `optional<lru_cache::item_list::iterator>`. Keep this unchanged — when `using_flat_cache` is true, it will always be called with `boost::none` (the default), so the `set_non_live` block is simply skipped.

The recursive call at line 207 passes `prev(current_node)->cache_state`. When using flat_cache, `cache_state` is always `boost::none`, so this naturally works — no changes needed to this function.

**Summary: No changes needed to `revert_to_last_node_with_children()`.** The existing default parameter (`= boost::none`) and the fact that `cache_state` is never populated for flat_cache means the live-bit logic is automatically skipped.

---

## Task 4.4: Set Depth in Payload Before Insertion

When using `flat_cache`, the solver should set the search depth in the game_state's payload before inserting. This enables the TwoBig1 depth-based replacement policy.

Add a method to `game_state`:
```cpp
void set_payload_depth(uint16_t depth);
```

Implementation in `game_state.cpp`:
```cpp
void game_state::set_payload_depth(uint16_t depth) {
    payload.set_depth(depth);
}
```

Then in the flat_cache insert path in `dfs()`:
```cpp
if (using_flat_cache) {
    state.set_payload_depth(static_cast<uint16_t>(
        std::min(res.depth, static_cast<uint64_t>(UINT16_MAX))));
    is_new_state = cache.insert(state);
}
```

**Important:** Cap at `UINT16_MAX` (65535) since depth is stored as 16-bit in compact_state.

**Note:** The depth field is excluded from `compact_state::matches()` comparisons (it only compares bytes 3-31, depth is bytes 1-2), so setting depth does NOT affect duplicate detection. A state inserted at depth 10 will correctly match the same state encountered at depth 20.

---

## Task 4.5: Cache Construction at Call Sites

At each site where `lru_cache` is currently constructed, add a branch using `use_new_cache(rules)`:

### main.cpp (`solve_game` function, line 251-259):

```cpp
pair<solver, solver::result> solve_game(const sol_rules& rules, uint64_t timeout, uint64_t cache_capacity,
                                        game_state::streamliner_options str_opts,
                                        optional<int> seed, optional<const Document&> in_doc) {
    game_state gs = seed ? game_state(rules, *seed, str_opts) : game_state(rules, *in_doc, str_opts);

    // Use unique_ptr for polymorphic ownership
    std::unique_ptr<cache_interface> cache_ptr;
    if (use_new_cache(rules)) {
        cache_ptr = std::make_unique<flat_cache>(cache_capacity);
    } else {
        cache_ptr = std::make_unique<lru_cache>(gs, cache_capacity);
    }

    solver sol(gs, *cache_ptr);
    solver::result res = sol.run(std::chrono::milliseconds(timeout));
    return make_pair(sol, res);
}
```

**Required includes in main.cpp:** Add `#include "game/flat_cache.h"` and `<memory>`.

### solvability_calc.cpp (`solve_seed` function, line 171-179):

```cpp
solvability_calc::seed_result solvability_calc::solve_seed(int seed, millisec timeout, const sol_rules& rules,
                                                          uint64_t cache_capacity,
                                                          game_state::streamliner_options stream_opt) {
    game_state gs(rules, seed, stream_opt);

    std::unique_ptr<cache_interface> cache_ptr;
    if (use_new_cache(rules)) {
        cache_ptr = std::make_unique<flat_cache>(cache_capacity);
    } else {
        cache_ptr = std::make_unique<lru_cache>(gs, cache_capacity);
    }

    solver sol(gs, *cache_ptr);
    return seed_result(seed, sol.run(boost::optional<std::chrono::milliseconds>(timeout)));
}
```

**Required includes in solvability_calc.cpp:** Add `#include "../game/flat_cache.h"` and `<memory>`.

### benchmark.cpp (two call sites, lines ~72 and ~258):

Apply the same pattern at both `lru_cache cache(gs, cache_capacity)` sites. The include for `flat_cache.h` should already be straightforward to add.

**Required includes in benchmark.cpp:** Add `#include "../game/flat_cache.h"` and `<memory>`.

---

## Task 4.6: Unit Tests

Add tests in a new file `src/test/unit_tests/solver_cache_selection_test.cpp`:

### Test 1: FreeCellUsesNewCache
Create a FreeCell game (single-deck, no sequences, no accordion). Run the solver with a `flat_cache`. Verify it solves correctly (seed 1 is solvable for FreeCell).

### Test 2: SolverWithFlatCacheProducesSameOutcome
For seeds 1-10 with FreeCell rules, solve using both `lru_cache` and `flat_cache`. Verify the `sol_type` (SOLVED/UNSOLVABLE) matches for each seed. States-searched counts may differ.

### Test 3: UseCacheSelectionFunction
Verify `use_new_cache()` returns true for `free-cell`, `klondike`, `black-hole`, `spanish-patience` and false for `spider` (two-deck).

Add the new test file to `sources_test` in `CMakeLists.txt`.

---

## Task 4.7: Build and Verify

1. Build: `./build.sh --release --unit-tests`
2. Run unit tests: `cd cmake-build-release && ctest -R unit_tests --output-on-failure`
3. Run Level 1 regression: `cd cmake-build-release && ctest -R regression_level1 --output-on-failure`
4. All existing tests must still pass
5. All new tests must pass
6. **Critical:** Level 1 regression solvability outcomes must match oracle exactly

---

## Review Criteria

- [ ] `flat_cache` used for single-deck, no-sequence, no-accordion, no-spider-deal games
- [ ] `lru_cache` used for all other games (Spider, Accordion, two-deck, spider-type dealing)
- [ ] Solver DFS correctly branches between flat_cache and lru_cache code paths
- [ ] Live-bit logic (iterator storage, `set_non_live`) skipped for flat_cache
- [ ] Depth set in payload before flat_cache insertion
- [ ] Depth capped at UINT16_MAX
- [ ] All existing tests pass unchanged
- [ ] Level 1 regression outcomes match oracle
- [ ] No memory leaks or sanitizer errors
- [ ] Code compiles with `-Wall -Wextra -Werror`

---

## What NOT To Do

- Do NOT remove the `lru_cache` or `cached_game_state` code — it's still needed for multi-deck/sequence/accordion games
- Do NOT modify `flat_cache.h` or `flat_cache.cpp` (Milestone 3 code)
- Do NOT remove pile ordering — that's Milestone 6
- Do NOT add depth safety bound yet — defer to Milestone 5
- Do NOT change any existing test files
