# Proposal: Templated game_state with Single Runtime Dispatch

**Date:** 2026-04-16  
**Status:** Implemented (Phase A complete, branch `feature/templated-dispatch`, Commits 0-6)  
**Tracked in:** `docs/known-issues.md` §8 (RESOLVED)

---

## Problem Statement

In the current default `solvitaire` binary, `game_state` always computes the Zobrist hash and descriptor payload on every move, even when the selected cache at runtime is `lru_cache` — which never reads either. For games that route to LRU (2-deck, spider, suit-symmetry, accordion), this is pure overhead on the hottest path in the DFS loop.

The short-term fix (Phase 3 workpackage) introduces two boolean flags in `game_state`, set once at construction and checked on every hash/payload update. This eliminates the wasted work but introduces stable branches inside the DFS loop. It is correct and measurably better than the current state. It is not the right long-term architecture.

---

## Proposed Solution

### Core Idea

Instantiate the DFS solver with a compile-time-specialised `game_state` for each cache policy. The runtime decision of which policy to use happens **once per solve**, before the DFS loop starts. Inside the loop there are zero branches on cache type — the compiler generates a fully specialised, branch-free code path for each policy.

### Policy Types

Four policies covering the full routing matrix:

| Policy | Hash computed? | Payload computed? | Cache used |
|---|---|---|---|
| `FlatPolicy` | Yes — Zobrist descriptor hash | Yes — `compact_state` | `generic_flat_cache<CompactStatePolicy>` |
| `HashOnlyPolicy` | Yes — Zobrist descriptor hash | **No** — hash suffices; no per-card payload stored | `generic_flat_cache<HashOnlyPolicy>` |
| `PredecessorPolicy` | Yes — predecessor Zobrist | Yes — `predecessor_state` | `predecessor_flat_cache` |
| `LRUPolicy` | **No** | **No** | `lru_cache` |

### Structure

**`game_state_impl<Policy>`**

`game_state` becomes a template on Policy. Each Policy provides static (or empty) implementations of:

```cpp
struct FlatPolicy {
    static void update_descriptor(compact_state& p, uint64_t& h, uint8_t cid,
                                  uint8_t old_desc, uint8_t new_desc);
    static void update_foundation(compact_state& p, uint64_t& h, uint8_t suit,
                                  uint8_t old_rank, uint8_t new_rank);
    static void update_waste_ptr(compact_state& p, uint64_t& h,
                                 uint8_t old_ptr, uint8_t new_ptr);
    static void update_hole_top(compact_state& p, uint64_t& h,
                                uint8_t old_cid, uint8_t new_cid);
    static void init(compact_state& p, uint64_t& h, const sol_rules& rules,
                     /* pile state */);
};

struct LRUPolicy {
    static void update_descriptor(...) {}  // no-op — inlines to nothing at -O3
    static void update_foundation(...) {}
    static void update_waste_ptr(...) {}
    static void update_hole_top(...) {}
    static void init(...) {}
};
```

The `payload` and `zobrist_hash_value` members of `game_state_impl` are present in all instantiations at the type level, but for `LRUPolicy` they are never written and the compiler eliminates them under optimisation. Alternatively, they can be conditionally included via a `has_flat_state` trait on the policy, removing them from the struct layout entirely for LRU — at the cost of slightly more complexity.

**Typedef hides the template from external code**

At the bottom of `game_state.h`:

```cpp
#if defined(SOLVITAIRE_LRU_ONLY)
    using game_state = game_state_impl<LRUPolicy>;
#elif defined(SOLVITAIRE_FLAT_ONLY)
    using game_state = game_state_impl<FlatPolicy>;
#elif defined(SOLVITAIRE_HASH_ONLY)
    using game_state = game_state_impl<HashOnlyPolicy>;
#else
    // Default binary: game_state is a type alias for the appropriate instantiation
    // selected at the dispatch point; see solve_game() below.
    // For code that does not care about the policy (tests, benchmarks, utilities):
    using game_state = game_state_impl<FlatPolicy>;  // or DefaultPolicy including all
#endif
```

For the default binary, the solver and solve path are templated; see below.

**Single dispatch point**

In `main.cpp` (or `solve_game()`), the one-time policy selection:

```cpp
PolicyTag determine_policy(const sol_rules& rules,
                           bool force_lru, bool suit_sym,
                           const std::string& cache_type) {
    if (use_predecessor_cache(rules))    return PolicyTag::PREDECESSOR;
    if (cache_type == "hash-only")       return PolicyTag::HASH_ONLY;
    if (use_new_cache(rules, suit_sym) && !force_lru)  return PolicyTag::FLAT;
    return PolicyTag::LRU;
}

switch (determine_policy(rules, force_lru, suit_sym, cache_type)) {
    case PolicyTag::FLAT:
        return run_solve<FlatPolicy>(rules, seed, opts, ...);
    case PolicyTag::HASH_ONLY:
        return run_solve<HashOnlyPolicy>(rules, seed, opts, ...);
    case PolicyTag::PREDECESSOR:
        return run_solve<PredecessorPolicy>(rules, seed, opts, ...);
    case PolicyTag::LRU:
        return run_solve<LRUPolicy>(rules, seed, opts, ...);
}
```

