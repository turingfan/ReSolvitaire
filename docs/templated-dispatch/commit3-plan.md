# Commit 3 Plan: "The Big One" — game_state_impl<Policy> Conversion

**Date:** 2026-04-28  
**Branch:** `feature/templated-dispatch`  
**Prerequisite:** Commits 0, 1, 2 complete

---

## Overview

Commit 3 converts `game_state` from a single class with two runtime boolean flags and
two preprocessor guards into a template `game_state_impl<Policy>` where Policy is one
of the four tag types from `cache_policy.h`. The runtime flags and all `#ifdef` guards
disappear from game_state entirely. A `game_state` typedef at the bottom of the header
preserves the existing API for all callers that do not care about policy.

The `solver` class must also be templated, and `solve_game()` in `main.cpp` gains a
one-time dispatch switch that selects the concrete Policy and constructs the matching
`game_state_impl<Policy>` + `solver<Policy>` before the DFS loop starts.

This is the commit that eliminates all dead work from the DFS hot path for LRU games
and hash-only games.

---

## Invariants to Preserve

- All four variant binaries (`solvitaire`, `solvitaire-flat`, `solvitaire-hash-only`,
  `solvitaire-lru`) must build cleanly.
- All Level 1-3 regression tests must pass across all four cache variants.
- Unit tests must pass.
- Node-count oracles (added in Commit 0) must be respected: node counts must match.
- External callers (`solver.cpp`, `main.cpp`, unit tests) use `game_state` as a name
  and must continue to compile without changes to their source.

---

## Files That Change

| File | Change |
|---|---|
| `src/main/game/search-state/game_state.h` | Full template conversion |
| `src/main/game/search-state/game_state.cpp` | Full template conversion + explicit instantiations |
| `src/main/game/search-state/game_state.legal_moves.cpp` | Add explicit instantiations |
| `src/main/game/search-state/game_state.dominance_moves.cpp` | Add explicit instantiations |
| `src/main/game/search-state/game_state.pile_order.cpp` | Add explicit instantiations |
| `src/main/game/cache_policy.h` | Add `skip_pile_ordering` trait (see §Open Questions) |
| `src/main/solver/solver.h` | Template on Policy |
| `src/main/solver/solver.cpp` | Template + explicit instantiations |
| `src/main/main.cpp` | Replace `solve_game()` with Policy-dispatching version |

## Files That Do NOT Change

- `compact_state.h/.cpp` — Commit 2 confirmed the API is already correct.
- `hash_descriptor_store.h` — Same.
- `cache_policy.h` — Modulo `skip_pile_ordering` addition (see §Open Questions).
- `flat_cache.*`, `hash_only_cache.*`, `predecessor_flat_cache.*`, `global_cache.*` — No change.
- `cache_factory.h` — No change (still returns `cache_interface*` polymorphically).
- `game_state.legal_moves.cpp`, `.dominance_moves.cpp`, `.pile_order.cpp` bodies — No logic change, only explicit instantiation declarations added.
- Unit tests — should compile without change via `game_state` typedef.

---

## Three Transformation Patterns in game_state.cpp

Every preprocessor conditional in `game_state.cpp` falls into one of three
patterns. The implementer must apply the correct pattern to each site.

### Pattern A — Whole-block compile guard

```cpp
// BEFORE
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    <hash/payload work>
#else
    (void)param;  // silence unused-param warnings
#endif

// AFTER
if constexpr (Policy::computes_hash) {
    <hash/payload work>
} else {
    (void)param;
}
```

Occurs in: constructors (`init_payload_and_hash` call), `make_built_group_move`,
`undo_built_group_move`, `make_stock_to_all_tableau_move`,
`undo_stock_to_all_tableau_move`, `make_regular_move`, `undo_regular_move`,
`check_face_down_consistent`, `update_card_descriptor`,
`update_foundation_in_hash`, `update_waste_ptr_in_hash`,
`update_hole_top_in_hash`, `set_payload_depth`, `compute_hash_from_scratch`,
`determine_destination_descriptor`.

### Pattern B — Member-name selector

```cpp
// BEFORE
#ifdef SOLVITAIRE_HASH_ONLY
    hash_desc.get_descriptor(cid);
    hash_desc.set_descriptor(cid, val);
    // ... (other desc_store calls)
#else
    payload.get_descriptor(cid);
    payload.set_descriptor(cid, val);
#endif

// AFTER
desc_store.get_descriptor(cid);
desc_store.set_descriptor(cid, val);
```

Commit 2 confirmed all five method groups (`clear`, `get/set_descriptor`,
`get/set_foundation`, `get/set_waste_ptr`, `get/set_hole_top`) have identical
signatures on both types. Pattern B is a pure name collapse — no logic change.

Occurs at: `init_initially_face_up` (lines 1141-1145), `init_payload_and_hash`
(lines 1154-1205), `update_card_descriptor` (1265-1270),
`update_foundation_in_hash` (1283-1288), `update_waste_ptr_in_hash` (1310-1315),
`update_hole_top_in_hash` (1328-1333), `make_regular_move` (488-492).

### Pattern C — Runtime flag removal

```cpp
// BEFORE
if (computing_flat_hash) {
    <hash work>
}

// AFTER (inside a Pattern-A block that already established computes_hash == true)
<hash work>   // runtime check removed; always true here
```

The `if (computing_flat_hash)` guards inside update helpers were runtime-branch
protections for the default binary where the flag could be false at runtime (LRU
games). After the template conversion, `game_state_impl<LRUPolicy>` never calls
these helpers at all (the Pattern-A `if constexpr (Policy::computes_hash)` at
the call site handles it). The inner `if (computing_flat_hash)` check is
therefore removed.

