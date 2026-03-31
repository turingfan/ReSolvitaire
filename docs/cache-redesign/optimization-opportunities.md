# Flat Cache Optimization Opportunities

## 1. Power-of-2 Bucket Counts: Direct Bit Masking for Hash Indexing

### Current Approach

The flat cache currently uses **Fibonacci hashing** (multiply-and-shift) to map the 64-bit Zobrist hash to a cluster index, regardless of cluster count:

```cpp
uint64_t flat_cache::cluster_index(uint64_t hash) const {
#ifdef __SIZEOF_INT128__
    return (uint64_t)((uint128_t)hash * num_clusters >> 64);  // Fibonacci hashing
#else
    return hash % num_clusters;  // Fallback modulo
#endif
}
```

This design supports **any bucket count** (power-of-2 or not), making the cache flexible. However, it costs:
- Fibonacci hashing: **~3-4 CPU cycles** (128-bit multiply + right shift)
- Modulo fallback: **~10-20 CPU cycles** (division)

### Optimization Opportunity

If the cache enforced **power-of-2 bucket counts**, indexing could use direct **bit masking**:

```cpp
uint64_t flat_cache::cluster_index(uint64_t hash) const {
    return hash & (num_clusters - 1);  // Single bitwise AND: ~1 CPU cycle
}
```

### Why This Is Safe

The Zobrist hash is constructed by XORing random 64-bit values from lookup tables:

```cpp
// zobrist.cpp initialization
std::uniform_int_distribution<uint64_t> dist;
Z_card[c][d] = dist(rng);  // Fully random across all 64 bits
```

The game state hash combines these via XOR (game_state.cpp lines 944, 955, 964, 973):

```cpp
zobrist_hash_value ^= zobrist_hash::card_key(c, descriptor);
zobrist_hash_value ^= zobrist_hash::foundation_key(suit, rank);
// ... more XORs
```

Since XOR of uniformly random 64-bit values preserves randomness, **the low `n` bits are as uniformly distributed as any other bits**. Direct masking is therefore safe.

### Trade-off Analysis

| Aspect | Gain/Loss |
|--------|-----------|
| **Speed gain** | ~3 CPU cycles per cluster lookup (~2-3x faster) |
| **Scale impact** | In a deep search with billions of lookups, could be measurable (~0.5-2% overall runtime) |
| **Flexibility loss** | Must constrain `num_clusters` to powers of 2 (minor impact) |
| **Implementation cost** | Low: add bit count or validation in constructor |

### Recommendation

**Consider for Phase 2 optimization** (after correctness is verified):

1. Add a constructor parameter: `explicit flat_cache(uint64_t max_entries, bool power_of_2_only = false)`
2. If `power_of_2_only` is true, round `num_clusters` up to the nearest power of 2
3. Update `cluster_index()` with conditional logic:
   ```cpp
   if (power_of_2_optimized) {
       return hash & (num_clusters - 1);
   } else {
       // Current Fibonacci hashing
   }
   ```
4. Benchmark on realistic workloads to quantify gains

### Implementation Notes

- Use `__builtin_clz()` or bit manipulation utilities to round up to power of 2
- Update constructor documentation to explain the trade-off
- No changes to insertion, eviction, or replacement policy—only the hash function
- Backward compatible: existing code with arbitrary capacities continues to work

---

## 2. Generation-Based Cache Aging (Already Implemented)

**Status:** ✓ Implemented in flat_cache via `age` field in compact_state

The cache uses generation-based aging to defer expensive zeroing:
- Each generation, increment a global counter instead of memset'ing the entire cache
- Only clear entries when their generation is stale
- O(1) logical clearing vs O(n) memset cost

No further optimization needed here; this is already optimal.

---

## 3. Other Potential Optimizations (Future Investigation)

### 3.1 Cache Line Prefetching
- Two-slot clusters (64 bytes = one cache line) already align with L1 cache behavior
- Consider explicit `__builtin_prefetch()` on secondary cluster for collision chains
- Impact: Likely negligible; CPU prefetchers already handle this well

### 3.2 Sibling Hash Function
- Current open addressing probes `hash & (num_clusters - 1)`
- Could use a different hash function for the second slot (e.g., `(hash >> 32) & (num_clusters - 1)`)
- Impact: Reduce collision probability, but current two-slot design already mitigates well

### 3.3 Adaptive Load Factor
- Current design doesn't enforce load factor (insertion keeps going until memory exhausted)
- Could track occupancy and warn/adjust when load factor exceeds threshold
- Impact: Marginal; TwoBig1 replacement policy handles high load well

---

## 4. Verification Checklist for Any Optimization

Before committing to power-of-2 bit masking or other changes:

- [ ] Unit tests pass (flat_cache_test.cpp)
- [ ] Regression suite passes (Levels 1–5)
- [ ] Benchmark before/after on representative workloads
- [ ] Profile hot paths to confirm cache lookup is actually a bottleneck
- [ ] Document any constraints in code comments and CLAUDE.md

---

**Document Date:** 2026-03-31
**Branch:** refactor-caching (cache-redesign documentation)