Where `run_solve<Policy>` constructs `game_state_impl<Policy>`, the matching cache, and calls a templated `solver<Policy>`.

**`solver<Policy>` (or `solver` templated on game_state type)**

The solver DFS loop takes a `game_state_impl<Policy>&`. The source of the loop does not change at all — it calls `gs.make_move()`, `gs.undo_move()`, `gs.get_legal_moves()` etc., which are the same API regardless of policy. The template is instantiated four times in the binary; the bodies are largely identical. LTO + COMDAT folding deduplicates the structurally identical portions at link time.

**`game_state`'s split `.cpp` files**

`game_state.legal_moves.cpp`, `game_state.dominance_moves.cpp`, `game_state.pile_order.cpp` do not touch hash or payload at all. They implement methods of `game_state_impl` that are identical across all policies. Each file needs explicit instantiation declarations at the bottom:

```cpp
template class game_state_impl<FlatPolicy>;
template class game_state_impl<LRUPolicy>;
template class game_state_impl<HashOnlyPolicy>;
template class game_state_impl<PredecessorPolicy>;
```

`game_state.cpp` itself (which contains hash/payload logic) gets all four instantiations too, but each policy's methods are specialised or delegated to the policy struct.

---

## Benefits

- **Zero overhead inside DFS loop** — no branches, no dead stores, no wasted computation. The LRU code path is as clean as if `compact_state` had never existed.
- **Single source of truth** — one `game_state_impl` template, not four copies of `game_state`. Policy struct contains only the delta.
- **Replaces all preprocessor guards** — `SOLVITAIRE_COMPUTES_FLAT_HASH` and all the `#if !defined(SOLVITAIRE_LRU_ONLY)` guards become unnecessary. The variant binaries are just variant typedef selections.
- **Extensible** — adding a new cache type requires a new policy struct and one `case` in the dispatch switch, with no changes to game_state or solver logic.
- **Testable per policy** — unit tests can construct `game_state_impl<LRUPolicy>` directly and verify that no hash computation occurs.

---

## Costs and Risks

- **Compile time** — solver is instantiated four times. Significant increase in compile time for `solver.cpp` and `game_state.cpp`. Mitigated by explicit instantiation (avoids redundant re-instantiation in every TU).
- **Binary size** — four copies of the solver DFS loop. LTO + COMDAT folding reclaims much of this; net increase likely modest.
- **Migration scope** — `solver.h/cpp` and `main.cpp`'s solve path must be templated on Policy. Files that only *use* `game_state` but do not care about policy (benchmarks, utilities, most tests) are unaffected by the typedef. Files that construct `game_state` directly (a small number) must be updated to specify a policy or use the default typedef.
- **game_state is mission-critical** — any mistake in the template refactor could affect correctness across all code paths. Must be gated on a full Level 1–4 regression run across all four policies before merging.

---

## Relationship to Short-Term Fix

The short-term fix (Phase 3 boolean-guard) and this proposal are **compatible and sequential**:

1. **Phase 3 (now):** Add `bool computing_flat_hash` and `bool computing_flat_payload` to `game_state`. Guard hash operations with the first, payload operations with the second. Eliminates wasted work in the default binary via stable branches. Also guard flat-cache `.cpp` files from LRU-only variant builds. Ship this.

2. **This proposal (later phase):** Replace the boolean guards and preprocessor guards with a policy template. The transition is clean because each boolean guard explicitly marks a site that becomes a `Policy::computes_hash` or `Policy::computes_payload` constexpr branch. The two booleans are a direct runtime prototype of the two policy traits.

---

## Short-Term Fix (Phase 3 — current workpackage)

Two flags set once at `game_state` construction, derived from which cache was selected:

| Cache | `computing_flat_hash` | `computing_flat_payload` |
|---|---|---|
| `generic_flat_cache<CompactStatePolicy>` | true | true |
| `generic_flat_cache<HashOnlyPolicy>` | true | false |
| `predecessor_flat_cache` | true | true |
| `lru_cache` | false | false |

Guards applied:
- `zobrist_hash_value ^= ...` and all Zobrist update calls: `if (computing_flat_hash)`
- `payload.set_*()`, `init_payload_and_hash()`, `set_payload_depth()`, `assert_payload_consistent()`, `recompute_payload_from_scratch()`: `if (computing_flat_payload)`
- Flat-cache `.cpp` files guarded from `SOLVITAIRE_LRU_ONLY` variant builds via preprocessor (variant binaries have no runtime overhead at all — the flags do not exist in those builds)

Result: correct, zero wasted work in the default binary, two stable branches per update call (CPU predicts perfectly after the first iteration of any game).

In the template proposal above, `computing_flat_hash` becomes `Policy::computes_hash` (a static constexpr bool) and `computing_flat_payload` becomes `Policy::computes_payload` — the boolean guards are a direct runtime analogue of the compile-time policy traits.

This is the deliverable for Phase 3. The template proposal above is deferred to a later phase.