Similarly, `if (computing_flat_payload)` guards on `set_payload_depth()` and
`assert_payload_consistent()` become `if constexpr (Policy::computes_payload)`.

**Important:** `init_payload_and_hash()` begins with `if (!computing_flat_hash) return;`.
This guard was needed when the method was called unconditionally in the default binary.
After conversion, the method is only called from within `if constexpr (Policy::computes_hash)`,
so the guard is deleted.

---

## Detailed Changes Per File

### 1. `game_state.h`

#### 1a. Remove the derived macro and conditional includes

Delete the derived `SOLVITAIRE_COMPUTES_FLAT_HASH` macro block (lines 28-36). It
becomes unnecessary: Policy traits replace the macro entirely.

Replace the conditional include:
```cpp
// DELETE:
#ifndef SOLVITAIRE_HASH_ONLY
#  include "../compact_state.h"
#else
#  include "../hash_descriptor_store.h"
#endif

// REPLACE WITH:
#include "../compact_state.h"
#include "../hash_descriptor_store.h"
#include "../cache_policy.h"
```

Both types are always included because the template must compile all four policy
instantiations in the default binary.

#### 1b. Convert the class declaration

```cpp
// BEFORE
class game_state {

// AFTER
template <typename Policy>
class game_state_impl {
```

#### 1c. Public runtime flags → removed

Delete:
```cpp
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    bool computing_flat_hash;
    bool computing_flat_payload;
#endif
```

These become `Policy::computes_hash` and `Policy::computes_payload` (constexpr
traits on the Policy struct). Any external code reading these flags must be updated
to use the policy trait instead.

**Known external reader in solver.cpp (line 146):**
```cpp
if (state.computing_flat_payload)  state.set_payload_depth(...)
```
After conversion this becomes `if constexpr (Policy::computes_payload)` inside
a templated solver (see §Solver changes).

#### 1d. Public accessor methods — conditional on policy

Three methods are currently guarded `#ifndef SOLVITAIRE_HASH_ONLY` and a fourth
`#if SOLVITAIRE_COMPUTES_FLAT_HASH && !defined(NDEBUG) && !defined(SOLVITAIRE_HASH_ONLY)`:

```cpp
const compact_state& get_payload() const;
void set_payload_depth(uint16_t depth);
void compute_hash_from_scratch();
compact_state recompute_payload_from_scratch() const;  // debug
void assert_payload_consistent() const;                // debug
```

These are only valid when `Policy::computes_payload` is true (FlatPolicy,
PredecessorPolicy). Recommended approach: use C++17 `enable_if` to make them
not participate in overload resolution for other policies:

```cpp
template <typename P = Policy,
          typename = std::enable_if_t<P::computes_payload>>
const compact_state& get_payload() const;

template <typename P = Policy,
          typename = std::enable_if_t<P::computes_payload>>
void set_payload_depth(uint16_t depth);
// etc.
```

Alternatively (simpler, acceptable): keep all methods always declared but use
`static_assert(Policy::computes_payload, "...")` in the body. Either approach
works because the `game_state` typedef always resolves to a policy where
`computes_payload` is true in the default binary, so no existing callers break.

`get_zobrist_hash()` can remain always-present (it returns `zobrist_hash_value`
which is always a member; for LRU the value is always 0 / unset).

#### 1e. Private members — descriptor store unification

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

`zobrist_hash_value` and `initially_face_up[52]` remain as always-present members
(present in all four instantiations at the type level). For LRU games, they are
never written; the compiler eliminates them at -O3 under the `if constexpr` guard.
EBO (Empty Base Optimisation) to physically remove them for LRUPolicy is deferred.

#### 1f. `skip_pile_ordering` — see §Open Questions

Currently set in the constructor from `use_new_cache()`. After conversion, it must
be derivable from Policy alone. See §Open Questions Q1 for options.

#### 1g. `init_payload_and_hash` and `init_initially_face_up` declarations

These are `#if SOLVITAIRE_COMPUTES_FLAT_HASH` guarded. After conversion, keep them
as always-declared private methods; they become no-ops or `if constexpr`-guarded
in their bodies for policies where `computes_hash = false`.

#### 1h. Typedef aliases at the bottom of the header

After the class definition, add the `game_state` typedef that preserves the existing
name for all callers:

```cpp
// ─── game_state typedef ───────────────────────────────────────────────────────
// Preserves the game_state name for all callers that don't care about policy.
// Variant binaries select a single policy; the default binary defaults to
// FlatPolicy for code outside the dispatch switch (tests, benchmarks, utilities).
// Inside solve_game(), the dispatch switch constructs the concrete Policy directly.

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

Note: PredecessorPolicy does not get its own variant binary. It is always part of the
default and FLAT_ONLY binaries (selected at dispatch time for accordion games). The
`solvitaire-flat` binary uses FlatPolicy for most games and PredecessorPolicy for
accordion; it needs both instantiated.

---

### 2. `game_state.cpp`

The `.cpp` file is a template body. It needs two structural changes:

**2a. Class-name substitution throughout:** Every `game_state::method_name` becomes
`game_state_impl<Policy>::method_name`. Add `template <typename Policy>` before every
method definition.

**2b. Apply the three patterns** at every `#if SOLVITAIRE_COMPUTES_FLAT_HASH`,
`#ifdef SOLVITAIRE_HASH_ONLY`, and `if (computing_flat_*)` site. Exact sites
enumerated in §Three Transformation Patterns.

