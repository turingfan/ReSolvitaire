# Level 4 Benchmark: Hash-Only vs. Flat Cache

## Executive Summary

To isolate algorithmic speedup from memory initialization overhead, we benchmarked the experimental `hash_only_cache` against the baseline `flat_cache` using Level 4 regression instances (harder instances selected from oracles).

The results confirm that while memory initialization (~175ms difference) dominates "easy" instances, the `hash_only_cache` provides a sustained **~10-15% throughput gain** (NPS).

---

## 1. Speedup by Difficulty Bucket (Geo-Mean)

Instances were grouped by their baseline oracle time to observe how speedup scales with search depth.

| Difficulty | Instances | Speedup (Geo-Mean) |
| :--- | :--- | :--- |
| **< 1s** | 19 | **2.153x** |
| **1 - 5s** | 62 | **1.143x** |
| **5 - 10s** | 28 | **1.104x** |
| **> 10s** | 5 | **2.227x** |

### Observations:
- **Small instances (<1s)** are heavily skewed by the ~175ms advantage in zeroing out a 0.8GB (hash-only) vs 3.2GB (flat) memory block.
- **Medium instances (1-10s)** show a consistent **10-14% speedup**. Since initialization overhead is <10% of the total runtime here, this reflects the raw performance gain of skipping state payload construction and better cache line utility.
- **Large instances (>10s)** saw a jump back to **>2x speedup**.

---

## 2. Per-Node Throughput (NPS)

Aggregate throughput was measured across all non-timeout definitive instances.

- **Flat Cache NPS**: 1,943,092
- **Hash-Only NPS**: 2,214,907
- **Raw Throughput Gain**: **1.140x (+14.0%)**

This 14% improvement in nodes per second is a direct result of the reduced work-per-node in the solver's core loop.

---

## 3. Reliability & Correctness

- **Outcome Agreement**: Exactly 100% of the 114 instances produced the same result (Solved/Unsolvable/Timeout) between both cache types.
- **False Positives**: Despite storing only a 64-bit Zobrist hash without a state payload for verification, no logic failures or outcome mismatches were detected in these hard instances.

---

### Resource Usage

| Cache Type | Max Allocated GB | Capacity (Entries) |
| :--- | :--- | :--- |
| **Flat Cache** | 3.2 GB | 100 M |
| **Hash-Only** | 0.8 GB | 100 M |

*(Note: Actual resident memory is lower than the allocation because the OS only commits pages as they are touched by the solver).*

## Conclusion

The `hash_only_cache` is a superior implementation for single-deck solitaire games. It uses 75% less allocated memory and is 14% faster in raw throughput.
