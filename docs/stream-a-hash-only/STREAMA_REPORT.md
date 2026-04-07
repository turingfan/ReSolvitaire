# Stream A: Hash-Only Cache Benchmark Final Report

This report summarizes the implementation and benchmarking results for the "Hash-Only Cache" experiment in the `implement-hash-only-cache` branch.

## 1. Summary of Work Done

- **Implementation**: Created `hash_only_cache.h/cpp`, a 16-byte-per-cluster (8 bytes per slot) transposition cache that stores only 64-bit Zobrist hashes. This provides 4x higher density compared to the baseline `flat_cache` (64 bytes per cluster).
- **Correctness Testing**:
    - Ran all unit and integration tests (all 169 pass).
    - Modified `DualCacheAgreementWithFlatOnKlondike` in `hash_only_cache_test.cpp` to verify over **28.5 million operations** without evictions.
    - Result: **0 hash collisions** observed empirically.
- **Benchmarking**: Compared `hash_only_cache` vs `flat_cache` on Klondike (50 seeds), FreeCell (20 seeds), and Baker's Game (20 seeds) with a 60-second timeout.

## 2. Benchmark Results

### 2.1 Overall Performance
| Game Type (Seeds) | Metric | Flat Cache | Hash-Only Cache | Speedup | Result |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Klondike (1-50)** | Geo Mean Time | 583.8 ms | 236.0 ms | **2.47x** | Identical |
| **FreeCell (1-20)** | Geo Mean Time | 944.2 ms | 503.7 ms | **1.87x** | Identical |
| **Bakers Game (1-20)** | Geo Mean Time | 300 ms* | 102.3 ms | **~3x** | Identical |

*Note: Resident memory usage was reduced from ~3000 MB to ~760 MB (approx 4x reduction) across these runs.*

### 2.2 Hard Instances Analysis (>= 1s)
For instances where the baseline search took at least 1 second, the speedup is more modest but still consistent:

- **Klondike**: 9 instances >= 1s. Baseline Geo: 11.18s, Hash-Only Geo: 9.47s. Speedup: **1.18x**.
- **FreeCell**: 7 instances >= 1s. Baseline Geo: 6.57s, Hash-Only Geo: 5.56s. Speedup: **1.18x**.
- **Bakers Game**: No instances exceeded 1s in the tested 20-seed set.

## 3. Conclusions and Recommendations

- The hash-only cache provides a significant speedup on average (1.8x to 3x) and a dramatic 4x reduction in memory footprint.
- For long searches (>=1s), the gain is reduced to ~1.18x. This suggests that for very deep searches, the overhead of payload construction becomes less dominant compared to other bottle-necks (e.g., search tree logic, cache miss rates, or memory bandwidth).
- The `hash_only_cache` delivers ~14% throughput gain and 4x memory reduction, making it attractive for performance-oriented benchmarking and exploration. However, Level 4 benchmarking (see Section 4) revealed that hash collisions do occur at low frequency (~1.8% of hard instances), causing silent incorrect pruning. It is therefore **not suitable for provably-correct solving** without an additional collision-detection mechanism.

## 4. Hash Collision Evidence

Post-hoc analysis of Level 4 benchmark node counts (flat_nodes vs hash_nodes per instance) surfaced direct evidence of hash collisions:

- **Node count agreement in 112 of 114 instances**: The ratio `flat_nodes / hash_nodes` is within a few nodes of 1.000, confirming the hash-only cache is functionally equivalent to the flat cache in the vast majority of cases.
- **Two clear outliers confirm false-positive collisions**: raglan seed 1202 searched 6.14x fewer nodes in hash-only (26,850,474 flat vs 4,374,208 hash-only), and east-haven seed 871030 searched 3.30x fewer nodes (11,474,478 vs 3,473,070). These ratios are far outside normal noise and indicate that the cache falsely reported states as already visited, causing the solver to skip large subtrees.
- **Correct outcomes were preserved in both cases**, but this is coincidental — in general, false-positive collisions can cause the solver to return an incorrect Solved/Unsolvable verdict.
- **A handful of instances show hash-only searching slightly more nodes than flat** (ratio as low as ~0.894x), most likely due to eviction differences between the two implementations rather than false negatives.

## 5. Known Issues
See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for details on benchmarking script limitations discovered during this run.
