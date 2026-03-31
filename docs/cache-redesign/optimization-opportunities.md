# Flat Cache Optimization Opportunities

Analysis based on the current codebase (`flat_cache.cpp`, `compact_state.h`, `solver.cpp`) and transposition table literature from chess engine practice (Stockfish, Ethereal). Focused on **single-run efficiency** — not multi-run cache reuse.

## Current Architecture Summary

The flat cache is a 2-way set-associative hash table:
- Each **cluster** is 64 bytes (`alignas(64)`), holding two 32-byte `compact_state` entries
- **Indexing**: Fibonacci hashing (128-bit multiply-shift) on M1 Pro; modulo fallback otherwise
- **Verification**: Full 29-byte `memcmp` of payload (bytes 3–31 of compact_state)
- **Replacement**: TwoBig1 — slot 0 is depth-preferred, slot 1 is always-replace
- **Storage**: `std::vector<cluster>` — allocated and zero-initialised in constructor
- **Default capacity**: 100M entries → 50M clusters × 64 bytes = **~3.2 GB zeroed at startup**

The solver hot loop (per non-dominance node) executes:
1. `get_zobrist_hash()` — inline, returns stored `uint64_t`
2. `get_payload()` — inline, returns `const compact_state&`
3. `cluster_index(hash)` — Fibonacci hash (~3–4 cycles on M1)
4. **Load cluster from main memory** — potential L3 miss (~40–80 ns on M1)
5. `is_occupied()` — check `data[0] != 0`
6. `matches()` — `memcmp(data+3, other.data+3, 29)` for each occupied slot
7. Write 32 bytes if inserting

Step 4 (the DRAM fetch) dominates when the working set exceeds L3 cache. Everything else is noise by comparison for large searches.

---

## 1. Memory Allocation: Lazy Initialisation via `mmap`

### The Problem

The constructor does `clusters.resize(num_clusters)`, which zero-fills the entire buffer eagerly. At default capacity (3.2 GB), this takes **hundreds of milliseconds** even on high-bandwidth M1 memory. For short searches that visit only thousands of states and touch a tiny fraction of the table, this upfront cost can exceed the search time itself.

### The Opportunity

Replace `std::vector<cluster>` with a raw `mmap(MAP_ANONYMOUS | MAP_PRIVATE)` allocation. The OS provides zero-filled virtual pages but **defers physical allocation until first touch** (demand paging). Only pages actually written during the search incur any cost.

Benefits for single runs:
- A search touching 10,000 clusters (640 KB) pays for 640 KB of page faults, not 3.2 GB of memset
- A search touching the full table pays approximately the same as today (pages faulted as needed)
- No behavioural change — `is_occupied()` still sees zero in untouched pages

### macOS Considerations

### Platform-Specific APIs

All three major platforms provide virtual memory allocation with deferred physical backing (demand paging). The key operations are: **allocate** (reserve virtual address space, zero-on-first-touch), **release** (return memory to OS), and **reset** (logically clear without physical zeroing — for cache reuse in benchmarking loops).

#### macOS (Darwin) — Primary platform

```cpp
#include <sys/mman.h>

// Allocate: returns zero-filled virtual pages, physical allocation deferred
void* buf = mmap(nullptr, total_bytes,
                 PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
if (buf == MAP_FAILED) throw std::bad_alloc();

// Release:
munmap(buf, total_bytes);

// Reset (logical clear — pages lazily reclaimed, return zeroes on next access):
madvise(buf, total_bytes, MADV_FREE);
```

`MADV_FREE` is preferred on macOS: it marks pages as reclaimable but doesn't immediately tear down page tables. The kernel reclaims them lazily under memory pressure. Subsequent reads return zeroes. `MADV_DONTNEED` also works but forces immediate page table invalidation, which is more expensive for the "clear between benchmark runs" use case.

#### Linux

```cpp
#include <sys/mman.h>

// Allocate: identical API to macOS
void* buf = mmap(nullptr, total_bytes,
                 PROT_READ | PROT_WRITE,
                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
if (buf == MAP_FAILED) throw std::bad_alloc();

// Release:
munmap(buf, total_bytes);

// Reset — two options with different semantics:
madvise(buf, total_bytes, MADV_DONTNEED);  // Immediate: pages freed, next access returns zeroes
madvise(buf, total_bytes, MADV_FREE);      // Lazy (Linux 4.5+): pages freed under memory pressure
```

