# Known Issues - Argument Passing & Consistency

This document tracks subtle issues identified during the implementation and validation of the Accordion Predecessor Cache (Stream B).

## Resolved Issues

The following tests were failing during earlier development but are now fixed (commit bfc9365):
- `PredecessorCacheTest.UndoRestoresToCachedState` — was failing because the `game_state` seed constructor exited early via `if (rules.tableau_pile_count == 0) return;` before reaching `init_predecessor_state()`. Fixed by wrapping the tableau-dealing loop in `if (rules.tableau_pile_count > 0)` instead of using an early return.
- `PredecessorCacheTest.MultipleMovesAndUndos` — same root cause, same fix.

## 1. Force LRU Argument Dropped in `game_state` Constructors

The `game_state` constructors take a `bool force_lru` argument which is intended to signal if only the legacy LRU cache should be used. Currently, this flag is:
- Passed from public constructors to the private common constructor.
- **Ignored** by the private constructor (it is not assigned to any member variable).
- **Not checked** by `game_state::uses_predecessor_cache()`.

### Problem
Because the state doesn't know it's being used with an LRU cache, it continues to compute and maintain the 64-byte predecessor state (Zobrist XORs, array updates, and undo frames) for every move. This is functionally correct but adds unnecessary overhead when the predecessor cache is disabled via `--force-lru`.

## 2. Inconsistent Cache Selection in `benchmark::run_json`

The `benchmark::run_json` function (triggered by `--benchmark-json`) does not take or respect a `force_lru` parameter. It unconditionally initializes the predecessor cache if `use_predecessor_cache(rules)` is true:

```cpp
// src/main/evaluation/benchmark.cpp
if (use_predecessor_cache(rules)) {
    cache_ptr = std::make_unique<predecessor_flat_cache>(cache_capacity);
}
```

This prevents using the JSON-based benchmarking infrastructure for baseline comparisons against the LRU cache for accordion games.
