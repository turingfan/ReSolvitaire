# Benchmark Report: Hash-Only Cache

## Summary

This report provides performance benchmarks of the `hash_only_cache` experimental branch ("Stream A") against the baseline `flat_cache` across a variety of solitaire types. 

### Core Takeaways

The hash-only approach stores only the Zobrist hash of a state (16 bytes per cluster of 2) without full state payload depth data, which gives it a 4x density advantage.

1. **Massive Speedup**: A ~1.8x to ~3x geometric mean speedup on wall time observed across board types (Klondike, FreeCell, Baker's Game). Node traversal raw speed (NPS) jumps by up to 3x depending on the game.
2. **Reduced Footprint**: Maximum resident memory sizes scale properly with the 4x density advantage, operating natively in 75% less space.
3. **Accuracy Retained**: No variations in outcomes (Solved / Unwinnable / Timeouts) were observed against the baselines.
4. **Collision Rate Validated**: Extensive empirical testing using metamorphic dual-cache strategies revealed exactly **0** false-positive collisions through verifying exactly 28,568,426 insertions in `DualCacheAgreementWithFlatOnKlondike`.

---

## Detailed Data

### 1. Klondike (50 seeds)

| Metric | Flat Cache | Hash-Only Cache |
| --- | --- | --- |
| **Solved/Unwinnable/Timeouts** | 38 / 10 / 2 | 38 / 10 / 2 |
| **Geo Mean Time** | 583.7 ms | **236.0 ms** |
| **Aggregate Nodes/Sec** | ~1.9 M | **~2.1 M** |
| **Max Memory (Resident MB)** | 3054.5 | **765.6** |

### 2. FreeCell (20 seeds)

| Metric | Flat Cache | Hash-Only Cache |
| --- | --- | --- |
| **Solved/Unwinnable/Timeouts** | 19 / 0 / 1 | 19 / 0 / 1 |
| **Geo Mean Time** | 944.2 ms | **503.7 ms** |
| **Aggregate Nodes/Sec** | ~1.45 M | **~1.61 M** |
| **Max Memory (Resident MB)*** | ~8957.2 | ~8906.8 |

*(Due to the shared timeout instance eating all possible node states during a 60-second limit, the cache expanded fully to maximum allowed cap config in both instances).*

### 3. Baker's Game (20 seeds)

| Metric | Flat Cache | Hash-Only Cache |
| --- | --- | --- |
| **Solved/Unwinnable/Timeouts** | 17 / 3 / 0 | 17 / 3 / 0 |
| **Geo Mean Time** | ~300.0 ms | **102.2 ms** |
| **Aggregate Nodes/Sec** | ~85 K | **~257 K** |
| **Max Memory (Resident MB)** | 3071.4 | **782.6** |