**2c. Constructor parameter removal:** Remove `force_lru` and `cache_type` from
constructor signatures (they were only needed to compute the runtime flags which are
now Policy traits). `suit_sym` must be passed or derived — see §Open Questions Q1
on `skip_pile_ordering`.

Note: the initializer-list constructor `game_state(const sol_rules&,
std::initializer_list<...>)` is used by unit tests. Its third parameter is
`streamliner_options`. It calls the private base constructor which sets
`computing_flat_hash/payload`. After conversion this constructor must still work
for test code. Unit tests use `game_state` (typedef → `game_state_impl<FlatPolicy>`)
so they automatically get the Flat policy. The test constructor can call
`init_payload_and_hash()` unconditionally inside an `if constexpr` block.

**2d. `static` member definitions:** `Z_pred[52][110]` and `Z_pred_initialised`
are `static` members. For templates, these need out-of-line definitions with
template syntax:
```cpp
template <typename Policy> uint64_t game_state_impl<Policy>::Z_pred[52][110];
template <typename Policy> bool     game_state_impl<Policy>::Z_pred_initialised = false;
```

**2e. `recompute_payload_from_scratch()` and `assert_payload_consistent()`:**
These are `#ifndef NDEBUG` + `#if SOLVITAIRE_COMPUTES_FLAT_HASH` + 
`!defined(SOLVITAIRE_HASH_ONLY)` guarded. After conversion:
- Guard with `#ifndef NDEBUG` (retain)
- Guard body with `if constexpr (Policy::computes_payload)` or use `enable_if` on
  the declaration (matches §1d approach)
- `recompute_payload_from_scratch()` returns `compact_state`. This only makes sense
  for `computes_payload` policies where `descriptor_store_type = compact_state`.
  The return type works correctly when the method is only enabled for those policies.

**2f. `check_face_down_consistent()` (debug):**
Currently `#if SOLVITAIRE_COMPUTES_FLAT_HASH` guards the inner descriptor check that
uses `payload.get_descriptor()`. After conversion: replace the inner guard with
`if constexpr (Policy::computes_hash)` and replace `payload` with `desc_store`.
Note: this debug method only runs in `NDEBUG`-off builds where FlatPolicy is
almost always the policy in use, so this is low-risk.

**2g. Explicit instantiations at the bottom:**

```cpp
// ─── Explicit instantiations ──────────────────────────────────────────────────
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

---

### 3. Split `.cpp` files (legal_moves, dominance_moves, pile_order)

These three files define methods of `game_state` that touch no hash/payload logic.
They contain no `#if SOLVITAIRE_COMPUTES_FLAT_HASH` guards.

**Only change needed:** Add the same explicit instantiation block at the bottom of
each file (identical to §2g above). Also add `template <typename Policy>` before each
method definition and rename `game_state::` → `game_state_impl<Policy>::`.

No logic changes in any of these files.

---

### 4. `cache_policy.h`

Needs one addition to support `skip_pile_ordering` (see §Open Questions Q1):

```cpp
struct FlatPolicy {
    static constexpr bool computes_hash        = true;
    static constexpr bool computes_payload     = true;
    static constexpr bool skip_pile_ordering   = true;   // ADD
    typedef compact_state descriptor_store_type;
};
struct HashOnlyPolicy {
    static constexpr bool computes_hash        = true;
    static constexpr bool computes_payload     = false;
    static constexpr bool skip_pile_ordering   = true;   // ADD
    typedef hash_descriptor_store descriptor_store_type;
};
struct PredecessorPolicy {
    static constexpr bool computes_hash        = true;
    static constexpr bool computes_payload     = true;
    static constexpr bool skip_pile_ordering   = false;  // ADD (accordion games use LRU pile ordering)
    typedef compact_state descriptor_store_type;
};
struct LRUPolicy {
    static constexpr bool computes_hash        = false;
    static constexpr bool computes_payload     = false;
    static constexpr bool skip_pile_ordering   = false;  // ADD
    struct empty_descriptor_store {};
    typedef empty_descriptor_store descriptor_store_type;
};
```

`skip_pile_ordering` in the constructor becomes:
```cpp
skip_pile_ordering = Policy::skip_pile_ordering;
```

**Ian must confirm** the PredecessorPolicy value (see §Open Questions Q1).

---

### 5. `solver.h`

Convert to template:
```cpp
template <typename Policy>
class solver {
public:
    cache_interface& cache;
    // ... (result type unchanged — not template-dependent)
    explicit solver(const game_state_impl<Policy>&, cache_interface&);
    // ...
    const game_state_impl<Policy> init_state;
private:
    bool using_flat_cache;   // becomes: static constexpr bool using_flat_cache = Policy::computes_hash;
    game_state_impl<Policy> state;
    // ...
};
```

The `node` struct inside `solver` holds
`boost::optional<lru_cache::item_list::iterator> cache_state`. This field is
only used for LRUPolicy. It can remain always-present (acceptable overhead for
other policies); EBO removal is deferred.

The `result` struct is not template-dependent and can remain in the class or be
extracted to a standalone struct if the return-type issue requires it (see §Open
Questions Q2).

---

### 6. `solver.cpp`

**6a. Method prefix:** Every `solver::` → `solver<Policy>::`, add
`template <typename Policy>` before each definition.

**6b. `using_flat_cache` flag:**

```cpp
// BEFORE
using_flat_cache = dynamic_cast<flat_cache*>(&cache) != nullptr || ...;

// AFTER
// using_flat_cache is a static constexpr — or just use if constexpr at call sites
```

