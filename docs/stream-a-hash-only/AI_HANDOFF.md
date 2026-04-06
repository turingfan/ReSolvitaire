# Stream A: Hash-Only Cache — AI Handoff

**Branch:** `implement-hash-only-cache`
**Base branch:** `dev`
**Date:** 2026-04-05
**Status:** Complete and all tests passing. Ready for benchmarking.

---

## Branch Context

This branch implements a hash-only transposition cache as an experiment within the ReSolvitaire caching redesign. It is part of a two-stream parallel effort:
- **Stream A** (this branch): Hash-only cache — stores only 64-bit Zobrist hashes, no payload. 4x memory density. Eliminates payload construction.
- **Stream B** (`implement-accordion-predecessor`): Predecessor encoding for accordion games — has known bugs, see its own AI_HANDOFF.md.

Neither branch has been merged to `dev`. The user (Ian Gent) has explicitly requested no merges to dev.

The branch has a single commit beyond dev: `a32ff19 feat: implement hash_only_cache and parameterised dual_cache`. All 169 tests pass.

## Architecture Summary

### Cache Hierarchy

```
cache_interface (abstract base, src/main/game/cache_interface.h)
├── lru_cache (legacy, Boost MultiIndex)
├── flat_cache (descriptor-based Zobrist, 32-byte entries, 64-byte clusters)
├── hash_only_cache (THIS: hash-only, 16-byte clusters)
├── predecessor_flat_cache (Stream B: accordion)
└── dual_cache (metamorphic wrapper, compares two cache_interface implementations)
```

### Key Files and Their Roles

| File | Role |
|---|---|
| `src/main/game/hash_only_cache.h` | Class definition: cluster struct, normalise(), interface |
| `src/main/game/hash_only_cache.cpp` | Implementation: Fibonacci hashing, insert/contains, eviction |
| `src/main/game/dual_cache.h` | Refactored to accept `unique_ptr<cache_interface>` pair with names |
| `src/main/game/cache_interface.h` | Abstract base + `use_new_cache()` routing helper |
| `src/main/input-output/input/command_line_helper.h/cpp` | `--cache-type` option (auto\|hash-only) |
| `src/main/main.cpp` | Cache construction routing based on --cache-type |
| `src/main/evaluation/solvability_calc.h/cpp` | Cache type parameter threading |
| `src/main/evaluation/benchmark.h/cpp` | Cache type parameter threading |
| `src/main/solver/solver.cpp` | `using_flat_cache` detection includes hash_only_cache |
| `src/test/unit_tests/hash_only_cache_test.cpp` | 9 test cases |
| `src/test/unit_tests/dual_cache_test.cpp` | Updated for new dual_cache API |
| `src/test/unit_tests/mismatch_analyzer.cpp` | Updated for new dual_cache API |
| `src/test/unit_tests/mismatch_diagnostic.cpp` | Updated for new dual_cache API |

## Implementation Details

### The normalise() Trick

```cpp
static uint64_t normalise(uint64_t hash) {
    return hash == 0u ? 1u : hash;
}
```

Hash value 0 is reserved as the empty sentinel. If a real Zobrist hash happens to be 0, it's stored as 1. This creates a single artificial collision between hash-0 and hash-1 states, with probability 1/2^64 — negligible.

### Cluster Layout

```
struct cluster {
    uint64_t hashes[2];  // 16 bytes total, 0 = empty
};
```

Four clusters (8 entries) fit in a single 64-byte cache line. This is 4x denser than flat_cache's 32-byte entries.

### Insert Logic

1. Normalise hash
2. Compute cluster index via Fibonacci hashing
3. If hash already in slot 0 or 1: return false (duplicate)
4. If slot 0 empty: fill slot 0
5. Else if slot 1 empty: fill slot 1
6. Else: evict slot 1 (always-replace)

### Contains Logic

1. Normalise hash
2. Compute cluster index
3. Return `(slot0 == h || slot1 == h)`

### Integration with Solver

The solver's `using_flat_cache` flag is set when the cache is a `flat_cache`, `hash_only_cache`, or `dual_cache` (detected via `dynamic_cast`). This flag controls whether the solver uses the flat-cache code path (payload depth setting, `cache.insert()`) vs the LRU-specific code path (iterator-based insertion for solution reconstruction).

For hash_only_cache, `state.set_payload_depth()` is still called (it's harmless — the hash_only_cache ignores the payload entirely). The key optimization is that `insert()` and `contains()` only call `gs.get_zobrist_hash()`, never `gs.get_payload()`, avoiding compact_state construction.

## Test Status

All 169 tests pass: 9 hash-only specific tests, plus all existing unit and integration tests.

## Known Limitations

1. **False positives from hash collisions**: A 64-bit hash has collision probability ~N^2/2^65 for N inserted states. For 10^6 states: ~5×10^-8. This is negligible for correctness but worth measuring empirically.

2. **No depth-based replacement**: Without payload, there's no depth field. The replacement policy is simpler (slot 0 preferred, slot 1 always-replace) but cannot make depth-quality tradeoffs that flat_cache's TwoBig1 can.

3. **Correctness is probabilistic**: Unlike flat_cache (which verifies full payload on lookup), hash_only_cache can incorrectly report "seen" for a new state. This causes the solver to prune valid search branches. In practice, this should almost never change the outcome.

## What Needs Doing Next

1. **Benchmarking**: Compare hash_only_cache vs flat_cache on multiple game types. Key metrics: wall time, states searched, outcome (solved/unsolvable/timeout). Use `--cache-type hash-only` vs default.

2. **Empirical false positive measurement**: Run dual_cache with hash_only as primary and flat_cache as reference. Any pre-eviction mismatch where hash_only says "seen" but flat_cache says "new" is a hash collision. The `DualCacheAgreement` test already does this for 100 states; extend to thousands or run full solvability comparisons.

3. **Consider making hash-only the default**: If benchmarks show it's consistently faster with negligible error rate, it could replace flat_cache as the default for single-deck games.

4. **Do NOT merge to dev** until benchmarking validates the approach.

## Building and Testing

```bash
cd /path/to/ReSolvitaire-caching
git checkout implement-hash-only-cache

# Build
mkdir -p cmake-build-release && cd cmake-build-release
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target solvitaire --target unit_tests -j 8

# Test
ctest -R unit_tests --output-on-failure
./bin/unit_tests --gtest_filter="HashOnlyCache*"

# Quick benchmark comparison
./bin/solvitaire --type klondike --random 42 --json --cache-type hash-only
./bin/solvitaire --type klondike --random 42 --json
```

## Suggested Prompt for Continuing AI Agent

```
You are working on the ReSolvitaire project, branch `implement-hash-only-cache`.
This branch implements a hash-only transposition cache (no payload, 4x density).
Read docs/stream-a-hash-only/AI_HANDOFF.md for full context.

All tests pass. Your task is benchmarking:

1. Compare hash_only_cache vs flat_cache on klondike (seeds 1-50):
   - For each seed, run with --cache-type hash-only and without (default flat_cache)
   - Capture: outcome, states_searched, wall time (use --json output)
   - Report: any outcome differences, average speedup, cache eviction rates

2. Run a dual_cache false-positive measurement:
   - Modify hash_only_cache_test.cpp to run DualCacheAgreement with 1000+ states
   - Report any pre-eviction mismatches

3. Try free-cell and bakers-dozen as well for broader coverage.

Key CLI: --cache-type hash-only enables hash-only cache.
Key files: src/main/game/hash_only_cache.h/cpp

Do not merge to dev. Commit results and any test additions to this branch.
```