On Linux, `MADV_DONTNEED` has stronger guarantees than on macOS: it immediately frees physical pages and guarantees zeroes on next access. `MADV_FREE` (available since kernel 4.5) is lazier and faster but pages may retain old data until the kernel needs the memory. **For transposition table clearing, `MADV_DONTNEED` is the safe choice on Linux** — it guarantees `is_occupied()` sees zero in reclaimed pages.

#### Windows

```cpp
#include <windows.h>
#include <memoryapi.h>

// Allocate: reserve + commit (MEM_COMMIT defers physical allocation until first write)
void* buf = VirtualAlloc(nullptr, total_bytes,
                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
if (buf == nullptr) throw std::bad_alloc();

// Release:
VirtualFree(buf, 0, MEM_RELEASE);

// Reset (discard physical pages, next access returns zeroes):
// Option A — DiscardVirtualMemory (Win 8.1+, preferred):
DiscardVirtualMemory(buf, total_bytes);

// Option B — VirtualAlloc MEM_RESET + MEM_RESET_UNDO pattern:
VirtualAlloc(buf, total_bytes, MEM_RESET, PAGE_READWRITE);
```

`DiscardVirtualMemory` is the closest Windows equivalent to `madvise(MADV_DONTNEED)`: it discards physical pages and guarantees zeroes on next access. It's available from Windows 8.1 onwards. The older `MEM_RESET` approach is weaker — it hints that pages are not needed but does **not** guarantee zeroed contents on re-read, so it is unsafe for our use case unless paired with an explicit re-commit.

### Platform Abstraction

A thin wrapper isolates the platform differences:

```cpp
// platform_memory.h
namespace platform {
    void* alloc_zeroed(size_t bytes);   // mmap / VirtualAlloc
    void  release(void* ptr, size_t bytes);  // munmap / VirtualFree
    void  reset_to_zero(void* ptr, size_t bytes);  // madvise / DiscardVirtualMemory
}
```

Implementation via `#ifdef __APPLE__` / `#ifdef __linux__` / `#ifdef _WIN32`. All three paths guarantee:
1. Freshly allocated memory reads as zero (demand paging)
2. After reset, subsequent reads return zero (safe for `is_occupied()` check)
3. Alignment ≥ page size (4096 bytes), satisfying `alignas(64)` cluster requirement

### Implementation Sketch

```cpp
class flat_cache : public cache_interface {
    cluster* clusters;       // raw pointer to platform-allocated region
    size_t   alloc_bytes;
    uint64_t num_clusters;
    // ...

    flat_cache(uint64_t max_entries) {
        num_clusters = std::max<uint64_t>(1, max_entries / 2);
        alloc_bytes = num_clusters * sizeof(cluster);
        clusters = static_cast<cluster*>(platform::alloc_zeroed(alloc_bytes));
    }

    ~flat_cache() { platform::release(clusters, alloc_bytes); }

    void clear() override {
        platform::reset_to_zero(clusters, alloc_bytes);
        occupied_count = 0;
        eviction_count = 0;
    }
};
```

### Estimated Impact

| Scenario | Current (vector) | With lazy allocation |
|----------|-----------------|----------------------|
| Short search (10K nodes, ~640 KB touched) | ~200–400 ms init + ~5 ms search | ~5 ms total |
| Long search (100M nodes, full table) | ~200–400 ms init + search | ~same total (pages faulted during search) |

**This is likely the single largest efficiency gain available**, especially for benchmarking workloads that repeatedly create and destroy caches.

### Risks and Mitigations

- **Platform-specific code**: Isolated behind `platform_memory.h` abstraction; each platform path is ~10 lines
- **Error handling**: All three APIs have clear failure indicators (`MAP_FAILED`, `nullptr`); throw `std::bad_alloc`
- **Alignment**: All platform allocators return page-aligned memory (≥4096 bytes), satisfying the 64-byte `alignas` requirement
- **RAII**: Raw pointer requires manual cleanup; encapsulate in destructor (shown above) or `unique_ptr` with custom deleter
- **Fallback**: If platform allocation fails (e.g. unsupported OS), fall back to `std::vector<cluster>` with eager zeroing — correctness is preserved, only startup performance degrades

---

## 2. Power-of-2 Bucket Count: Bit Masking for Hash Indexing

### Current Approach

```cpp
uint64_t flat_cache::cluster_index(uint64_t hash) const {
#ifdef __SIZEOF_INT128__
    return (uint64_t)((uint128_t)hash * num_clusters >> 64);  // ~3-4 cycles
#else
    return hash % num_clusters;  // ~10-20 cycles
#endif
}
```

