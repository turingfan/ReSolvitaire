# Commit 3b Prompt: Template solver + cache layer — zero-overhead dispatch

## Context

You are on branch `feature/templated-dispatch-wip-commit3a` (HEAD `eca03d2`).
Commit 3a converted `game_state` to `game_state_impl<Policy>` template with four
policies (FlatPolicy, HashOnlyPolicy, PredecessorPolicy, LRUPolicy). A typedef
`game_state = game_state_impl<FlatPolicy>` preserves the API for all callers.
The solver, caches, and dispatch code were NOT changed in 3a.

**Your job:** Template the solver and cache layer so the DFS hot path has zero
virtual dispatch. Add a dispatch switch in `main.cpp`, `solvability_calc.cpp`,
and `benchmark.cpp`. After 3b, `cache_interface` is only used by test
infrastructure (dual_cache parity tests) — never in the solver.

C++ standard is **17** (set in CMakeLists.txt during 3a). Use `if constexpr`
freely.

## Design decisions (already made — do not revisit)

1. **Option B2:** Solver holds `typename Policy::cache_type&` directly, not
   `cache_interface&`. Zero virtual dispatch in DFS.

2. **Policy::cache_type mapping:**
   - `FlatPolicy::cache_type = generic_flat_cache<CompactStatePolicy>`
   - `HashOnlyPolicy::cache_type = generic_flat_cache<HashOnlyClusterPolicy>`
   - `PredecessorPolicy::cache_type = generic_flat_cache<PredecessorClusterPolicy>`
   - `LRUPolicy::cache_type = lru_cache`

3. **Option C (return type):** `solve_game_impl<Policy>` does all reporting
   (JSON/CSV/verbose) inside the dispatch branch and returns a non-template
   result struct. The templated solver never escapes the dispatch branch.

4. **`cache_interface` kept** for dual_cache test infrastructure. Not deleted.

5. **`make_cache` kept** in `cache_factory.h` for backward compat (tests, dual_cache).

6. **`solver::result`** can stay nested in the solver template (identical across
   all instantiations). The `using solver = solver_impl<FlatPolicy>` typedef
   ensures `solver::result` works everywhere. Alternatively, extract to a
   standalone `solver_result` struct if that's cleaner — your call.

7. **`node::cache_state`** (`boost::optional<lru_cache::item_list::iterator>`)
   stays as-is in all instantiations. Defer conditional inclusion / EBO.

## Files to change (with specific instructions)

### 1. `src/main/game/cache_policy.h`

Add `cache_type` typedef to each policy. Use forward declarations (not includes)
for the cache classes:

```cpp
// Forward declarations
template <typename P> class generic_flat_cache;
struct CompactStatePolicy;
struct HashOnlyClusterPolicy;
struct PredecessorClusterPolicy;
class lru_cache;

struct FlatPolicy {
    // ... existing traits ...
    typedef generic_flat_cache<CompactStatePolicy> cache_type;
};
// Same pattern for HashOnlyPolicy, PredecessorPolicy, LRUPolicy
```

Note: `CompactStatePolicy` is guarded by `#ifndef SOLVITAIRE_HASH_ONLY` in
`generic_flat_cache_policies.h`. The forward declaration here is fine because
`FlatPolicy::cache_type` is never instantiated in a HASH_ONLY build. Guard the
forward declaration of `CompactStatePolicy` and the `FlatPolicy` struct the same
way if needed for consistency — but the existing `FlatPolicy` in this file is
NOT currently guarded. Check whether it needs to be.

### 2. `src/main/game/generic_flat_cache_policies.h`

Template `hash_of` and `payload_of` on GS type (the game state). Bodies are
unchanged — they just call `gs.get_zobrist_hash()` etc. which exist on all
`game_state_impl<Policy>` instantiations that compute hashes.

```cpp
// BEFORE: static uint64_t hash_of(const game_state& gs)
// AFTER:
template <typename GS>
static uint64_t hash_of(const GS& gs) { return gs.get_zobrist_hash(); }
```

Same for `payload_of` on CompactStatePolicy, HashOnlyClusterPolicy,
PredecessorClusterPolicy (with `get_payload()`, `get_predecessor_zobrist_hash()`,
`get_predecessor_payload()`).

### 3. `src/main/game/generic_flat_cache.h`

The current `insert(const game_state&)` and `contains(const game_state&)` are
virtual overrides. After templating the policy methods, these need template
versions for the solver to call with `game_state_impl<HashOnlyPolicy>` etc.

Pattern: move the body into a template method, keep the virtual override as a
thin wrapper:

```cpp
// Template version — solver calls this directly (no virtual dispatch)
template <typename GS>
bool insert(const GS& gs) {
    // ... existing body (unchanged, calls Policy::hash_of(gs) etc.) ...
}

// Virtual override for cache_interface compatibility (dual_cache tests)
bool insert(const game_state& gs) override {
    return this->template insert<game_state>(gs);
}
```

Same pattern for `contains`.

Overload resolution: when called with `game_state` (= `game_state_impl<FlatPolicy>`),
the non-template override is preferred (exact match, non-template wins).
When called with a different `game_state_impl<Policy>`, only the template matches.
Both are correct.

### 4. `src/main/game/global_cache.h` and `global_cache.cpp`

**`hasher`:** Currently stores `const game_state& init_gs`. Change to store the
fields it actually uses (from `init_gs.rules` and `init_gs.stream_opts`):
- `sol_rules::build_policy build_pol`
- `bool foundations_present`
- `bool hole` (whether rules.hole is set)
- `bool suit_symmetry` (derived from stream_opts)

Then the hasher constructor can be templated or changed to take these fields.
The `hash_value(card)` and `operator()` methods use only these stored fields.

**`cached_game_state`:** Template the constructor and `add_pile`/`add_pile_in_reverse`/
`add_card` on GS type. These access `gs.rules`, `gs.piles`, `gs.cells`,
`gs.stream_opts` etc. — all present on any `game_state_impl<Policy>`.
`game_state_impl` already has `friend struct cached_game_state;`.

Move template method bodies to the header (or use explicit instantiation in
the .cpp). Explicit instantiation in .cpp is cleaner:
```cpp
template cached_game_state::cached_game_state(const game_state_impl<LRUPolicy>&);
template cached_game_state::cached_game_state(const game_state_impl<FlatPolicy>&);
// (FlatPolicy needed for make_cache/dual_cache test path)
```

**`lru_cache`:** Template `insert`, `insert_with_iterator`, `contains`,
`get_diagnostic_info`, and the constructor on GS type. Keep virtual overrides
as thin wrappers (same pattern as generic_flat_cache). `get_init_tuple` is
private and only called from the constructor — template it too.

### 5. `src/main/solver/solver.h`

Template the solver class on Policy:

```cpp
template <typename Policy>
class solver_impl {
public:
    typename Policy::cache_type& cache;

    struct node { /* unchanged */ };
    struct result { /* unchanged */ };

    explicit solver_impl(const game_state_impl<Policy>&, typename Policy::cache_type&);
    result run(boost::optional<std::chrono::milliseconds> = boost::none);
    void print_solution() const;
    // static printing methods stay (they don't depend on Policy)

    const game_state_impl<Policy> init_state;

private:
    // DELETE: bool using_flat_cache;
    game_state_impl<Policy> state;
    std::vector<node> frontier;
    result res;
    node root;
    typename std::vector<node>::iterator current_node;
};

// Backward-compatible typedef
using solver = solver_impl<FlatPolicy>;
```

The `print_header`, `print_result_csv`, `print_null_seed_info` static methods
don't depend on Policy. They can stay in the template (accessed via `solver::`
typedef) or be extracted as free functions.

Remove `#include "../game/cache_interface.h"` — the solver no longer uses it.
Keep `#include "../game/global_cache.h"` (needed for `lru_cache::item_list::iterator`
in node).

### 6. `src/main/solver/solver.cpp`

Template all methods. Key changes:

**Constructor:** Delete the entire `dynamic_cast` chain (lines 74-86).
Delete `using_flat_cache` member. The policy traits replace it.

```cpp
template <typename Policy>
solver_impl<Policy>::solver_impl(const game_state_impl<Policy>& gs,
                                  typename Policy::cache_type& c)
    : cache(c), init_state(gs), state(gs), /* ... */ {
    // No dynamic_cast chain — Policy determines everything at compile time
}
```

**`dfs()`:** Replace `if (using_flat_cache)` branches with
`if constexpr (Policy::computes_hash)`:

```cpp
// Lines 144-165 become:
if constexpr (Policy::computes_hash) {
    if constexpr (Policy::computes_payload)
        state.set_payload_depth(static_cast<uint16_t>(...));
    if (state.uses_predecessor_cache())
        state.set_predecessor_payload_depth(...);
    is_new_state = cache.insert(state);  // direct non-virtual call
#ifndef NDEBUG
    if constexpr (Policy::computes_payload)
        state.assert_payload_consistent();
#endif
} else {
    // LRU path
    auto insert_res = cache.insert_with_iterator(state);
    current_node->cache_state = insert_res.first;
    is_new_state = insert_res.second;
}
```

Similarly for the `revert_to_last_node_with_children` branches (~lines 173-177)
and the backtracking function (~lines 220-263).