The `dynamic_cast` detection was needed when `solver` didn't know the policy. After
templating, `Policy::computes_hash` replaces it. The `bool using_flat_cache` member
can be deleted; `if constexpr (Policy::computes_hash)` is used at each call site.

**6c. Policy-conditional blocks in `dfs()`:**

```cpp
// BEFORE
if (using_flat_cache) {
#if SOLVITAIRE_COMPUTES_FLAT_HASH && !defined(SOLVITAIRE_HASH_ONLY)
    if (state.computing_flat_payload)
        state.set_payload_depth(...);
#endif
    if (state.uses_predecessor_cache())
        state.set_predecessor_payload_depth(...);
    is_new_state = cache.insert(state);
#ifndef NDEBUG
#if SOLVITAIRE_COMPUTES_FLAT_HASH && !defined(SOLVITAIRE_HASH_ONLY)
    if (state.computing_flat_payload) state.assert_payload_consistent();
#endif
#endif
} else {
    auto& lru_cache_ref = dynamic_cast<lru_cache&>(cache);
    ...lru insert with iterator...
}

// AFTER
if constexpr (Policy::computes_hash) {
    if constexpr (Policy::computes_payload)
        state.set_payload_depth(...);
    if (state.uses_predecessor_cache())
        state.set_predecessor_payload_depth(...);
    is_new_state = cache.insert(state);
#ifndef NDEBUG
    if constexpr (Policy::computes_payload) state.assert_payload_consistent();
#endif
} else {
    auto& lru_cache_ref = dynamic_cast<lru_cache&>(cache);
    ...lru insert with iterator...
}
```

**6d. `revert_to_last_node_with_children`:** The LRU cache-state iterator is used
in the LRU branch only. `if constexpr (Policy::computes_hash)` handles the path.

**6e. Explicit instantiations at the bottom** (identical policy set to game_state):
```cpp
#if defined(SOLVITAIRE_LRU_ONLY)
template class solver<LRUPolicy>;
#elif defined(SOLVITAIRE_FLAT_ONLY)
template class solver<FlatPolicy>;
template class solver<PredecessorPolicy>;
#elif defined(SOLVITAIRE_HASH_ONLY)
template class solver<HashOnlyPolicy>;
#else
template class solver<FlatPolicy>;
template class solver<HashOnlyPolicy>;
template class solver<PredecessorPolicy>;
template class solver<LRUPolicy>;
#endif
```

---

### 7. `main.cpp`

#### 7a. The dispatch switch

The current `solve_game()` function constructs a single `game_state` and a single
`solver`. After conversion, it dispatches to a templated helper:

```cpp
template <typename Policy>
static std::pair<solver<Policy>, solver::result>
solve_game_impl(const sol_rules& rules, uint64_t timeout, uint64_t cache_capacity,
                typename game_state_impl<Policy>::streamliner_options str_opts,
                boost::optional<int> seed,
                boost::optional<rapidjson::Document*> in_doc,
                const std::string& cache_type = "auto") {
    game_state_impl<Policy> gs = seed
        ? game_state_impl<Policy>(rules, *seed, str_opts)
        : game_state_impl<Policy>(rules, *in_doc, str_opts);
    std::unique_ptr<cache_interface> cache_ptr = make_cache(rules, gs, cache_capacity, cache_type, ...);
    solver<Policy> sol(gs, *cache_ptr);
    solver::result res = sol.run(std::chrono::milliseconds(timeout));
    return {sol, res};
}
```

The outer `solve_game()` becomes the dispatch switch:

```cpp
// Determine policy once, before DFS starts
if (use_predecessor_cache(rules) && !force_lru)
    return solve_game_impl<PredecessorPolicy>(rules, timeout, ...);
else if (cache_type == "hash-only" && use_new_cache(rules, suit_sym) && !force_lru)
    return solve_game_impl<HashOnlyPolicy>(rules, timeout, ...);
else if (use_new_cache(rules, suit_sym) && !force_lru)
    return solve_game_impl<FlatPolicy>(rules, timeout, ...);
else
    return solve_game_impl<LRUPolicy>(rules, timeout, ...);
```

#### 7b. Return-type issue — see §Open Questions Q2

The current `solve_game()` returns `pair<solver, solver::result>`. After conversion,
the solver is `solver<Policy>` (four distinct concrete types). The caller in `main()`
uses `solution.first.print_solution()` and `solution.first.init_state`. This creates
a type mismatch.

**See §Open Questions Q2 for options.** This is the most architecturally significant
open question for this commit.

---

## Open Questions for Ian

These questions require Ian's confirmation before implementation. The implementer
must **not** guess at answers to these.

### Q1 — `skip_pile_ordering` for PredecessorPolicy

Current code:
```cpp
skip_pile_ordering = use_new_cache(s_rules, suit_sym) && !force_lru;
```

For PredecessorPolicy (accordion games): `use_new_cache()` returns `false` (because
`rules.accordion_size > 0` is excluded from `use_new_cache`). So currently
`skip_pile_ordering = false` for accordion games.

**Question:** Is `skip_pile_ordering = false` correct for PredecessorPolicy? I.e.,
do accordion games need `eval_pile_order()` to run in `place_card`/`take_card`? If
accordion piles are never in `tableau_piles`, the call would be a harmless no-op. If
they are, the answer matters for correctness.

If `false` is correct for PredecessorPolicy: add `static constexpr bool skip_pile_ordering`
to each Policy struct (values: Flat=true, HashOnly=true, Predecessor=false, LRU=false).

