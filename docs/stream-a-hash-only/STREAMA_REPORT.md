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
- Given that outcomes were identical and 0 collisions were found in 28.5 million states, the `hash_only_cache` is a highly efficient and safe alternative for single-deck games.

## 4. Known Issues
See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for details on benchmarking script limitations discovered during this run.
