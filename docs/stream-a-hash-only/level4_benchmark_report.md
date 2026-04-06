# Level 4 Benchmark: Hash-Only vs. Flat Cache

## Executive Summary

To isolate algorithmic speedup from memory initialization overhead, we benchmarked the experimental `hash_only_cache` against the baseline `flat_cache` using Level 4 regression instances (harder instances selected from oracles).

The results confirm that while memory initialization (~175ms difference) dominates "easy" instances, the `hash_only_cache` provides a sustained **~10-15% throughput gain** (NPS). However, post-hoc node count analysis revealed rare but real hash collisions in 2 of 114 instances, which inflated the ">10s" speedup figure. See Section 4 for details.

---

## 1. Speedup by Difficulty Bucket (Geo-Mean)

Instances were grouped by their baseline oracle time to observe how speedup scales with search depth.

| Difficulty | Instances | Speedup (Geo-Mean) |
| :--- | :--- | :--- |
| **< 1s** | 19 | **2.153x** |
| **1 - 5s** | 62 | **1.143x** |
| **5 - 10s** | 28 | **1.104x** |
| **> 10s** | 5 | **2.227x** [^1] |

[^1]: The ">10s" geo-mean is inflated by two instances (raglan seed 1202, east-haven seed 871030) where hash collisions caused the hash-only cache to falsely prune large portions of the search tree. Excluding those two outliers, the real speedup for hard instances without collisions is approximately **1.05–1.21x**, consistent with the 1-10s buckets.

### Observations:
- **Small instances (<1s)** are heavily skewed by the ~175ms advantage in zeroing out a 0.8GB (hash-only) vs 3.2GB (flat) memory block.
- **Medium instances (1-10s)** show a consistent **10-14% speedup**. Since initialization overhead is <10% of the total runtime here, this reflects the raw performance gain of skipping state payload construction and better cache line utility.
- **Large instances (>10s)** saw a jump back to **>2x speedup**, but this is largely an artifact of hash collisions (see Section 4).

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
- **False Positives**: 2 instances showed hash collisions (see Section 4). Both still produced correct outcomes, but this is coincidental — hash collisions can cause incorrect outcomes in general.

---

## 4. Hash Collision Analysis

### Methodology

If the hash-only cache behaves identically to the flat cache (no collisions), both caches should search the same number of nodes for every instance. We compute the ratio `flat_nodes / hash_nodes` for each instance: a ratio near 1.000 is expected; a ratio significantly above 1.0 indicates the hash-only cache searched fewer nodes, consistent with false-positive collisions (states falsely reported as already visited, causing the solver to skip exploring them).

### Findings

- **112 of 114 instances** show near-perfect node count agreement: the ratio is within a few nodes of 1.000, confirming the hash-only cache is functionally equivalent to the flat cache in the vast majority of cases.

- **2 instances are clear outliers with far fewer nodes searched in hash-only**:

  | Instance | Flat Nodes | Hash-Only Nodes | Ratio | Flat Time | Hash-Only Time |
  | :--- | ---: | ---: | :--- | ---: | ---: |
  | raglan seed 1202 | 26,850,474 | 4,374,208 | **6.14x** | 16.62s | 2.67s |
  | east-haven seed 871030 | 11,474,478 | 3,473,070 | **3.30x** | 9.76s | 1.39s |

  These ratios are far outside the noise floor and are strong evidence that hash collisions occurred: the cache falsely reported several states as "already visited", causing the solver to prune large subtrees it would otherwise have explored.

- **Both affected instances still produced the correct outcome** (Solved/Unsolvable matching flat cache). This is coincidental: the collision-pruned subtrees happened not to contain the solution path or any misclassified dead-ends in these specific instances. In general, false-positive collisions can cause the solver to declare a solvable game unsolvable or vice versa.

- **A handful of instances show hash-only searching slightly more nodes than flat** (ratio slightly below 1.0, minimum ~0.894x). The most likely explanation is eviction differences between the two cache implementations: the hash-only cache's higher entry density may lead to different LRU eviction patterns, occasionally causing re-exploration of previously seen states.

### Implications

False positives are rare (2 of 114 instances, ~1.8%) but real. The `hash_only_cache` is suitable for performance-oriented exploration where occasional incorrect pruning is acceptable. It should **not** be used for provably-correct solving without a collision-detection mechanism (e.g., storing a partial state fingerprint alongside the hash).

---

### Resource Usage

| Cache Type | Max Allocated GB | Capacity (Entries) |
| :--- | :--- | :--- |
| **Flat Cache** | 3.2 GB | 100 M |
| **Hash-Only** | 0.8 GB | 100 M |

*(Note: Actual resident memory is lower than the allocation because the OS only commits pages as they are touched by the solver).*

## Conclusion

The `hash_only_cache` provides approximately **14% higher throughput** and **4x lower memory allocation** compared to `flat_cache`. However, it carries the risk of very rare hash collisions (observed in ~1.8% of hard Level 4 instances) that cause the solver to silently skip regions of the search tree. In both observed cases the correct outcome was preserved, but this cannot be guaranteed in general.

The hash-only cache is well-suited for **performance-oriented exploration** (e.g., benchmarking, approximate search, or settings where occasional incorrect pruning is tolerable). It is **not suitable for correctness-critical applications** — such as verifying solvability claims — without an additional collision-detection mechanism.