**`revert_to_last_node_with_children`:** The `dynamic_cast<lru_cache&>(cache)`
becomes just `cache` (already the right type for LRU). Guard the LRU-specific
`set_non_live` call with `if constexpr (!Policy::computes_hash)`.

**`print_solution`:** `game_state state_copy = init_state` becomes
`game_state_impl<Policy> state_copy = init_state`.

**Explicit instantiations** at the bottom:
```cpp
#if defined(SOLVITAIRE_LRU_ONLY)
template class solver_impl<LRUPolicy>;
#elif defined(SOLVITAIRE_FLAT_ONLY)
template class solver_impl<FlatPolicy>;
#elif defined(SOLVITAIRE_HASH_ONLY)
template class solver_impl<HashOnlyPolicy>;
#else
template class solver_impl<FlatPolicy>;
template class solver_impl<HashOnlyPolicy>;
template class solver_impl<PredecessorPolicy>;
template class solver_impl<LRUPolicy>;
#endif
```

**Includes:** Remove conditional `#if !defined(SOLVITAIRE_LRU_ONLY)` guards
around cache includes. The template will only instantiate what's needed. Include
`generic_flat_cache.h` and `global_cache.h` unconditionally (they're needed for
the type declarations even if not all instantiations are compiled).

Actually — `generic_flat_cache.h` includes `generic_flat_cache_policies.h` which
includes `compact_state.h` guarded by `#ifndef SOLVITAIRE_HASH_ONLY`. In the
HASH_ONLY build, `CompactStatePolicy` doesn't exist, and
`generic_flat_cache<CompactStatePolicy>` can't be instantiated. This is fine
because only `solver_impl<HashOnlyPolicy>` is instantiated in that build.

### 7. `src/main/main.cpp`

**Dispatch switch.** Replace the inner `solve_game` function (that returns
`pair<solver, solver::result>`) with a template `solve_game_impl<Policy>`:

```cpp
struct solve_output {
    solver::result result;
    std::string solution_text;  // empty if not solved (only populated in verbose mode)
    std::string deal_text;      // init_state as string
};

template <typename Policy>
solve_output solve_game_impl(
        const sol_rules& rules, uint64_t timeout, uint64_t cache_capacity,
        game_state_impl<Policy>::streamliner_options str_opts,
        boost::optional<int> seed,
        boost::optional<const Document&> in_doc) {
    game_state_impl<Policy> gs = seed
        ? game_state_impl<Policy>(rules, *seed, str_opts)
        : game_state_impl<Policy>(rules, *in_doc, str_opts);

    // Construct cache — asymmetric: lru_cache needs gs reference, flat caches don't
    typename Policy::cache_type cache = [&]() {
        if constexpr (std::is_same_v<typename Policy::cache_type, lru_cache>)
            return lru_cache(gs, cache_capacity);
        else
            return typename Policy::cache_type(cache_capacity);
    }();

    solver_impl<Policy> sol(gs, cache);
    auto res = sol.run(std::chrono::milliseconds(timeout));

    solve_output out;
    out.result = res;
    // Capture output for later printing
    { std::ostringstream ss; ss << sol.init_state; out.deal_text = ss.str(); }
    if (res.sol_type == solver::result::type::SOLVED) {
        std::ostringstream ss;
        sol.print_solution_to(ss);  // or capture frontier replay
        out.solution_text = ss.str();
    }
    return out;
}
```

**NOTE on `print_solution`:** Currently `print_solution()` prints directly to
`cout`. Either add a `print_solution_to(ostream&)` overload, or capture the
output, or just call `print_solution()` inside the template and set a flag in
`solve_output` so the outer function knows not to print again. Simplest: just
call `sol.print_solution()` directly inside `solve_game_impl` and have the
outer function only print the result stats.

**Dispatch function** that selects the policy:

```cpp
solve_output dispatch_solve(
        const sol_rules& rules, uint64_t timeout, uint64_t cache_capacity,
        game_state::streamliner_options str_opts,
        boost::optional<int> seed,
        boost::optional<const Document&> in_doc) {
    bool suit_sym = str_opts == game_state::streamliner_options::SUIT_SYMMETRY
                 || str_opts == game_state::streamliner_options::BOTH;
    if (use_predecessor_cache(rules)) {
        return solve_game_impl<PredecessorPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc);
    } else if (use_new_cache(rules, suit_sym)) {
        return solve_game_impl<FlatPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc);
    } else {
        return solve_game_impl<LRUPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc);
    }
}
```

**HashOnlyPolicy dispatch:** Currently `--cache-type hash-only` selects hash-only.
Add that branch to the dispatch:
```cpp
    if (cache_type == "hash-only" && use_new_cache(rules, suit_sym)) {
        return solve_game_impl<HashOnlyPolicy>(...);
    }
```