If `skip_pile_ordering` is not a pure policy property but still depends on runtime
`suit_sym` for some edge case: it must remain a runtime bool in the constructor but
can still be derived from Policy alone at construction time (i.e., `skip_pile_ordering =
Policy::computes_hash && !std::is_same_v<Policy, PredecessorPolicy>`).

### Q2 — Return type of `solve_game()`

`solve_game()` currently returns `pair<solver, solver::result>`. After `solver` is
templated, the four branches of the dispatch switch return four different concrete
types.

**Proposed options** (choose one):

**Option A — Factor out `print_solution()` from solver**  
Move `print_solution()` to a free function that takes `const game_state_impl<P>&` init
state and a `vector<move>` (the frontier). The solver stores only the frontier (not
the init_state) and returns it alongside the result. Then `solve_game()` can return
`pair<vector<move>, solver::result>` — a non-templated type.

**Option B — Type-erase the solver**  
Extract a non-template `solver_base` class (virtual `print_solution()`, virtual result
accessor). `solver<Policy>` inherits from `solver_base`. `solve_game()` returns
`pair<unique_ptr<solver_base>, solver::result>`. Adds a virtual call but only at the
post-DFS reporting step — zero hot-path cost.

**Option C — Do the dispatch switch fully inside `solve_game()`**  
`solve_game()` dispatches, runs, and prints/reports internally. It returns only
`solver::result`. The `main()` caller only gets the result struct. Simplest
approach but removes the ability for main to replay the frontier.

**Recommendation:** Option A has the cleanest interface. Option C is simplest to
implement. Both are acceptable.

**Ian must choose** which option to implement before coding begins.

### Q3 — Constructor signature after removing `force_lru` / `cache_type`

Current public constructors:
```cpp
game_state(const sol_rules&, int seed, streamliner_options, bool force_lru, const string& cache_type);
game_state(const sol_rules&, const Document&, streamliner_options, bool force_lru, const string& cache_type);
```

After conversion, `force_lru` and `cache_type` are no longer needed (they determined
`computing_flat_hash/payload`, which are now Policy traits). They should be removed.

**Question:** Are `force_lru` and `cache_type` used for any purpose in the constructor
body OTHER than computing `computing_flat_hash/payload` and `skip_pile_ordering`? If
so, those uses need alternative handling.

From reading the constructor code: `cache_type` and `force_lru` appear only in:
1. `computing_flat_hash = needs_flat_hash(s_rules, suit_sym, force_lru, cache_type)` → deleted
2. `computing_flat_payload = needs_flat_payload(...)` → deleted
3. `skip_pile_ordering = use_new_cache(s_rules, suit_sym) && !force_lru` → becomes `Policy::skip_pile_ordering`

Conclusion: these parameters can be safely removed from the constructor signatures.
However, `suit_sym` (derived from `stream_opts`) is still needed for `skip_pile_ordering`
if it remains a runtime determination. If `skip_pile_ordering` becomes a Policy trait
(Q1 answer), `suit_sym` is no longer needed in the constructor either.

---

## Suggested Implementation Order

The following order minimises the risk of breaking the build midway:

1. **`cache_policy.h`** — Add `skip_pile_ordering` trait (once Q1 is answered).

2. **`game_state.h`** — Convert to template declaration. Keep all method bodies where
   they currently are (inline) or declare them extern. At this point the file compiles
   but game_state.cpp won't yet. Build will fail — expected.

3. **`game_state.cpp`** — Apply all three transformation patterns. Add explicit
   instantiations. At this point build should succeed for all variant targets.
   **Test:** `./build.sh --release --unit-tests --variants` + unit tests + Level 1
   regression.

4. **Split `.cpp` files** — Add method-prefix changes and explicit instantiations.

5. **`solver.h` + `solver.cpp`** — Template the solver. At this point the default
   binary's `solve_game()` still uses the old non-dispatch-switched path. If the
   `game_state` typedef is `game_state_impl<FlatPolicy>`, this compiles but routes
   all games to FlatPolicy regardless of game type. LRU games will break (they need
   LRUPolicy).

6. **`main.cpp`** — Add the dispatch switch. This is the point where the default
   binary correctly routes all four game types to the correct policy. Full regression
   runs here.

7. **Testing gate** — Run all four variant regression suites (Levels 1-3, all 12
   oracle comparisons) and unit tests. All must pass with node counts matched before
   committing.

---

## Risk Assessment

### High risk
- **Template instantiation errors** during step 3: Any method body that references
  `hash_desc`, `payload`, `computing_flat_hash`, or `computing_flat_payload` by name
  will fail to compile. The three transformation patterns cover all known sites, but
  a grep of the file for these names after editing is the correct verification step.
- **Static member initialisation for template statics** (Z_pred): must use the
  template out-of-line syntax (see §2d). Getting this wrong causes link errors.

### Medium risk
- **The return-type issue in main.cpp** (Q2): depends on chosen option. Option C
  (internalize print logic) requires moving code that currently lives after
  `solve_game()` returns, which touches several code paths in `main()`.
- **`skip_pile_ordering` for PredecessorPolicy** (Q1): wrong value causes incorrect
  pile ordering in accordion games, which changes node counts and breaks Level 1-3
  oracles for LRU and predecessor suites.

### Low risk
- **split `.cpp` files**: These have no hash/payload logic and only need the method
  prefix and explicit instantiation changes. Mechanical.
- **Unit tests**: All use `game_state` typedef → `game_state_impl<FlatPolicy>`. No
  test source changes expected.
