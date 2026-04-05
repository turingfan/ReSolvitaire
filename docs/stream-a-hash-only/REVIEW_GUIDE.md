# Stream A: Hash-Only Cache — Review Guide

**Branch:** `implement-hash-only-cache` (based on `dev`)
**Date:** 2026-04-05
**Status:** Complete. All 169 tests pass. Ready for benchmarking.

---

## What Was Implemented

A hash-only transposition cache that stores only the 64-bit Zobrist hash per entry — no payload at all. This eliminates both payload construction and payload comparison, at the cost of potential false positives from hash collisions.

### New Files

| File | Purpose |
|---|---|
| `hash_only_cache.h/cpp` | 2-way clustered cache, 16 bytes per cluster |
| `hash_only_cache_test.cpp` | 9 GoogleTest cases including dual-cache agreement |

### Key Design Decisions

1. **16-byte clusters (4x density)**: Each cluster holds two `uint64_t` hashes. Compared to `flat_cache`'s 32-byte entries (two per 64-byte cluster), hash_only_cache fits 4x as many entries per byte of memory.

2. **Empty sentinel = 0**: Hash value 0 means empty slot. Actual Zobrist hash values of 0 are remapped to 1 via `normalise()`. The probability of a real hash being 0 is 1/2^64 — negligible.

3. **Fibonacci hashing**: Uses 128-bit multiply-high (`(uint128_t)hash * num_clusters >> 64`) for cluster index mapping. Falls back to modulo on platforms without `__uint128_t`.

4. **Simplified TwoBig1**: Without depth information, the replacement policy simplifies to: slot 0 is preferred (never directly replaced when slot 1 is available), slot 1 is always-replace when both slots are full.

5. **`--cache-type` CLI option**: New option accepting `auto` (default, uses flat_cache) or `hash-only`. This allows A/B benchmarking without code changes.

6. **Only calls `get_zobrist_hash()`**: The `insert()` and `contains()` methods never call `get_payload()`, so no compact_state needs to be constructed or compared. This removes a significant per-node cost from the search.

## Changes Not Obviously Related to Caching

1. **`dual_cache.h` refactored**: Changed from hardcoded `lru_cache` + `flat_cache` pair to accepting two `unique_ptr<cache_interface>` with string names. Constructor signature:
   ```cpp
   dual_cache(std::unique_ptr<cache_interface> primary,
              std::unique_ptr<cache_interface> reference,
              const std::string& primary_name = "primary",
              const std::string& reference_name = "reference");
   ```
   The mismatch logging uses these names. Existing `lru_only_hits`/`flat_only_hits` field names are kept for backward compatibility with test accessors.

2. **`command_line_helper`**: Added `--cache-type` option and `get_cache_type()` accessor. Default is `"auto"`.

3. **`solvability_calc.h/cpp` and `benchmark.h/cpp`**: Added `cache_type` parameter threading. These were refactored to accept the cache type string and construct the appropriate cache.

4. **`solver.cpp`**: `using_flat_cache` detection now includes `hash_only_cache` via `dynamic_cast`.

5. **Test file updates**: `dual_cache_test.cpp`, `mismatch_analyzer.cpp`, `mismatch_diagnostic.cpp` updated to the new `dual_cache` constructor API.

## What to Look For in Code Review

1. **False positive rate**: Hash-only caching will occasionally report "already seen" for a genuinely new state (64-bit collision). For single-deck games with ~10^6 states searched, the expected collision rate is ~10^6 * 10^6 / 2^64 ≈ 5×10^-8 — negligible. But worth verifying empirically via dual-cache comparison.

2. **`normalise()` correctness**: Confirm that remapping hash 0 → 1 doesn't create collisions with naturally-occurring hash value 1. It doesn't — it just means that two distinct states (one with true hash 0, one with true hash 1) would collide. Probability: negligible.

3. **No payload construction cost**: Verify that `insert()` and `contains()` truly only call `gs.get_zobrist_hash()` and never `gs.get_payload()`. This is the main performance benefit.

4. **CLI routing in main.cpp**: The `--cache-type hash-only` path creates a `hash_only_cache` before the normal `use_new_cache()` check. Verify that `--cache-type auto` (default) falls through to normal cache selection.

5. **dual_cache backward compatibility**: The refactored `dual_cache` still supports all existing test patterns. Check that mismatch logging still works correctly with the new name-based output.

## Testing

```bash
cd cmake-build-release
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target unit_tests -j 8

# All tests (169 pass)
ctest -R unit_tests --output-on-failure

# Just hash-only tests
./bin/unit_tests --gtest_filter="HashOnlyCache*"
```

### What the 9 Test Cases Cover

1. **BasicInsertAndContains**: Insert + lookup + move + lookup
2. **DuplicateInsertReturnsFalse**: Deduplication
3. **ContainsReturnsFalseForAbsent**: False negative check
4. **UndoRestoresToCachedState**: Insert, move, undo, lookup
5. **ManyInserts**: Bulk insert of 100 states
6. **ClearEmptiesCache**: Clear resets everything
7. **EvictionOccursWhenFull**: Tiny cache forces eviction
8. **DualCacheAgreement**: hash_only + flat_cache via dual_cache, 100 states from 3 seeds, pre-eviction agreement
9. **DualCacheAgreementThreeSeeds**: Extended multi-seed agreement test

## Benchmarking

```bash
# Hash-only cache on klondike (or any flat-cache-eligible game)
./bin/solvitaire --type klondike --random 42 --json --cache-type hash-only

# Normal flat cache (default)
./bin/solvitaire --type klondike --random 42 --json

# LRU cache baseline
./bin/solvitaire --type klondike --random 42 --json --force-lru

# Solvability comparison (100 seeds)
./bin/solvitaire --type free-cell --solvability 100 --timeout 60000 --json --cache-type hash-only
./bin/solvitaire --type free-cell --solvability 100 --timeout 60000 --json

# Benchmark mode (if on benchmark-python branch with Python tooling)
python3 scripts/run_benchmark.py --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 --output results/hash-only.csv \
    --solver-args "--cache-type hash-only"
```

### What to measure:
- **Wall time**: hash-only should be faster (no payload construction/comparison)
- **States searched**: hash-only may search slightly fewer states (higher effective cache density, fewer evictions) or slightly more (rare false positives causing premature pruning of unseen states)
- **Outcome agreement**: Both caches should produce the same solved/unsolvable verdict for essentially all instances