The outer `solve_game(rules, clh, seed, in_doc, instance_name)` calls
`dispatch_solve` instead of the old `solve_game(rules, timeout, ...)`. It handles
the "smart" retry logic and all output formatting using `solve_output`.

**Remove** `force_lru` and `cache_type` parameters from the inner solve function
(they're subsumed by the policy dispatch). The outer function reads them from
`clh` and uses them in the dispatch switch. If `force_lru` is set, dispatch to
LRUPolicy.

### 8. `src/main/evaluation/solvability_calc.h` and `.cpp`

Template `solve_seed` on Policy. Add dispatch switch at the call site (inside
`solver_thread`). Pattern is the same as main.cpp's `dispatch_solve`.

Remove `make_cache` usage — construct `Policy::cache_type` directly.

Keep `solver::result` references (they work through the typedef).

### 9. `src/main/evaluation/benchmark.h` and `.cpp`

Same dispatch pattern in `run` and `run_json`. Template the inner solve loop
on Policy. The outer functions do the dispatch.

`run_json` is more complex (reads game type from JSON). Each instance might
use a different game type / rules, so the dispatch happens per-instance inside
the loop.

## Files that do NOT change

- `cache_interface.h` — kept as-is
- `cache_factory.h` — kept for backward compat
- `dual_cache.h` — uses cache_interface, works for FlatPolicy tests
- `compact_state.h/cpp` — no change
- `hash_descriptor_store.h` — no change
- `game_state.h/cpp` and split .cpp files — already converted in 3a
- `flat_cache.h/cpp`, `hash_only_cache.h/cpp`, `predecessor_flat_cache.h/cpp` —
  old caches kept for dual_cache parity tests, no changes needed (they still
  override cache_interface with `const game_state&`)
- Unit test .cpp files — use `game_state` typedef, compile without changes
- `CMakeLists.txt` — no changes needed

## Key pitfalls to watch for

1. **`boost::optional` vs `std::optional` ambiguity in main.cpp:** Already fixed
   in 3a — all bare `optional` in main.cpp are qualified as `boost::optional`.
   Maintain this in any new code you add.

2. **`#ifndef SOLVITAIRE_HASH_ONLY` guards:** `CompactStatePolicy` and
   `compact_state` don't exist in HASH_ONLY builds. Make sure FlatPolicy and
   PredecessorPolicy code paths aren't compiled in HASH_ONLY. The explicit
   instantiation blocks handle this.

3. **Template definitions in .cpp files:** Methods defined in .cpp need explicit
   instantiation. Don't forget the operator<< for solver::result (it's a free
   function, not template-dependent — leave it alone).

4. **`lru_cache` constructor asymmetry:** `lru_cache(gs, capacity)` needs the
   game state for the hasher. `generic_flat_cache(capacity)` does not. Handle
   this in the dispatch/construction code.

5. **`solver.cpp` preprocessor guards:** The current code has `#if SOLVITAIRE_COMPUTES_FLAT_HASH`
   guards inside `dfs()` (lines 145-158). These are the pre-3a preprocessor
   guards that coexist with the `using_flat_cache` runtime flag. In your template
   conversion, REPLACE BOTH with `if constexpr (Policy::computes_hash)` and
   `if constexpr (Policy::computes_payload)`. Delete the preprocessor guards
   entirely.

6. **`state.computing_flat_payload`** (solver.cpp:146, 157): This is a pre-3a
   runtime flag that was removed from game_state in 3a. Replace with
   `if constexpr (Policy::computes_payload)`.

## Build and test

```bash
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure
cd cmake-build-release && ctest -R regression_level1 --output-on-failure
```

### Known temporary regressions from 3a

9 unit tests and 44/150 Level 1 instances fail because `skip_pile_ordering` is
a compile-time policy trait. In the default binary, `game_state = game_state_impl<FlatPolicy>`
with `skip_pile_ordering = true`, but some games need LRU with pile ordering.
**3b's dispatch switch fixes this** — those games dispatch to `LRUPolicy` which
has `skip_pile_ordering = false`. After 3b, ALL tests should pass.

### Success criteria

1. All unit tests pass (including the 9 that currently fail)
2. Level 1 regression passes (all 150 instances, including the 44 that currently fail)
3. `cache_interface` is NOT referenced from solver.h or solver.cpp
4. No `dynamic_cast` in solver.cpp
5. No `using_flat_cache` in solver.cpp

## Escape hatch

If a file outside the listed set absolutely must change to make things work,
explain WHY and make the minimal change. Do not silently modify unlisted files.
If you're stuck or a constraint in this prompt seems wrong, say so and explain
the conflict rather than burning tokens going back and forth.