- **`recompute_payload_from_scratch()`**: Only called in debug builds under
  `assert_payload_consistent()`. Template-correct as long as `enable_if` or
  `computes_payload` guard is applied consistently.

---

## Testing Gate

Before committing, all of the following must pass:

```bash
# Build all variants
./build.sh --release --unit-tests --variants

# Unit tests
cd cmake-build-release && ctest -R unit_tests --output-on-failure

# Level 1 regression — all four cache variants, node counts enforced
cd cmake-build-release && ctest -R regression_level1 --output-on-failure

# Level 2 and Level 3 if time permits
cd cmake-build-release && ctest -R regression_level2 --output-on-failure
cd cmake-build-release && ctest -R regression_level3 --output-on-failure
```

Node-count oracles from Commit 0 enforce that the DFS traversal is bit-for-bit
identical to the pre-refactor binaries. Any regression in these oracles indicates
a correctness bug in the template conversion, not merely a performance regression.

---
---

# Amendment (2026-04-28): Option B2 and Commit 3a/3b Split

**Decision by Ian:** The cache layer must also be templated with zero virtual
dispatch overhead (Option B2). The solver holds the concrete cache type directly,
not a `cache_interface&`. `cache_interface` is retained for test infrastructure
only (dual_cache parity tests). This amendment updates the plan accordingly.

The original Commit 3 is split into two commits:
- **Commit 3a** — template `game_state` (the core transformation)
- **Commit 3b** — template solver + cache layer, add dispatch switch, eliminate
  virtual dispatch from the DFS hot path

---

## Commit 3a: Template `game_state`

Scope: everything from the original plan's §1–§3 (game_state.h, game_state.cpp,
split .cpp files) plus §4 (cache_policy.h `skip_pile_ordering`). No solver or
cache changes.

After 3a, `game_state_impl<Policy>` exists as a template, and the typedef
`game_state = game_state_impl<FlatPolicy>` (or variant-specific policy) preserves
the existing API for **all** callers. Everything compiles through the typedef.
The solver, caches, solvability_calc, benchmark — none change yet.

### Files that change in 3a

| File | Change |
|---|---|
| `game_state.h` | Template conversion (§1a–1h from original plan) |
| `game_state.cpp` | Patterns A/B/C, explicit instantiations (§2a–2g) |
| `game_state.legal_moves.cpp` | Method prefix + explicit instantiations (§3) |
| `game_state.dominance_moves.cpp` | Method prefix + explicit instantiations (§3) |
| `game_state.pile_order.cpp` | Method prefix + explicit instantiations (§3) |
| `cache_policy.h` | Add `skip_pile_ordering` trait (§4, pending Q1 answer) |

### Files that do NOT change in 3a

Everything else. The `game_state` typedef means all external code compiles
without modification.

### Testing gate for 3a

Same as original plan: `./build.sh --release --unit-tests --variants`, unit
tests, Level 1 regression (all four cache variants). Node counts must match.

---

## Commit 3b: Solver + cache layer — zero-overhead dispatch

Scope: template the solver, add `cache_type` to Policy structs, template or
update all cache implementations to accept `game_state_impl<Policy>`, add the
dispatch switch in `main.cpp`, eliminate `cache_interface` from the solver hot
path.

### Design: how cache dispatch works after 3b

Each Policy struct in `cache_policy.h` gains a `cache_type` typedef naming the
concrete cache class. The solver holds the concrete cache directly:

```cpp
template <typename Policy>
class solver {
    typename Policy::cache_type cache;   // concrete, no virtual dispatch
    game_state_impl<Policy> state;
    // ...
};
```

The dispatch switch in `main.cpp` selects the Policy once, then constructs
`game_state_impl<Policy>` and `solver<Policy>` (which internally constructs
`Policy::cache_type`). Inside the DFS loop, `cache.insert(state)` is a direct
non-virtual call to the concrete cache's method. Zero overhead.

### Policy cache_type mapping

Per Q4 resolution: `generic_flat_cache` is the default for all flat-cache
variants. The old concrete caches remain for dual_cache parity tests only.

```cpp
struct FlatPolicy {
    // ... existing traits ...
    typedef generic_flat_cache<CompactStatePolicy> cache_type;
};
struct HashOnlyPolicy {
    // ...
    typedef generic_flat_cache<HashOnlyClusterPolicy> cache_type;
};
struct PredecessorPolicy {
    // ...
    typedef generic_flat_cache<PredecessorClusterPolicy> cache_type;
};
struct LRUPolicy {
    // ...
    typedef lru_cache cache_type;
};
```

Note: `cache_policy.h` will need to forward-declare `generic_flat_cache` and
the cluster policy types, or include the relevant headers. Since `cache_policy.h`
is a lightweight traits header, forward declarations are preferred; the full
types are needed only when `cache_type` is instantiated (in solver.cpp and
main.cpp, which already include the cache headers).

### The game_state type mismatch and how B2 resolves it

In the default binary, `game_state` is a typedef for `game_state_impl<FlatPolicy>`.
Cache methods that take `const game_state&` would only accept FlatPolicy states.
But the solver for HashOnlyPolicy passes `game_state_impl<HashOnlyPolicy>`.

B2 resolves this because each cache is only used with its corresponding Policy.
The cache's method signatures are updated to take the correct
`game_state_impl<Policy>` type:

- **Old caches** (`flat_cache`, `hash_only_cache`, `predecessor_flat_cache`):
  In variant binaries, the `game_state` typedef already resolves to the correct
  Policy, so no signature changes are needed. In the default binary, the old
  caches are only used from within a dispatch branch where the Policy matches
  the typedef. **However**, `hash_only_cache` and `predecessor_flat_cache` need
  their `insert`/`contains` signatures templated (or the methods changed to take
  `game_state_impl<HashOnlyPolicy>`/`game_state_impl<PredecessorPolicy>`
  respectively) because `game_state` typedef is `FlatPolicy` in the default
  binary.

  **Simplest approach:** template the `insert`/`contains` methods on these
  caches:
  ```cpp
  template <typename GS>
  bool insert(const GS& gs) { ... }
  ```
  The method bodies only call `gs.get_zobrist_hash()` and `gs.get_payload()` (or
  equivalent), which are present on all game_state_impl instantiations that
  compute hashes. The bodies don't change.

- **`generic_flat_cache`:** Already a template. Only needs the cluster policies'
  `hash_of`/`payload_of` methods templated:
  ```cpp
  // BEFORE (in generic_flat_cache_policies.h)
  static uint64_t hash_of(const game_state& gs) { return gs.get_zobrist_hash(); }

  // AFTER
  template <typename GS>
  static uint64_t hash_of(const GS& gs) { return gs.get_zobrist_hash(); }
  ```
  Same for `payload_of`. Bodies are unchanged. `generic_flat_cache::insert` and
  `contains` then need their `const game_state&` parameter templated (or changed
  to a template parameter on the class).

- **`lru_cache`:** Takes `const game_state&` in insert/contains and in its
  constructor (stores `const game_state& init_gs` in the hasher). After
  conversion, it is only used with `LRUPolicy`. Template the methods or change
  the signature to `game_state_impl<LRUPolicy>`. The hasher's stored reference
  also needs the correct type.

  **Note:** `lru_cache`'s `insert_with_iterator()` is called directly from
  `solver::dfs()` (not through `cache_interface`). After B2, the solver holds
  `lru_cache` directly, so this is a direct non-virtual call. The signature
  must accept `game_state_impl<LRUPolicy>`.

### `cache_interface` — kept for test infrastructure

`cache_interface` is NOT deleted. It remains as a polymorphic base for:
- `dual_cache` parity tests (which compare two cache implementations)
- Any future diagnostic/introspection tools

But `solver` no longer holds `cache_interface&`. It holds `Policy::cache_type`
directly. The old caches continue to inherit from `cache_interface` for the
dual_cache tests.

The virtual `insert(const game_state&)` / `contains(const game_state&)` methods
on `cache_interface` continue to use the `game_state` typedef. This works for
dual_cache tests because those tests use `game_state` (the typedef, resolving to
`FlatPolicy`) and only test FlatPolicy-eligible games. If dual_cache tests need
to cover other policies in future, `cache_interface` would need templating — but
that's out of scope for this commit.

### `make_cache` — demoted or eliminated

`make_cache()` in `cache_factory.h` is no longer used by the solver (the solver
constructs `Policy::cache_type` directly). It can be:

1. **Kept** for backward compatibility with solvability_calc/benchmark and test
   code that constructs caches without knowing the Policy.
2. **Eliminated** if solvability_calc/benchmark are updated to use the dispatch
   pattern.

Recommendation: keep `make_cache` for now. It still works for single-Policy
variant binaries and for tests. Mark it deprecated if desired.

### `solver` changes (extends original §5–§6)

Beyond the original plan's changes:

```cpp
template <typename Policy>
class solver {
public:
    typename Policy::cache_type& cache;   // concrete type, not cache_interface&
    // ...
    explicit solver(const game_state_impl<Policy>&, typename Policy::cache_type&);
    // ...
private:
    bool using_flat_cache;   // DELETE — replaced by Policy::computes_hash
    game_state_impl<Policy> state;
};
```

The `using_flat_cache` member and the `dynamic_cast` chain in the constructor
(lines 74–86 of current solver.cpp) are deleted entirely. Replaced by:
`if constexpr (Policy::computes_hash)` at each branch point in `dfs()`.

The `lru_cache::item_list::iterator` in `node::cache_state` is only used for
LRUPolicy. It can remain as `boost::optional` (acceptable overhead for other
policies) or be conditionally included. Defer EBO for now.

**`solver::result`** is not template-dependent. Extract it to a standalone
struct (or keep as `solver<FlatPolicy>::result` and typedef). This resolves
Q2 from the original plan — see §main.cpp below.

### `main.cpp` dispatch switch (extends original §7)

The dispatch switch constructs both the cache and the solver:

```cpp
template <typename Policy>
static solver_result solve_game_impl(
        const sol_rules& rules, uint64_t timeout, uint64_t cache_capacity,
        typename game_state_impl<Policy>::streamliner_options str_opts,
        boost::optional<int> seed,
        boost::optional<const Document&> in_doc) {
    game_state_impl<Policy> gs = seed
        ? game_state_impl<Policy>(rules, *seed, str_opts)
        : game_state_impl<Policy>(rules, *in_doc, str_opts);

    typename Policy::cache_type cache(cache_capacity);
    solver<Policy> sol(gs, cache);
    auto res = sol.run(std::chrono::milliseconds(timeout));

    // Print/report here (Option C from Q2) — or return result struct
    return { res, sol.get_frontier(), gs };
}
```

**Q2 resolution (return type):** With B2, the solver is fully contained inside
the dispatch branch. **Option C** (do all reporting inside `solve_game_impl`)
is the natural fit: the dispatch switch runs, prints, and returns only
`solver_result` (a non-template struct with outcome, stats, timing). The caller
in `main()` never sees the templated solver type.