The constructor sets `num_clusters = max_entries / 2` with no rounding, so the count can be any integer.

### Optimised Approach

If `num_clusters` is constrained to a power of 2:

```cpp
return hash & (num_clusters - 1);  // ~1 cycle
```

### Why This Is Safe for Zobrist Hashing

The Zobrist tables are initialised with `std::uniform_int_distribution<uint64_t>` via `mt19937_64` (`zobrist.cpp` lines 14–20). The game state hash is built by XORing these uniformly random 64-bit values. XOR preserves bit-independence: the low `n` bits of the final hash are as uniformly distributed as any other `n` bits.

This is a key difference from multiplicative or polynomial hash functions where low bits can have poor entropy. Zobrist hashing does not suffer from this problem.

Note: unlike chess engines that split the hash into index bits and verification bits, ReSolvitaire uses a completely separate verification mechanism (the 29-byte compact_state payload comparison). The Zobrist hash is used *only* for indexing, and verification is independent of it. There is therefore no concern about index/signature bit overlap.

### Trade-off

- **Gain**: ~2–3 CPU cycles per cache access
- **Loss**: Must round `num_clusters` to next power of 2, potentially using up to 2× more memory than requested in the worst case (e.g., requesting 50M+1 clusters → 64M allocated)
- **Mitigation**: Round *down* instead of up to stay within budget, accepting slightly fewer entries

### Implementation

```cpp
// Round down to largest power of 2 <= n
uint64_t floor_pow2(uint64_t n) {
    if (n == 0) return 1;
    // Set all bits below the highest set bit, then shift
    n |= (n >> 1); n |= (n >> 2); n |= (n >> 4);
    n |= (n >> 8); n |= (n >> 16); n |= (n >> 32);
    return (n >> 1) + 1;
}
```

Store a mask `index_mask = num_clusters - 1` as a member to avoid recomputing.

---

## 3. Huge Pages (2 MB Superpages)

### The Problem

With a 3.2 GB table and 4 KB page size, the table spans **~800,000 pages**. Each cache access to a new page requires a TLB (Translation Lookaside Buffer) lookup. The M1 Pro's L1 dTLB holds ~160 entries (4 KB pages); the L2 TLB holds ~3072 entries. Once the working set exceeds what the TLB can cover, every cache access risks a TLB miss penalty (~10–20 cycles to walk the page table on M1).

With 2 MB huge pages, the same 3.2 GB table requires only ~1,600 pages — well within L2 TLB capacity for reasonable working sets.

### macOS Availability

macOS supports superpages via `mach_vm_allocate` with `VM_FLAGS_SUPERPAGE_SIZE_2MB`:

```cpp
#include <mach/mach.h>

mach_vm_address_t addr = 0;
kern_return_t kr = mach_vm_allocate(
    mach_task_self(),
    &addr,
    total_bytes,
    VM_FLAGS_ANYWHERE | VM_FLAGS_SUPERPAGE_SIZE_2MB);
```

This is more complex than standard `mmap` and has constraints:
- Allocation size must be a multiple of 2 MB
- May fail if physical memory is fragmented
- Should fall back to regular pages gracefully

### Estimated Impact

Chess engine literature (Stockfish benchmarks, TCEC wiki) reports **5–10% NPS improvement** from huge pages on x86. The M1 Pro has a more efficient TLB hierarchy than x86, so the benefit may be smaller (~2–5%) but is still non-trivial for long searches.

### Recommendation

Implement as an optional enhancement with graceful fallback. Worth measuring on actual solitaire workloads before committing, as the benefit depends on the table's access pattern (which may be more localised than chess due to solitaire's tree structure).

---

## 4. `matches()` Comparison Width

### Current Implementation

```cpp
// compact_state.cpp
bool compact_state::matches(const compact_state& other) const {
    return std::memcmp(data + 3, other.data + 3, 29) == 0;
}
```

This compares 29 bytes starting at offset 3. On ARM64, the compiler will likely emit something like three 8-byte loads + a 5-byte tail comparison, but the unaligned start (offset 3) and odd length (29) prevent optimal vectorisation.

### Possible Improvement

If the compact_state layout were adjusted so that the comparison region is aligned and a power-of-2 length, the compiler could use two 128-bit NEON loads:

Option A: Compare all 32 bytes and mask out bytes 0–2:
```cpp
// Compare using two 128-bit loads (bytes 0-15 and 16-31)
// Mask out bytes 0-2 (occupied flag + depth)
uint64_t* a = reinterpret_cast<uint64_t*>(data);
uint64_t* b = reinterpret_cast<uint64_t*>(other.data);
// Mask: zero out first 3 bytes of first word
uint64_t mask0 = 0xFFFFFFFFFF000000ULL;  // big-endian dependent!
return ((a[0] ^ b[0]) & mask0) == 0
    && a[1] == b[1]
    && a[2] == b[2]
    && a[3] == b[3];
```

Option B: Move the occupied flag and depth to bytes 29–31 (the end of the struct), so `matches()` becomes `memcmp(data, other.data, 29)` starting at offset 0. This is slightly better aligned.

Option C: Accept the 29-byte memcmp. Modern compilers and ARM64 hardware handle unaligned access well, and `memcmp` is heavily optimised in libc. **Profile before changing** — the DRAM latency of loading the cache line (step 4 in the hot loop) almost certainly dominates the comparison cost.

### Recommendation

Low priority. Profile first. The comparison happens at most twice per cache access (once per slot), and the data is already in L1 once the cache line is loaded.

---

## 5. Virtual Dispatch Overhead

### Current Design

The solver accesses the cache through `cache_interface&`, with `insert()` and `contains()` as virtual methods. Each call goes through a vtable indirection.

```cpp
// solver.cpp line 131
is_new_state = cache.insert(state);  // virtual call
```

However, the solver *also* has a `bool using_flat_cache` flag and branches on it (line 128). So there's already a runtime type check.

### Cost Analysis

A virtual call costs ~2–3 extra cycles (vtable load + indirect branch) on M1. The branch predictor will learn this quickly since the cache type never changes during a run. The cost is negligible compared to DRAM access.

### Recommendation

Not worth changing unless profiling shows otherwise. If it ever matters, the solution is template parameterisation (CRTP) of the solver on cache type, eliminating virtual dispatch entirely. But this would be a significant refactor for minimal gain.

---

## 6. Prefetching

### Current Situation

The flat cache's 2-way cluster design ensures both entries are in the same 64-byte cache line. Once the cache line is loaded from DRAM, both `is_occupied()` and `matches()` checks on both slots hit L1. This is already well-designed.

### Possible Improvement

Software prefetching could hide DRAM latency by issuing the cluster load early:

```cpp
// In solver, before calling insert:
__builtin_prefetch(&clusters[cluster_index(hash)], 1, 1);
// ... do other work (e.g. set_payload_depth) ...
// Then call insert — cluster may already be in L1
```

This only helps if there is useful work to do between the prefetch and the access. Looking at the solver hot loop, `set_payload_depth` (line 129–130) is the only work between the hash computation and the cache access, which is probably too little to hide a ~40 ns DRAM latency.

### Recommendation

Low priority. Would require exposing the hash computation separately from `insert()`, which complicates the interface. Only worth it if profiling shows DRAM stalls are a significant fraction of runtime.

---

## Priority Summary

| Optimization | Expected Impact | Effort | Priority |
|---|---|---|---|
| **1. mmap lazy allocation** | Major for short runs (10–100× faster startup) | Medium (platform-specific RAII) | **High** |
| **2. Power-of-2 bit masking** | ~2–3 cycles/access (~1–3% NPS for large searches) | Low | **Medium** |
| **3. Huge pages** | ~2–5% NPS for large searches | Medium (macOS-specific API) | **Medium** |
| **4. matches() alignment** | Negligible (dominated by DRAM latency) | Low | Low |
| **5. Virtual dispatch** | Negligible | High (CRTP refactor) | Low |
| **6. Software prefetch** | Uncertain (depends on pipeline) | Low | Low |

---

## Verification Checklist for Any Optimization

Before committing any change:

- [ ] Unit tests pass (`flat_cache_test.cpp`)
- [ ] Regression suite passes (Levels 1–5)
- [ ] Benchmark before/after on representative workloads (short and long searches)
- [ ] Profile hot paths to confirm the optimised path is actually a bottleneck
- [ ] Test on macOS (primary platform) — verify mmap/madvise/superpage behaviour

---

**Document Date:** 2026-03-31
**Branch:** refactor-caching (cache-redesign documentation)
**Based on:** Code analysis of flat_cache.cpp, compact_state.cpp, solver.cpp, zobrist.cpp; chess engine literature (Stockfish, Ethereal, chessprogramming.org)