Note: `lru_cache` constructor takes a `game_state_impl<LRUPolicy>` reference (for
the hasher). For flat caches, the constructor takes only `capacity`. This
asymmetry is handled by the dispatch switch constructing each cache type with
the correct arguments.

### `solvability_calc` and `benchmark`

Both follow the same dispatch-switch pattern. Each has a `solve_seed` (or `run`)
function that constructs game_state + cache + solver. Template this function on
Policy, add a small dispatch switch at the call site.

The existing `game_state::streamliner_options` typedef works because
`streamliner_options` is a nested enum/struct that is identical across all
policy instantiations. Can be extracted to a free enum if needed.

These are ~20 lines of dispatch boilerplate per file. Not hard.

### Files that change in 3b

| File | Change |
|---|---|
| `cache_policy.h` | Add `cache_type` typedef to each Policy |
| `solver.h` | Template on Policy; hold `Policy::cache_type&` |
| `solver.cpp` | Template + explicit instantiations; delete `using_flat_cache` |
| `main.cpp` | Dispatch switch, Option C reporting |
| `generic_flat_cache_policies.h` | Template `hash_of`/`payload_of` |
| `generic_flat_cache.h` | Update `insert`/`contains` to accept templated GS |
| `flat_cache.h/cpp` | Template `insert`/`contains` on GS type |
| `hash_only_cache.h/cpp` | Template `insert`/`contains` on GS type |
| `predecessor_flat_cache.h/cpp` | Template `insert`/`contains` on GS type |
| `global_cache.h/cpp` | Template lru_cache methods + hasher on GS type |
| `solvability_calc.h/cpp` | Dispatch switch in `solve_seed` |
| `benchmark.h/cpp` | Dispatch switch in `run` |

### Files that do NOT change in 3b

| File | Why |
|---|---|
| `cache_interface.h` | Kept as-is for dual_cache test infrastructure |
| `cache_factory.h` | Kept for backward compat; not used by solver |
| `dual_cache.h` | Uses `cache_interface`; works for FlatPolicy tests |
| `compact_state.h/cpp` | No change (confirmed by Commit 2 audit) |
| `hash_descriptor_store.h` | No change |
| `game_state.h/cpp` | Already converted in 3a |
| Unit tests | Use `game_state` typedef; compile without changes |

### Testing gate for 3b

Same as original plan plus:
- Verify no virtual calls remain in the solver hot path (inspect assembly or
  check that `cache_interface` is not referenced from solver.h/cpp)
- Dual_cache tests continue to pass (they use `cache_interface` path, not the
  solver's direct-cache path)

---

## Corrections to Original Plan

### "Files That Do NOT Change" — revised

The original plan's list included several files that DO change in 3b. The split
into 3a/3b makes this precise: nothing outside game_state changes in 3a;
the cache layer and solver change in 3b.

### Q2 answer: Option C

The dispatch switch in `main.cpp` handles all reporting internally. `solve_game`
returns a non-template result struct. This is the natural fit for B2 because the
solver type cannot escape the dispatch branch.

### `check_face_down_consistent()` (line 1094)

The Commit 2 API audit omitted this: `payload.get_descriptor(cid)` is called
inside `#if SOLVITAIRE_COMPUTES_FLAT_HASH` without `#ifndef SOLVITAIRE_HASH_ONLY`.
Pre-existing debug-only bug. Commit 3a's Pattern B transformation
(`payload` → `desc_store`) fixes it. Noted here for completeness.

---

## Open Questions (carried forward, updated)

### Q1 — `skip_pile_ordering` for PredecessorPolicy — RESOLVED

**Answer (Ian, 2026-04-28):** PredecessorPolicy *should* skip pile ordering
(the predecessor hash is order-independent, so pile ordering is unnecessary).
However, the current code has `skip_pile_ordering = false` for accordion games.
To avoid changing behaviour and breaking oracles, **keep `false` for now**.
This is a safe suboptimal default; changing to `true` is a separate future
optimization.

Values for this commit: `FlatPolicy=true`, `HashOnlyPolicy=true`,
`PredecessorPolicy=false` (conservative), `LRUPolicy=false`.

### Q2 — Return type of `solve_game()` — RESOLVED: Option C

See §main.cpp dispatch switch above.

### Q3 — Constructor signature after removing `force_lru` / `cache_type` — RESOLVED

These parameters are removed in 3a. `suit_sym` is no longer needed if
`skip_pile_ordering` becomes a Policy trait (contingent on Q1).

### Q4 (new) — Old caches vs generic_flat_cache — RESOLVED

**Answer (Ian, 2026-04-28):** Use `generic_flat_cache` as the default
`cache_type`, not the old concrete caches. The old caches are being replaced;
using the generic versions by default surfaces any bugs as early as possible.

```cpp
struct FlatPolicy {
    typedef generic_flat_cache<CompactStatePolicy> cache_type;
};
struct HashOnlyPolicy {
    typedef generic_flat_cache<HashOnlyClusterPolicy> cache_type;
};
struct PredecessorPolicy {
    typedef generic_flat_cache<PredecessorClusterPolicy> cache_type;
};
struct LRUPolicy {
    typedef lru_cache cache_type;
};
```

The old concrete caches (`flat_cache`, `hash_only_cache`,
`predecessor_flat_cache`) remain in the codebase for dual_cache parity tests
but are no longer the default production path.

### Q5 (new) — `solvability_calc` and `benchmark`: include or defer? — RESOLVED

**Answer (Ian, 2026-04-28):** Include in 3b. Same dispatch-switch pattern as
`main.cpp` — ~20 lines of boilerplate per file, mechanical change.
